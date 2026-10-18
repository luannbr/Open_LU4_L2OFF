#include "AccountDB.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/Odbc.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Account.h"
#include "AccountContainer.h"
#include "ClientSession.h"
#include "ConnectedIPList.h"
#include "Config.h"
#include "DBConn.h"
#include "GPProxyServerMgr.h"
#include "GradeServerMgr.h"
#include "IFatigue.h"
#include "IOServerEx.h"
#include "include/Generator.h"
#include "IOServer.h"
#include "IPSessionDB.h"
#include "Log.h"
#include "PAServerMgr.h"
#include "PBServerMgr.h"
#include "Protocol.h"
#include "Reporter.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Util.h"
#include "WSMServerMgr.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

constexpr authd::platform::OdbcReturn kSqlSuccess = 0;
constexpr authd::platform::OdbcReturn kSqlSuccessWithInfo = 1;
constexpr authd::platform::OdbcInteger kSqlNts = -3;
constexpr authd::platform::OdbcSmallInt kStatementHandle = 3;
constexpr authd::platform::OdbcSmallInt kInput = 1;
constexpr authd::platform::OdbcSmallInt kOutput = 4;
constexpr authd::platform::OdbcSmallInt kCChar = 1;
constexpr authd::platform::OdbcSmallInt kCBinary = -2;
constexpr authd::platform::OdbcSmallInt kCBit = -7;
constexpr authd::platform::OdbcSmallInt kCShort = 5;
constexpr authd::platform::OdbcSmallInt kCInt = 4;
constexpr authd::platform::OdbcSmallInt kCUInt = -18;
constexpr authd::platform::OdbcSmallInt kCTinyInt = -6;
constexpr authd::platform::OdbcSmallInt kCUTinyInt = -28;
constexpr authd::platform::OdbcSmallInt kCTimestamp = 93;
constexpr authd::platform::OdbcSmallInt kSqlVarchar = 12;
constexpr authd::platform::OdbcSmallInt kSqlTinyInt = -6;
constexpr authd::platform::OdbcSmallInt kSqlVarBinary = -3;
constexpr authd::platform::OdbcSmallInt kSqlBit = -7;
constexpr authd::platform::OdbcSmallInt kSqlTimestamp = 11;

} // namespace

#if defined(_WIN32)
void __stdcall QuitGameTimer(
#else
void QuitGameTimer(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);

#if defined(_WIN32)
void __stdcall TimerRoutine(
#else
void TimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);

std::int32_t AccountDB::sBillLogTraceId = 0;
std::int32_t AccountDB::sReconnectKey = 0;

// L2Authd.exe 0x0040EB70, Src/accountdb.cpp:3741-3770.
void AccountDB::GetProfileFromPortalUserData(
    CAccount& account,
    const PortalUser& portalUser) {
    const std::int32_t now =
        runtime::crt::Time32Now();
    const std::time_t rawNow =
        static_cast<std::time_t>(now);
    const std::tm current =
        *std::localtime(&rawNow);

    const std::uint16_t birthYear =
        static_cast<std::uint16_t>(
            portalUser.birthdate);
    const std::uint16_t birthMonthDay =
        ntohs(static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(
                portalUser.birthdate) >> 16U));
    const std::uint16_t currentMonthDay =
        static_cast<std::uint16_t>(
            (current.tm_mon + 1) << 8U) |
        static_cast<std::uint16_t>(current.tm_mday);
    int age = current.tm_year + 1900 - birthYear;
    if (birthMonthDay > currentMonthDay) {
        --age;
    }
    account.age = static_cast<char>(age);
    account.gender = portalUser.sex;
}

// L2Authd.exe 0x0040EC40, Src/accountdb.cpp:3773-3829.
bool AccountDB::AuthenticateOtp(
    CClientSession* client,
    PreLoginUser& user) {
    SecAuth::Enum service = SecAuth::NONE;
    const SecAuth::Enum googleOtp =
        SecAuth::GOOGLEOTP;
    if (user.account.securityFlag.IsActivated(
            googleOtp)) {
        service = SecAuth::GOOGLEOTP;
    }

    if (service == SecAuth::NONE) {
        if (user.otpAnswer !=
            config.OTPNotAssignedValue) {
            user.account.warn_flag |=
                config.OTPAnswerInputedFlag;
            // Log.AddLog(
            //     LOG_WARN,
            //     "AuthenticateOtp - user forgot otp status. uid[%u]",
            //     user.account.uid)
            // L2_LOG_WARN(Log,
                // "AuthenticateOtp - user forgot otp status. uid[%u]",
                // user.account.uid)
            L2_LOG_WARN(Log,
                "[login] Account %u supplied an OTP answer while OTP is not activated. "
                "Recording the warning flag and continuing authentication.",
                user.account.uid);
        }
        return true;
    }

    if (user.account.securityUsage.IsPassed(
            service)) {
        return true;
    }
    if (user.otpAnswer == -1) {
        client->SetPreLoginUser(user);
        client->Send(
            "cdhhc",
            13,
            user.account.uid,
            client->GetOtpFailCount(),
            config.OTPRetryCount,
            service);
        return false;
    }

    char result = 20;
    if (service == SecAuth::GOOGLEOTP) {
        result = CGpProxyServerMgr::Inst()->VerifyOtp(
            ORIGIN_CLIENT_LOGIN,
            client,
            user);
    }
    if (result != 0) {
        user.account.securityUsage.SetFailed(service);
        client->SendFail(1, result, nullptr);
    }
    return false;
}

// L2Authd.exe 0x0040EDA0, Src/accountdb.cpp:3832-3855.
bool AccountDB::AuthenticateSecurityCard(
    CClientSession* client,
    PreLoginUser& user) {
    if (!config.useSecurityCard) {
        return true;
    }

    const SecAuth::Enum service =
        SecAuth::SECURITYCARD;
    if (!user.account.securityFlag.IsActivated(
            service)) {
        return true;
    }
    if (user.account.securityUsage.IsPassed(
            service)) {
        return true;
    }

    const unsigned char cardKey =
        GetCardKeyNSaveState(
            user.account.uid,
            user.account.card_number,
            &user);
    client->Send(
        "cdc",
        10,
        user.account.uid,
        cardKey);
    return false;
}

// L2Authd.exe 0x0040EE60, Src/accountdb.cpp:1385-1614.
char AccountDB::AboutToPlay(
    CClientSession* pSession,
    int uid,
    LoginUser* pLoginUser,
    int time_left,
    int serverid,
    int stat,
    int reservations) {
    int fatigueRestTime = 0;
    int fatiguePlayTime = 0;
    int fatigueSecSinceLastLogout = 0;
    if (pSession != nullptr &&
        config.useFatigueSystem &&
        AccountContainer::Inst()->IsFatigueCustomer(
            pLoginUser->fcid,
            pLoginUser->age,
            pLoginUser->auth_flag)) {
        AccountContainer::Inst()->GetAccountTime(
            pLoginUser->fcid,
            &fatigueRestTime,
            &fatiguePlayTime);
        AccountContainer::Inst()->GetLastLogoutTime(
            pLoginUser->fcid,
            &fatigueSecSinceLastLogout);
    }

    char error = 0;
    int result = 0;
    if (pSession != nullptr) {
        bool alreadyPlaying = false;
        {
            L2_GUARD(m_lock);
            const auto it = usermap.find(uid);
            if (it != usermap.end()) {
                LoginUser* const liveUser = it->second;
                if (liveUser->um_mode == UM_PLAY_OK ||
                    liveUser->um_mode == UM_IN_GAME) {
                    alreadyPlaying = true;
                } else {
                    liveUser->fatigueRestTime = fatigueRestTime;
                    liveUser->fatiguePlayTime = fatiguePlayTime;
                    liveUser->timeAfterLastLogout =
                        fatigueSecSinceLastLogout;
                }
            }
        }
        if (alreadyPlaying) {
            error = 7;
            pSession->Send("cc", 6, error);
            return error;
        }
    }

    [[maybe_unused]] const int OperationCode =
        (stat % 1000) / 100;
    if (!GetServerStatus(static_cast<char>(serverid)) &&
        pSession != nullptr) {
        error = 8;
        pSession->Send("cc", 6, error);
        return error;
    }

    if (g_ServerList.ExistId(serverid)) {
        // Original behavior bug: L2Authd.exe sends at 0x40F2C8/0x40F411,
        // then updates routing at 0x40F482. A fast world reply can win that
        // race. Publish routing first. The original branch is commented below.
        if (pSession != nullptr) {
            error = static_cast<char>(UpdateSessionId(
                uid, pSession->GetSessionId(), pLoginUser->md5key, serverid));
            if (error != 0) {
                pSession->Send("cc", 6, error);
                return error;
            }
        }
        char vmFlag = 0;
        if (config.usePBServer &&
            !CIPSessionDB::IsIPCharging(stat) &&
            pLoginUser->chargeServerId == 2) {
            vmFlag = 1;
        } else {
            reservations = 0;
        }
        if (config.useNotifyCSResult) {
            vmFlag = static_cast<char>(
                vmFlag |
                (((pLoginUser->subscription >> 31) & 1) << 7));
        }

        char bufZoneFatigueInfo[1553];
        bufZoneFatigueInfo[0] = 0;
        std::memset(
            bufZoneFatigueInfo + 1,
            0,
            sizeof(bufZoneFatigueInfo) - 1);
        int zoneFatigueInfoTotLen = 0;
        [[maybe_unused]] unsigned char zoneFatigueAvailCount = 0;
        const int itemId = pLoginUser->chargeServerId == 2
            ? pLoginUser->pbServer.itemId
            : 0;
        const WorldServer world =
            g_ServerList.GetAt(serverid);

        if (config.useExtendPacket) {
            result = SendSocket(
                world.inner_addr,
                "cdsddddcdddsdcdcbhhbddbd",
                0,
                uid,
                pLoginUser->account,
                time_left,
                pLoginUser->loginflag,
                pLoginUser->warnflag,
                stat,
                vmFlag,
                reservations,
                pLoginUser->fatigueRestTime,
                pLoginUser->fatiguePlayTime,
                pLoginUser->loginCafe,
                static_cast<int>(pLoginUser->loginIp.s_addr),
                GetComputationBasis(*pLoginUser),
                pLoginUser->timeAfterLastLogout,
                pLoginUser->packageVersionInfo,
                24,
                pLoginUser->byteChildPlayTimeInfo,
                pLoginUser->user_level,
                zoneFatigueInfoTotLen,
                zoneFatigueInfoTotLen,
                bufZoneFatigueInfo,
                pLoginUser->lastLogoutTime,
                pLoginUser->activateTime,
                8,
                pLoginUser->activateInformation,
                itemId);
        } else {
            result = SendSocket(
                world.inner_addr,
                "cdsddddcdddsdcdchbddbd",
                0,
                uid,
                pLoginUser->account,
                time_left,
                pLoginUser->loginflag,
                pLoginUser->warnflag,
                stat,
                vmFlag,
                reservations,
                pLoginUser->fatigueRestTime,
                pLoginUser->fatiguePlayTime,
                pLoginUser->loginCafe,
                static_cast<int>(pLoginUser->loginIp.s_addr),
                GetComputationBasis(*pLoginUser),
                pLoginUser->timeAfterLastLogout,
                pLoginUser->packageVersionInfo,
                zoneFatigueInfoTotLen,
                zoneFatigueInfoTotLen,
                bufZoneFatigueInfo,
                pLoginUser->lastLogoutTime,
                pLoginUser->activateTime,
                8,
                pLoginUser->activateInformation,
                itemId);
        }
    }

    if (pSession != nullptr) {
        if (result == 0) {
            // Roll back the early publication introduced by the original
            // behavior bug fix above. Do not alter a replaced/approved account.
            {
                L2_GUARD(m_lock);
                const auto account = usermap.find(uid);
                if (account != usermap.end() && account->second == pLoginUser &&
                    pLoginUser->sid == pSession->GetSessionId() &&
                    pLoginUser->selectedServerid == serverid &&
                    pLoginUser->um_mode != UM_PLAY_OK && pLoginUser->um_mode != UM_IN_GAME) {
                    pLoginUser->selectedServerid = 0;
                }
            }
            error = 8;
            pSession->Send("cc", 6, error);
        }
        // Original behavior bug (L2Authd.exe 0x40F464..0x40F4AB):
        // this success branch published routing only AFTER the world send.
        // else {
        //     error = static_cast<char>(
        //         UpdateSessionId(
        //             uid,
        //             pSession->GetSessionId(),
        //             pLoginUser->md5key,
        //             serverid))
        //     if (error != 0) {
        //         pSession->Send("cc", 6, error)
        //     }
        // }
    }
    return error;
}

// L2Authd.exe 0x0040F4E0, Src/accountdb.cpp:1321-1349.
char AccountDB::CheckUserTime(
    int Uid,
    int* RemainTime) {
    char ErrorCode = 0;
    CDBConn conn(g_linDB);
    authd::platform::OdbcLen cbUid = 0;
    authd::platform::SQLBindParameter(
        conn.m_stmt,
        1,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &Uid,
        0,
        &cbUid);
    authd::platform::OdbcLen cbUserTime = 0;
    authd::platform::SQLBindParameter(
        conn.m_stmt,
        2,
        kOutput,
        kCUInt,
        kCInt,
        0,
        0,
        RemainTime,
        0,
        &cbUserTime);

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "{CALL dbo.ap_GUserTime (?,?) }");
    const authd::platform::OdbcReturn RetCode =
        authd::platform::SQLExecDirect(
            conn.m_stmt,
            buffer,
            kSqlNts);
    if (RetCode == kSqlSuccess) {
        if (*RemainTime < 1) {
            ErrorCode = 0x13;
        }
    } else {
        conn.Error(kStatementHandle, conn.m_stmt, buffer);
        ErrorCode = 1;
    }
    conn.ResetHtmt();
    return ErrorCode;
}

// L2Authd.exe 0x0040F650, Src/accountdb.cpp:1352-1378.
char AccountDB::CheckUserDuration(
    int uid,
    int* remainTime) {
    char ErrorCode = 0;
    CDBConn conn(g_linDB);
    authd::platform::OdbcLen cbUid = 0;
    authd::platform::SQLBindParameter(
        conn.m_stmt,
        1,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &uid,
        0,
        &cbUid);
    authd::platform::OdbcLen cbUserTime = 0;
    authd::platform::SQLBindParameter(
        conn.m_stmt,
        2,
        kOutput,
        kCInt,
        kCInt,
        0,
        0,
        remainTime,
        0,
        &cbUserTime);

    char buffer[256];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "{CALL dbo.ap_GUserDuration (?,?) }");
    const authd::platform::OdbcReturn RetCode =
        authd::platform::SQLExecDirect(
            conn.m_stmt,
            buffer,
            kSqlNts);
    if (RetCode == kSqlSuccess) {
        if (*remainTime < 1) {
            ErrorCode = 0x13;
        }
    } else {
        conn.Error(kStatementHandle, conn.m_stmt, buffer);
        ErrorCode = 1;
    }
    conn.ResetHtmt();
    return ErrorCode;
}

// L2Authd.exe 0x0040F7C0, Src/accountdb.cpp:1202-1222.
char AccountDB::CheckPersonalPayStat(
    CClientSession* pSession,
    LoginUser* pLoginUser,
    int uid) {
    char result = 0;
    if (config.usePBServer) {
        result = PBServerMgr::Inst()->RequestGetValueInfo(
            uid,
            pLoginUser,
            pSession,
            0,
            0);
    } else {
        int remainTime = 0;
        result = ChargePersonalDBValue(
            uid,
            pLoginUser,
            pSession,
            &remainTime);
    }
    if (result != 0 && pSession != nullptr) {
        pSession->SendFail(1, result, pLoginUser);
    }
    return result;
}

// L2Authd.exe 0x0040F870, Src/accountdb.cpp:1225-1318.
char AccountDB::ChargePersonalDBValue(
    int uid,
    LoginUser* pLoginUser,
    CClientSession* pSession,
    int* remainTime) {
    char result = 0;
    const int operationCode =
        (pLoginUser->stat % 1000) / 100;
    if (config.useNoBill) {
        pLoginUser->stat = 2000;
    }
    if (pLoginUser->stat == 0) {
        if (config.FreeServer == 3) {
            pLoginUser->stat = 2000;
        } else if (config.useDualCharging) {
            pLoginUser->stat = 0;
        } else {
            return 0x12;
        }
    }

    if (operationCode == 2 || operationCode == 5) {
        result = UserTimeLogin(
            uid,
            pLoginUser,
            remainTime);
    }
    if (result != 0) {
        if (config.FreeServer != 3) {
            return result;
        }
        result = 0;
        pLoginUser->stat = 2000;
    }

    if (pSession == nullptr) {
        result = RegAccount(uid, pLoginUser);
        if (result == 0) {
            WriteLogD(
                0x321,
                pLoginUser->account,
                pLoginUser->loginIp,
                pLoginUser->stat,
                pLoginUser->age,
                pLoginUser->gender,
                0,
                reporter.m_UserCount,
                uid);
        }
        return result;
    }

    if (pSession->GetSessionId() == UINT32_C(0xffffffff)) {
        return 0x14;
    }
    result = RegAccount(uid, pLoginUser);
    if (result != 0) {
        return result;
    }

    pSession->SetUserMode(UM_LOGIN);
    pSession->Send(
        "cddddddddbd",
        3,
        uid,
        pSession->GetMd5Key(),
        sBillLogTraceId,
        sReconnectKey,
        pLoginUser->stat,
        *remainTime,
        0,
        pLoginUser->warnflag,
        16,
        pLoginUser->forbidden_servers,
        pLoginUser->subscription);
    pSession->SetLastIO(runtime::TickCount32());
    WriteLogD(
        0x321,
        pLoginUser->account,
        pLoginUser->loginIp,
        pLoginUser->stat,
        pLoginUser->age,
        pLoginUser->gender,
        0,
        reporter.m_UserCount,
        uid);
    return result;
}

// L2Authd.exe 0x0040FAA0, Src/accountdb.cpp:2139-2165.
bool AccountDB::LoginAccount(
    CClientSession* pSession,
    char* name,
    char* password,
    int subscription,
    unsigned short cdkind,
    char clientInstances,
    int otpAnswer) {
    PreLoginUser prelogin;
    std::memset(
        &prelogin.bPassFlag,
        0,
        sizeof(PreLoginUser));
    prelogin.cdkind = cdkind;
    prelogin.clientInstances = clientInstances;
    prelogin.subscription = subscription;
    prelogin.otpAnswer = otpAnswer;
    std::strcpy(prelogin.playAccount, name);
    std::strcpy(prelogin.playPassword, password);
    std::strcpy(prelogin.loginAccount, name);
    std::strcpy(prelogin.loginPassword, password);

    if (!config.usePAServer) {
#if defined(AUTHD_USE_POSTGRESQL)
        // The original branch expects PA to fill PreLoginUser::account.
        // Local PostgreSQL login loads the account with AuthenticateUser first,
        // then calls AuthorizeUser. This uses the same account checks as PA login.
        // The legacy ODBC branch below keeps the original direct call.
        if (!AuthenticateUser(
                pSession,
                &prelogin,
                true,
                config.autoCreateAccounts)) {
            return false;
        }
        return AuthorizeUser(pSession, prelogin);
#else
        return AuthorizeUser(pSession, prelogin);
#endif
    }
    const char err_msg = PAServerMgr::Inst()->LoginAccount(
        0,
        pSession,
        nullptr,
        &prelogin);
    if (err_msg == 0) {
        return false;
    }
    pSession->SendFail(1, err_msg, nullptr);
    return true;
}

// L2Authd.exe 0x0040FBD0, Src/accountdb.cpp:2176-2199.
bool AccountDB::LoginAccountWsid(
    CClientSession* pSession,
    WebSessionId& webSessionId,
    WebToken& webToken,
    int subscription,
    unsigned short cdkind,
    char clientInstances) {
    char err_msg = 0x14;
    PreLoginUser prelogin;
    std::memset(
        &prelogin.bPassFlag,
        0,
        sizeof(PreLoginUser));
    prelogin.cdkind = cdkind;
    prelogin.clientInstances = clientInstances;
    prelogin.subscription = subscription;
    prelogin.otpAnswer = -1;
    prelogin.webSessionId = webSessionId;

    if (config.useGpProxy) {
        err_msg = CGpProxyServerMgr::Inst()->LoginAccountWsid(
            ORIGIN_CLIENT_LOGIN,
            pSession,
            nullptr,
            prelogin,
            webToken);
    } else if (config.useWSM) {
        err_msg = WSMServerMgr::Inst()->LoginAccountWsid(
            ORIGIN_CLIENT_LOGIN,
            pSession,
            nullptr,
            &prelogin,
            webToken);
    }
    if (err_msg != 0) {
        pSession->SendFail(1, err_msg, nullptr);
    }
    return err_msg != 0;
}

// L2Authd.exe 0x0040FD00, Src/accountdb.cpp:2870-2901.
bool AccountDB::ExternalTokenLogin(
    CClientSession* pSession,
    int companyNo,
    unsigned char* externalToken,
    int affiliatedGameId,
    int gameId,
    int subscription,
    unsigned short cdkind,
    char clientInstances,
    in_addr clientIp) {
    PreLoginUser prelogin;
    std::memset(
        &prelogin.bPassFlag,
        0,
        sizeof(PreLoginUser));
    prelogin.externalUser.companyNo = companyNo;
    if (IsBase64ExternalToken(
            companyNo,
            config.Country)) {
        std::memcpy(
            prelogin.externalUser.externalToken,
            externalToken,
            0x3ff);
    } else {
        prelogin.externalUser.externalToken[0] = 0;
        std::memcpy(
            prelogin.externalUser.externalToken + 1,
            externalToken,
            0x3ff);
        std::memcpy(
            prelogin.externalUser.externalToken + 0x41,
            &clientIp,
            sizeof(clientIp.s_addr));
    }
    prelogin.externalUser.affiliatedGameId = affiliatedGameId;
    prelogin.externalUser.gameId = gameId;
    prelogin.subscription = subscription;
    prelogin.cdkind = cdkind;
    prelogin.clientInstances = clientInstances;

    const char err_msg =
        PAServerMgr::Inst()->LoginExternalToken(
            0,
            pSession,
            nullptr,
            &prelogin);
    if (err_msg != 0) {
        pSession->SendFail(1, err_msg, nullptr);
    }
    return err_msg != 0;
}

// L2Authd.exe 0x0040FE50, Src/accountdb.cpp:2003-2068.
bool AccountDB::AuthenticateUser(
    CClientSession* pSession,
    PreLoginUser* pPreLogin,
    bool checkPassword,
    bool autoCreate) {
    char err_msg = 0;
    CAccount* const pAccount = &pPreLogin->account;
    char* const name = pPreLogin->playAccount;
    if (checkPassword) {
        err_msg = pAccount->CheckPassword(
            name,
            pPreLogin->playPassword,
            config.newEncrypt,
            autoCreate);
    } else {
        char dbEncFlag[2];
        err_msg = pAccount->LoadPassword(
            name,
            pPreLogin->playPassword,
            dbEncFlag,
            config.newEncrypt);
    }
    if (err_msg != 0) {
        pSession->SendFail(1, err_msg, nullptr);
        return false;
    }

    if (config.Country == 3 &&
        KickAccountByAccount(name, 7, true)) {
        pSession->SendFail(1, 7, nullptr);
        return false;
    }
    err_msg = pAccount->LoadAccount(name);
    if (err_msg != 0) {
        pSession->SendFail(1, err_msg, nullptr);
        return false;
    }

    if (!config.NpSecAuth()) {
        return AuthenticateUserSecondary(
            pSession,
            *pPreLogin);
    }
    CGpProxyServerMgr& gpproxy =
        *CGpProxyServerMgr::Inst();
    if (IsTokenLoginUser(*pPreLogin)) {
        err_msg = gpproxy.ListSecAuthAuthenticatedList(
            ORIGIN_CLIENT_LOGIN,
            pSession,
            *pPreLogin);
    } else {
        err_msg = gpproxy.ListSecAuthServices(
            ORIGIN_CLIENT_LOGIN,
            pSession,
            *pPreLogin);
    }
    if (err_msg != 0) {
        pSession->SendFail(1, err_msg, nullptr);
    }
    return false;
}

// L2Authd.exe 0x0040FFF0, Src/accountdb.cpp:1926-1995.
bool AccountDB::AuthenticateUserPortal(
    CClientSession* pSession,
    PreLoginUser* pPreLogin) {
    enum EPORTAL_BLOCK_CODE : int {
        PORTAL_BLOCK_GAME = 1,
        PORTAL_BLOCK_ENTIRE = 8,
    };
    enum EPORTAL_STATUSCODE : int {
        PORTAL_STATUS_NORMAL = 1,
        PORTAL_STATUS_WAITING_PARENTS_APPROVAL = 2,
        PORTAL_STATUS_WAITING_SECEDE = 3,
        PORTAL_STATUS_LONG_TERM_UNUSED = 7,
    };
    enum EPORTAL_NOTICE_CODE : int {
        PORTAL_NOTICE_PASSWD = 4,
        PORTAL_NOTICE_QUIZ = 8,
    };

    if (pPreLogin->portalUser.statusCode !=
        PORTAL_STATUS_NORMAL) {
        char reason = 2;
        switch (pPreLogin->portalUser.statusCode) {
        case PORTAL_STATUS_WAITING_PARENTS_APPROVAL:
            reason = 0x26;
            break;
        case PORTAL_STATUS_WAITING_SECEDE:
            reason = 0x27;
            break;
        case PORTAL_STATUS_LONG_TERM_UNUSED:
            reason = 0x4d;
            break;
        default:
            break;
        }
        pSession->SendFail(1, reason, nullptr);
        return false;
    }

    if ((pPreLogin->portalUser.blockCode &
         (PORTAL_BLOCK_GAME | PORTAL_BLOCK_ENTIRE)) != 0) {
        pSession->Send(
            "cdd",
            2,
            0,
            pPreLogin->portalUser.blockReasonCode);
        return false;
    }
    if (pPreLogin->portalUser.noticeCode != 0) {
        char reason = 0;
        if ((pPreLogin->portalUser.noticeCode &
             PORTAL_NOTICE_PASSWD) != 0) {
            reason = 0x11;
        } else if ((pPreLogin->portalUser.noticeCode &
                    PORTAL_NOTICE_QUIZ) != 0) {
            reason = 0x29;
        }
        if (reason != 0) {
            pSession->SendFail(1, reason, nullptr);
            return false;
        }
    }

    GetProfileFromPortalUserData(
        pPreLogin->account,
        pPreLogin->portalUser);
    return AuthenticateUser(
        pSession,
        pPreLogin,
        false);
}

// L2Authd.exe 0x004101E0, Src/accountdb.cpp:2074-2127.
bool AccountDB::AuthenticateUserSecondary(
    CClientSession* session,
    PreLoginUser& plu) {
    const SecurityServiceFlag& sf =
        plu.account.securityFlag;
    const SecurityUsage& su =
        plu.account.securityUsage;
    if (config.ForceSkipSecAuthIfDevicePassed &&
        su.IsPassed(SecAuth::DEVICEAUTH)) {
        return true;
    }
    if (!config.FidoDisabled &&
        IsTokenLoginUser(plu) &&
        sf.IsActivated(SecAuth::FIDO)) {
        if (!su.IsPassed(SecAuth::FIDO)) {
            session->SendFail(1, 0x14, nullptr);
            return false;
        }
        if (config.ForceSkipSecAuthIfFIDOPassed) {
            return true;
        }
    }
    if (config.deprecateSecurityCard &&
        sf.IsActivated(SecAuth::SECURITYCARD)) {
        session->SendFail(1, 0x50, nullptr);
        return false;
    }

    bool doNext = AuthenticateOtp(session, plu);
    if (!doNext) {
        return false;
    }
    if (config.ForceSkipSecAuthIfOTPPassed &&
        su.IsPassed(SecAuth::GOOGLEOTP)) {
        return true;
    }
    doNext = AuthenticateSecurityCard(session, plu);
    return doNext;
}

// L2Authd.exe 0x00410360, Src/accountdb.cpp:2206-2240.
bool AccountDB::AuthorizeUser(
    CClientSession* pSession,
    PreLoginUser& prelogin) {
    if (config.useIovationProxy &&
        !prelogin.iovationChecked) {
        pSession->SetPreLoginUser(prelogin);
        pSession->Send(
            "cd",
            0x13,
            prelogin.account.uid);
        return false;
    }

    if (config.useGradeServer) {
        const char errCode =
            GradeServerMgr::Inst()->RequestGrade(
                pSession,
                prelogin,
                0);
        if (errCode == 0) {
            return false;
        }
        if (config.useLevelCheck) {
            pSession->SendFail(1, errCode, nullptr);
            return false;
        }
        prelogin.account.user_level = 0;
    }
    return AuthorizeClientSideLogin(
        pSession,
        prelogin);
}

// L2Authd.exe 0x00410440, Src/accountdb.cpp:2247-2397.
bool AccountDB::AuthorizeClientSideLogin(
    CClientSession* pSession,
    PreLoginUser& prelogin) {
    CAccount* const pAccount = &prelogin.account;
    if (!pAccount->VerifyUnderAgeTimeLimit(
            prelogin.portalUser.uid)) {
        pSession->SendFail(1, 0x20, nullptr);
        return false;
    }

    // Noise: routine login sub-step success is extra with the login outcome and is not a packet.
    // const char* Buf3 = "AgeCheck OK"
    // WritePacketLog(
    //     "Auth<->AgeCheck",
    //     static_cast<unsigned int>(prelogin.portalUser.uid),
    //     reinterpret_cast<const unsigned char*>(Buf3),
    //     static_cast<int>(std::strlen(Buf3)),
    //     0x400)

    const char flagType = pAccount->CheckBlockStatus();
    if (flagType == 1) {
        char blockMsg[4096];
        std::memset(blockMsg, 0, sizeof(blockMsg));
        const int len = pAccount->MakeBlockInfo(blockMsg + 3);
        const int len2 = len + config.PacketSizeType;
        blockMsg[0] = static_cast<char>(len2);
        blockMsg[1] = static_cast<char>(len2 >> 8);
        blockMsg[2] = 9;
        pSession->Send(blockMsg, len + 3);
        return false;
    }
    if (flagType == 2) {
        pSession->Send(
            "cdd",
            2,
            pAccount->block_flag2,
            0);
        return false;
    }

    if (!pAccount->CheckAge()) {
        pSession->SendFail(1, 0x0c, nullptr);
        return false;
    }
    // Noise: routine login sub-step success is extra with the login outcome and is not a packet.
    // const char* Buf4 = "CheckAge OK"
    // WritePacketLog(
    //     "Auth<->CheckAge",
    //     static_cast<unsigned int>(prelogin.portalUser.uid),
    //     reinterpret_cast<const unsigned char*>(Buf4),
    //     static_cast<int>(std::strlen(Buf4)),
    //     0x400)

    const in_addr loginaddr = pSession->getaddr();
    if (!pAccount->CheckGMUser(loginaddr)) {
        char addrbuff[16];
        std::strncpy(
            addrbuff,
            ::inet_ntoa(loginaddr),
            sizeof(addrbuff));
        CDBConn conn(g_linDB);
        conn.BindParam(&pAccount->uid, 1);
        conn.BindParam(addrbuff, 16, 1);
        const bool bRet = conn.Execute(
            "{call ap_SetIllegalLoginTrace (?, ?, 4)}");

        // Original text-in-hex diagnostic kept. Report the actual rejection and DB trace result.
        // char* const ErrorMsg =
        //     const_cast<char*>("CheckGMUser Err")
        // char Buf8[512]
        // std::memset(Buf8, 0, sizeof(Buf8))
        // std::sprintf(
        //     Buf8,
        //     "IP:%s DbRet:%d %s",
        //     addrbuff,
        //     bRet,
        //     ErrorMsg)
        // WritePacketLog(
        //     "Auth<->CheckGMUserErr",
        //     static_cast<unsigned int>(prelogin.portalUser.uid),
        //     reinterpret_cast<const unsigned char*>(Buf8),
        //     static_cast<int>(std::strlen(Buf8)),
        //     0x400)
        // Log.AddLog(LOG_WARN,
        //     "component=auth event=login_rejected reason=gm_ip_not_allowed uid=%d portal_uid=%d peer=\"%s\" trace_query_ok=%s",
        //     pAccount->uid, prelogin.portalUser.uid, addrbuff, bRet ? "true" : "false")
        // Log.AddLog(LOG_WARN,
        //     "[login] GM login rejected for account %d (portal account %d): IP \"%s\" is not allowed. Illegal-login trace query %s.",
        //     pAccount->uid, prelogin.portalUser.uid, addrbuff, bRet ? "executed" : "failed")
        L2_LOG_WARN(Log,
            "[login] GM login rejected for account %d (portal account %d): IP \"%s\" is not allowed. Illegal-login trace query %s.",
            pAccount->uid, prelogin.portalUser.uid, addrbuff, bRet ? "executed" : "failed");
        pSession->SendFail(1, 0x14, nullptr);
        return false;
    }

    // Noise: routine login sub-step success is extra with the login outcome and is not a packet.
    // const char* Buf5 = "CheckGMUser OK"
    // WritePacketLog(
    //     "Auth<->CheckGMUser",
    //     static_cast<unsigned int>(prelogin.portalUser.uid),
    //     reinterpret_cast<const unsigned char*>(Buf5),
    //     static_cast<int>(std::strlen(Buf5)),
    //     0x400)
    pAccount->SetPremiumLevel(prelogin.nUserLevel);
    if (!pAccount->CheckFatigueData()) {
        pSession->SendFail(1, 0x2a, nullptr);
        return false;
    }
    pAccount->SetFreeServer();
    if (FindAccount(pAccount->uid)) {
        KickAccount(
            pAccount->uid,
            7,
            true,
            true);
        pSession->SendFail(1, 7, nullptr);
        return false;
    }

    // Noise: routine login sub-step success is extra with the login outcome and is not a packet.
    // const char* Buf6 = "SetFreeServer OK"
    // WritePacketLog(
    //     "Auth<->FindAccount",
    //     static_cast<unsigned int>(pAccount->uid),
    //     reinterpret_cast<const unsigned char*>(Buf6),
    //     static_cast<int>(std::strlen(Buf6)),
    //     0x400)

    LoginUser* const pLoginUser = LoginUser::Alloc();
    InitLoginUserData(
        pLoginUser,
        prelogin,
        *pAccount,
        pSession->getaddr(),
        pSession->GetMd5Key(),
        pSession->GetSessionId());
    pSession->SetUid(pAccount->uid);

    // Noise: routine login sub-step success is extra with the login outcome and is not a packet.
    // const char* Buf7 = "InitLoginUserData OK"
    // WritePacketLog(
    //     "Auth<->InitLoginUserData",
    //     static_cast<unsigned int>(pAccount->uid),
    //     reinterpret_cast<const unsigned char*>(Buf7),
    //     static_cast<int>(std::strlen(Buf7)),
    //     0x400)

    if (config.UseIPServer) {
        const int result = ipsessionDB.RequestIPKind(
            pLoginUser,
            pAccount->uid,
            0);
        if (result == 0) {
            return false;
        }
        if (result == 0x0b) {
            pSession->SendFail(1, 7, pLoginUser);
            return false;
        }
        if (config.useNoBill && !config.usePBServer) {
            pSession->SendFail(1, 0x14, pLoginUser);
            // Log.AddLog(
            //     LOG_ERROR,
            //     "AccountDB::AuthorizeClientSideLogin RequestIPKind failed.")
            // L2_LOG_ERROR(Log,
                // "AccountDB::AuthorizeClientSideLogin RequestIPKind failed.")
            L2_LOG_ERROR(Log,
                "[login] Account %u rejected after IP-kind lookup returned %d: "
                "useNoBill is enabled and usePBServer is disabled. Requested login failure response 0x14.",
                static_cast<unsigned int>(pAccount->uid), result);
            return false;
        }
    }

    // Noise: routine login sub-step success is extra with the login outcome and is not a packet.
    // const char* Buf = "UseIPServer OK"
    // WritePacketLog(
    //     "Auth<->IPServer off",
    //     static_cast<unsigned int>(pAccount->uid),
    //     reinterpret_cast<const unsigned char*>(Buf),
    //     static_cast<int>(std::strlen(Buf)),
    //     0x400)
    CheckPersonalPayStat(
        pSession,
        pLoginUser,
        pAccount->uid);
    return false;
}

// L2Authd.exe 0x00410A80, Src/accountdb.cpp:2405-2436.
char AccountDB::AuthorizeServerSideLogin(
    PreLoginUser& prelogin,
    in_addr clientIp,
    LoginUser* pLoginUser) {
    CAccount* const pAccount = &prelogin.account;
    if (!pAccount->VerifyUnderAgeTimeLimit(
            prelogin.portalUser.uid)) {
        return 0x20;
    }
    if (pAccount->CheckBlockStatus() != 0) {
        return 2;
    }
    if (!pAccount->CheckAge()) {
        return 0x0c;
    }
    pAccount->SetPremiumLevel(prelogin.nUserLevel);
    if (!pAccount->CheckFatigueData()) {
        return 0x2a;
    }
    pAccount->SetFreeServer();
    if (FindAccount(prelogin.account.uid)) {
        return 2;
    }
    InitLoginUserData(
        pLoginUser,
        prelogin,
        *pAccount,
        clientIp,
        -1,
        0);
    return 0;
}

// L2Authd.exe 0x00410B50, Src/accountdb.cpp:2555-2729.
char AccountDB::LoginServerSide(
    int uid,
    char* allianceUserKey,
    PreLoginUser& preLogin,
    int serverid,
    int stat,
    in_addr clientIp) {
    char result = 0x14;
    GetProfileFromPortalUserData(
        preLogin.account,
        preLogin.portalUser);
    LoginUser* const pLoginUser = LoginUser::Alloc();
    result = AuthorizeServerSideLogin(
        preLogin,
        clientIp,
        pLoginUser);
    if (result != 0) {
        pLoginUser->ReleaseRef();
        return result;
    }

    pLoginUser->stat = stat;
    pLoginUser->serverid = static_cast<char>(serverid);
    if (allianceUserKey != nullptr) {
        std::memcpy(
            pLoginUser->ptid,
            allianceUserKey,
            0x20);
    }
    if (config.PBType == 2) {
        const WorldServer world =
            g_ServerList.GetAt(serverid);
        if (world.serverKind != 0x101 &&
            world.serverKind != 0x104) {
            pLoginUser->originChargedServerId =
                static_cast<char>(serverid);
        }
    } else {
        pLoginUser->originChargedServerId =
            static_cast<char>(serverid);
    }

    const bool bFreeStatus =
        (config.FreeServer == 3 && stat == 2000) ||
        (config.FreeServer == 2 && stat == 1002);
    if (!bFreeStatus) {
        if (config.UseIPServer) {
            const char ipResult = ipsessionDB.RequestIPKind(
                pLoginUser,
                uid,
                0x1c);
            if (ipResult == 0) {
                return 0;
            }
            if (ipResult == 0x0b) {
                pLoginUser->ReleaseRef();
                return 7;
            }
        }
        if (config.usePBServer) {
            const char pbResult =
                PBServerMgr::Inst()->RequestGetValueInfo(
                    uid,
                    pLoginUser,
                    nullptr,
                    0x1c,
                    pLoginUser->originChargedServerId);
            if (pbResult != 0) {
                pLoginUser->ReleaseRef();
                return pbResult;
            }
            return 0;
        }
        if (preLogin.account.pay_stat != stat) {
            pLoginUser->ReleaseRef();
            return 0x2c;
        }
    }

    int remainTime = 0;
    pLoginUser->logintime =
        runtime::crt::Time32Now();
    result = ChargePersonalDBValue(
        uid,
        pLoginUser,
        nullptr,
        &remainTime);
    if (result != 0) {
        pLoginUser->ReleaseRef();
        return result;
    }
    if (!recordGamePlayTime(
            uid,
            pLoginUser->serverid)) {
        return 0x14;
    }

    int fatigueRestTime = 0;
    int fatiguePlayTime = 0;
    int fatigueSecSinceLastLogout = 0;
    if (config.useFatigueSystem &&
        AccountContainer::Inst()->IsFatigueCustomer(
            pLoginUser->fcid,
            pLoginUser->age,
            pLoginUser->auth_flag)) {
        AccountContainer::Inst()->GetAccountTime(
            preLogin.account.fcid,
            &fatigueRestTime,
            &fatiguePlayTime);
        AccountContainer::Inst()->GetLastLogoutTime(
            preLogin.account.fcid,
            &fatigueSecSinceLastLogout);
    }

    char bufZoneFatigueInfo[1553];
    bufZoneFatigueInfo[0] = 0;
    std::memset(
        bufZoneFatigueInfo + 1,
        0,
        sizeof(bufZoneFatigueInfo) - 1);
    int zoneFatigueInfoTotLen = 0;
    [[maybe_unused]] unsigned char zoneFatigueAvailCount = 0;
    const WorldServer world =
        g_ServerList.GetAt(serverid);
    if (config.useExtendPacket) {
        if (config.useGradeServer) {
            pLoginUser->user_level = 0;
        }
        SendSocket(
            world.inner_addr,
            "cdcddddddchbbh",
            0x15,
            uid,
            0,
            remainTime,
            preLogin.account.login_flag,
            0,
            fatigueRestTime,
            fatiguePlayTime,
            fatigueSecSinceLastLogout,
            preLogin.account.packageVersionInfo,
            zoneFatigueInfoTotLen,
            zoneFatigueInfoTotLen,
            bufZoneFatigueInfo,
            24,
            pLoginUser->byteChildPlayTimeInfo,
            pLoginUser->user_level);
    } else {
        SendSocket(
            world.inner_addr,
            "cdcddddddchb",
            0x15,
            uid,
            0,
            remainTime,
            preLogin.account.login_flag,
            0,
            fatigueRestTime,
            fatiguePlayTime,
            fatigueSecSinceLastLogout,
            preLogin.account.packageVersionInfo,
            zoneFatigueInfoTotLen,
            zoneFatigueInfoTotLen,
            bufZoneFatigueInfo);
    }
    return 0;
}

// L2Authd.exe 0x004110E0, Src/accountdb.cpp:3355-3403.
void AccountDB::ReConnectPBServer() {
    PBServerMgr* const pbServer = PBServerMgr::Inst();
    if (pbServer->mPBServerReConnect) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "PBServer Reconnect process START")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=billing event=resync_started peer=pb scope=in_game_accounts")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[PBS] Starting billing resynchronization for accounts in the game.")
        L2_LOG_INFO(Log,
            "[PBS] Starting billing resynchronization for accounts in the game.");
        {
            L2_GUARD(m_lock);
            for (const auto& account : usermap) {
                if (account.second->um_mode == UM_IN_GAME) {
                    reconnUserMap.insert(account);
                }
            }
        }
        PBServerMgr::Inst()->mPBServerReConnect = false;
    }

    char buffer[0x2000];
    std::memset(buffer, 0, sizeof(buffer));
    char* const pBuffer = buffer;
    int len = 0;
    int idx = 0;
    {
        L2_GUARD(m_reconnLock);
        auto it = reconnUserMap.begin();
        while (it != reconnUserMap.end()) {
            if (idx + 1 == 500) {
                break;
            }
            LoginUser* const user = it->second;
            len += Assemble(
                pBuffer + len,
                0x2000,
                "Ddd",
                static_cast<std::int64_t>(it->first),
                user->originChargedServerId,
                static_cast<int>(user->loginIp.s_addr));
            it = reconnUserMap.erase(it);
            ++idx;
        }
    }

    if (idx == 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=resync_queue_empty peer=pb action=return")
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Billing resynchronization queue is empty.")
        L2_LOG_INFO(Log,
            "[PBS] Billing resynchronization queue is empty.");
    }
    if (idx != 0 &&
        PBServerMgr::Inst()->RequestStartChargeList(
            idx,
            len,
            pBuffer) != 0) {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "RequestStartChargeList Failed.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=resync_batch_failed peer=pb accounts=%d payload_bytes=%d action=return batch_retry=not_queued",
        //     idx, len)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Could not submit the billing resynchronization batch: %d accounts, %d bytes. This batch is not queued for retry.",
        //     idx, len)
        L2_LOG_WARN(Log,
            "[PBS] Could not submit the billing resynchronization batch: %d accounts, %d bytes. This batch is not queued for retry.",
            idx, len);
    }
}

// L2Authd.exe 0x004113A0, Src/accountdb.cpp:3418-3442.
bool AccountDB::ThirdPartyTokenLogin(
    CClientSession* pSession,
    int companyNo,
    unsigned char* externalToken,
    int affiliatedGameId,
    int gameId,
    int subscription,
    unsigned short cdkind,
    char clientInstances,
    in_addr clientIp) {
    char err_msg = 0x14;
    PreLoginUser prelogin;
    std::memset(
        &prelogin.bPassFlag,
        0,
        sizeof(PreLoginUser));
    prelogin.externalUser.companyNo = companyNo;
    prelogin.externalUser.externalToken[0] = 0;
    std::memcpy(
        prelogin.externalUser.externalToken + 1,
        externalToken,
        0x3ff);
    std::memcpy(
        prelogin.externalUser.externalToken + 0x286,
        &clientIp,
        sizeof(clientIp.s_addr));
    prelogin.externalUser.affiliatedGameId = affiliatedGameId;
    prelogin.externalUser.gameId = gameId;
    prelogin.subscription = subscription;
    prelogin.cdkind = cdkind;
    prelogin.clientInstances = clientInstances;

    err_msg = PAServerMgr::Inst()->LoginExternalToken(
        0,
        pSession,
        nullptr,
        &prelogin);
    if (err_msg != 0) {
        pSession->SendFail(1, err_msg, nullptr);
    }
    return err_msg != 0;
}

// L2Authd.exe 0x004114B0, Src/accountdb.cpp:3871-3907.
bool AccountDB::LoginToken(
    CClientSession* pSession,
    char* authProviderCode,
    char* token,
    int affiliatedGameId,
    int gameId,
    int subscription,
    unsigned short cdkind,
    char clientInstances,
    in_addr clientIp) {
    const int companyNo = GetCompanyNo(authProviderCode);
    if (IsExternalLoginCompanyNo(companyNo)) {
        unsigned char externalToken[1024];
        externalToken[0] = 0;
        std::memset(
            externalToken + 1,
            0,
            sizeof(externalToken) - 1);
        std::memcpy(
            externalToken,
            token,
            std::strlen(token) + 1);
        return ExternalTokenLogin(
            pSession,
            companyNo,
            externalToken,
            affiliatedGameId,
            gameId,
            subscription,
            cdkind,
            clientInstances,
            clientIp);
    }

    char err_msg = 0x14;
    PreLoginUser preLogin;
    std::memset(
        &preLogin.bPassFlag,
        0,
        sizeof(PreLoginUser));
    std::strncpy(
        preLogin.tokenInfo.authProviderCode,
        authProviderCode,
        sizeof(preLogin.tokenInfo.authProviderCode));
    preLogin.tokenInfo.authProviderCode[
        sizeof(preLogin.tokenInfo.authProviderCode) - 1] = 0;
    std::strncpy(
        preLogin.tokenInfo.token,
        token,
        sizeof(preLogin.tokenInfo.token));
    preLogin.tokenInfo.token[
        sizeof(preLogin.tokenInfo.token) - 1] = 0;
    preLogin.subscription = subscription;
    preLogin.cdkind = cdkind;
    preLogin.clientInstances = clientInstances;
    preLogin.otpAnswer = -1;
    err_msg = PAServerMgr::Inst()->LoginToken(
        0,
        pSession,
        nullptr,
        &preLogin,
        clientIp);
    return err_msg != 0;
}

// L2Authd.exe 0x00411640, Src/accountdb.cpp:19-20.
AccountDB::AccountDB()
    : m_lock(eCriticalSection, 0),
      m_preLock(eCriticalSection, 0),
      m_reconnLock(eCriticalSection, 0) {}

// L2Authd.exe 0x004116F0, Src/accountdb.cpp:23-24.
AccountDB::~AccountDB() = default;

// L2Authd.exe 0x00411C00, Src/accountdb.cpp:191-287.
char AccountDB::RegAccount(int uid, LoginUser* user) {
    if (user->chargeServerId == 2 &&
        config.FreeServer != 3) {
        g_ServerList.MapForbiddenServers(
            user->pbServer.availableServers,
            user->pbServer.availableServersSize,
            user->forbidden_servers,
            16);
        g_ServerList.MapForbiddenServers(
            user->age,
            user->forbidden_servers,
            16);
    }

    user->AddRef();
    bool inserted = false;
    {
        L2_GUARD(m_lock);
        inserted =
            usermap.insert({uid, user}).second;
    }

    if (!inserted) {
        user->ReleaseRef();
        KickAccount(uid, 7, true, true);
        return 7;
    }

    if (config.useFatigueSystem &&
        AccountContainer::Inst()->IsFatigueCustomer(
            user->fcid,
            user->age,
            user->auth_flag)) {
        AccountContainer::Inst()->InsertFatigueAccount(
            user->fcid,
            uid);
    }

    if (config.useUserUseLog) {
        const std::time_t currentTime = std::time(nullptr);
        // const std::tm current = *std::localtime(&currentTime)
        const std::tm current = runtime::crt::LocalTimeOrZero(currentTime);
        const std::time_t loginTime =
            static_cast<std::time_t>(user->logintime);
        // const std::tm login = *std::localtime(&loginTime)
        const std::tm login = runtime::crt::LocalTimeOrZero(loginTime);
        UserUseLog.AddLog(
            LOG_NORMAL,
            "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,LOGIN,%s,,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
            current.tm_year + 1900,
            current.tm_mon + 1,
            current.tm_mday,
            current.tm_hour,
            current.tm_min,
            current.tm_sec,
            login.tm_year + 1900,
            login.tm_mon + 1,
            login.tm_mday,
            login.tm_hour,
            login.tm_min,
            login.tm_sec,
            user->account,
            user->serverid,
            user->stat,
            ::inet_ntoa(user->loginIp),
            user->loginCafe,
            uid,
            user->stat != 0,
            user->gender,
            user->age);
    }

    if (config.useDualLog) {
        const std::time_t currentTime = std::time(nullptr);
        // const std::tm current = *std::localtime(&currentTime)
        const std::tm current = runtime::crt::LocalTimeOrZero(currentTime);
        const std::time_t loginTime =
            static_cast<std::time_t>(user->logintime);
        // const std::tm login = *std::localtime(&loginTime)
        const std::tm login = runtime::crt::LocalTimeOrZero(loginTime);
        dualLog.AddLog(
            LOG_NORMAL,
            "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,LOGIN,%s,,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
            current.tm_year + 1900,
            current.tm_mon + 1,
            current.tm_mday,
            current.tm_hour,
            current.tm_min,
            current.tm_sec,
            login.tm_year + 1900,
            login.tm_mon + 1,
            login.tm_mday,
            login.tm_hour,
            login.tm_min,
            login.tm_sec,
            user->account,
            user->serverid,
            user->stat,
            ::inet_ntoa(user->loginIp),
            user->loginCafe,
            uid,
            user->stat != 0,
            user->gender,
            user->age);
    }

    return 0;
}

// L2Authd.exe 0x00411790, Src/accountdb.cpp:314-336.
bool AccountDB::KickAccount(
    int uid,
    char reason,
    bool sendToClient,
    [[maybe_unused]] bool removeAccount) {
    LoginUser* user = nullptr;
    if (!FindAccount(uid, &user)) {
        return false;
    }

    if (user->um_mode == UM_IN_GAME ||
        user->um_mode == UM_PLAY_OK) {
        SendKickMessage(
            1,
            uid,
            static_cast<int>(user->serverid),
            user->sid,
            user->account,
            reason);
    } else if (user->sid != UINT32_C(0xffffffff) &&
               sendToClient) {
        SendKickMessage(
            0,
            uid,
            static_cast<int>(user->serverid),
            user->sid,
            user->account,
            reason);
    }

    user->ReleaseRef();
    LogoutProcess(uid, 0);
    // Log.AddLog(
    //     LOG_DEBUG,
    //     "account (%d) has been kicked in AccountDB::KickAccount()",
    //     uid)
    // L2_LOG_DEBUG(Log,
    //     "account (%d) has been kicked in AccountDB::KickAccount()",
    //     uid)
    // L2_LOG_DEBUG(Log,
    //     "[sessions] Completed local kick processing for account %d.",
    //     uid)
    L2_LOG_DEBUG(Log,
        "[sessions] Invoked local logout processing after the kick path found account %d.",
        uid);
    return true;
}

// L2Authd.exe 0x00411870, Src/accountdb.cpp:291-305.
bool AccountDB::KickAccountByAccount(
    const char* account,
    char reason,
    bool sendToClient) {
    int uid = 0;
    CDBConn connection(g_linDB);
    connection.BindParam(
        const_cast<char*>(account),
        15,
        1);
    connection.Bind(&uid);
    connection.Execute("{call ap_GetGameAccountNo (?)}");

    bool noMoreRows = true;
    if (!connection.Fetch(noMoreRows)) {
        return false;
    }
    return KickAccount(
        uid,
        reason,
        sendToClient,
        true);
}

// L2Authd.exe 0x004119A0, Src/accountdb.cpp:339-396.
int AccountDB::BanAccount(
    int uid,
    short restrictionKind,
    char* reason,
    unsigned int reasonCapacity,
    int* extraResult) {
    int procedureResult = 0;
    CDBConn connection(g_linDB);
    connection.ResetHtmt();

    authd::platform::OdbcLen resultIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        1,
        kOutput,
        kCInt,
        kCInt,
        0,
        0,
        &procedureResult,
        0,
        &resultIndicator);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &uid,
        0,
        &uidIndicator);
    authd::platform::OdbcLen kindIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        3,
        kInput,
        kCShort,
        kCShort,
        0,
        0,
        &restrictionKind,
        0,
        &kindIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        4,
        kOutput,
        kCChar,
        kSqlVarchar,
        reasonCapacity,
        0,
        reason,
        reasonCapacity,
        nullptr);
    authd::platform::OdbcLen extraIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        5,
        kOutput,
        kCInt,
        kCInt,
        0,
        0,
        extraResult,
        0,
        &extraIndicator);

    char statement[256];
    std::strcpy(
        statement,
        "{? = CALL dbo.ap_SetAccountRestirction (?,?,?,?) }");
    int result = 0;
    if (authd::platform::SQLExecDirect(
            connection.m_stmt,
            statement,
            kSqlNts) == kSqlSuccess) {
        switch (procedureResult) {
        case 0:
        case 2:
            result = 0;
            break;
        case 1:
            result = 2;
            break;
        case 3:
            result = 1;
            break;
        default:
            result = 0x14;
            break;
        }
    } else {
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement);
        result = 0x14;
    }
    connection.ResetHtmt();
    return result;
}

// L2Authd.exe 0x00411FC0, Src/accountdb.cpp:902-926.
int AccountDB::checkInGame(int uid, int md5Key) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return 10;
        }
        if (account->second->um_mode == UM_IN_GAME) {
            return 7;
        }
        if (account->second->md5key == md5Key) {
            return 0;
        }
    }
    return 9;
}

// L2Authd.exe 0x00412080, Src/accountdb.cpp:588-613.
bool AccountDB::logoutAccount(int uid, int md5Key) {
    bool matched = false;
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end() &&
            account->second->md5key == md5Key) {
            matched = true;
        }
    }

    if (matched) {
        LogoutProcess(uid, 0);
    }
    return matched;
}

// L2Authd.exe 0x00412130, Src/accountdb.cpp:617-761.
bool AccountDB::recordGamePlayTime(
    int uid,
    int serverId) {
    bool recorded = false;
    void* timer = nullptr;
    LoginUser snapshot;
    const std::int32_t playStartTime =
        runtime::crt::Time32Now();

    if (config.useDualCharging) {
        {
            L2_GUARD(m_lock);
            const auto account = usermap.find(uid);
            if (account != usermap.end()) {
                LoginUser* const user = account->second;
                user->serverid = static_cast<char>(serverId);
                if (config.PBType == 2) {
                    const WorldServer world =
                        g_ServerList.GetAt(serverId);
                    if (world.serverKind != 0x101 &&
                        world.serverKind != 0x104) {
                        user->originChargedServerId =
                            static_cast<char>(serverId);
                    }
                } else {
                    user->originChargedServerId =
                        static_cast<char>(serverId);
                }
                user->um_mode = UM_IN_GAME;
                timer = user->timerHandle;
                user->timerHandle = nullptr;
                user->selectedServerid = 0;
                user->gamePlayed = true;
                user->playStartTime = playStartTime;
                std::memcpy(&snapshot, user, sizeof(snapshot));
                recorded = true;
            } else {
                // A late world notification must not bill a logged-out account.
                return false;
            }
        }
    } else {
        {
            L2_GUARD(m_lock);
            const auto account = usermap.find(uid);
            if (account != usermap.end()) {
                LoginUser* const user = account->second;
                user->billStartTime =
                    runtime::crt::Time32Now();
                user->serverid = static_cast<char>(serverId);
                if (config.PBType == 2) {
                    const WorldServer world =
                        g_ServerList.GetAt(serverId);
                    if (world.serverKind != 0x101 &&
                        world.serverKind != 0x104) {
                        user->originChargedServerId =
                            static_cast<char>(serverId);
                    }
                } else {
                    user->originChargedServerId =
                        static_cast<char>(serverId);
                }
                user->um_mode = UM_IN_GAME;
                user->chargeflag = true;
                timer = user->timerHandle;
                user->timerHandle = nullptr;
                user->selectedServerid = 0;
                user->gamePlayed = true;
                user->playStartTime = playStartTime;
                std::memcpy(&snapshot, user, sizeof(snapshot));
                recorded = true;
            } else {
                return false;
            }
        }

        if (CIPSessionDB::IsIPCharging(snapshot.stat) &&
            recorded) {
            if (ipsessionDB.ConfirmIPCharge(
                    static_cast<unsigned int>(uid),
                    snapshot.loginIp.s_addr,
                    snapshot.stat,
                    static_cast<char>(serverId)) != 0) {
                recorded = false;
            }
        } else if (config.usePBServer &&
                   snapshot.chargeServerId == 2) {
            if (PBServerMgr::Inst()->RequestStartCharge(
                    uid,
                    snapshot.originChargedServerId,
                    snapshot.loginIp) != 0) {
                recorded = false;
            }
        }

        if (config.useExternalFatigueSystem &&
            config.externalFatigueCompanyCode != 1) {
            fatigueMgr.FatigueLogin(uid, &snapshot);
        }
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            timer);
    }
    if (recorded) {
        if (config.useDualLog) {
            const std::time_t rawLoginTime =
                static_cast<std::time_t>(
                    snapshot.logintime);
            // const std::tm login = *std::localtime(&rawLoginTime)
            const std::tm login = runtime::crt::LocalTimeOrZero(rawLoginTime);
            const std::time_t rawPlayStartTime =
                static_cast<std::time_t>(
                    playStartTime);
            // const std::tm current = *std::localtime(&rawPlayStartTime)
            const std::tm current = runtime::crt::LocalTimeOrZero(rawPlayStartTime);
            dualLog.AddLog(
                LOG_NORMAL,
                "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,SERVERIN,%s,,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
                current.tm_year + 1900,
                current.tm_mon + 1,
                current.tm_mday,
                current.tm_hour,
                current.tm_min,
                current.tm_sec,
                login.tm_year + 1900,
                login.tm_mon + 1,
                login.tm_mday,
                login.tm_hour,
                login.tm_min,
                login.tm_sec,
                snapshot.account,
                snapshot.serverid,
                snapshot.stat,
                ::inet_ntoa(snapshot.loginIp),
                snapshot.loginCafe,
                uid,
                snapshot.stat != 0,
                snapshot.gender,
                snapshot.age);
        }

        const int inGameUser =
            runtime::raw_atomic::IncrementI32(
                &reporter.m_InGameUser);
        WriteLogD(
            0x322,
            snapshot.account,
            snapshot.loginIp,
            snapshot.stat,
            snapshot.age,
            snapshot.gender,
            0,
            inGameUser,
            uid);
    }
    if (config.limitIPNumforEachGS > 0 &&
        recorded) {
        ConnectedIPList::Inst()->AddConnectedIp(
            snapshot.serverid,
            snapshot.loginIp,
            uid,
            snapshot.logintime);
    }
    return recorded;
}

// L2Authd.exe 0x00412790, Src/accountdb.cpp:764-817.
bool AccountDB::StartCharge(
    CSocketServer* socket,
    int uid,
    LoginUser& user,
    ChargeType type) {
    if (user.chargeflag) {
        socket->Send(
            "cddd",
            44,
            uid,
            99,
            0);
        // Log.AddLog(
        //     LOG_WARN,
        //     "Duplicated Start Charge(uid:%d)",
        //     uid)
        // L2_LOG_WARN(Log,
            // "Duplicated Start Charge(uid:%d)",
            // uid)
        L2_LOG_WARN(Log,
            "[billing] Start requested for account %d with charging already active. "
            "Requested response 99; continuing billing-start processing.",
            uid);
    }

    user.billStartTime =
        runtime::crt::Time32Now();
    user.chargeflag = true;

    if (type == CHARGE_TYPE_PCCAFE &&
        !CIPSessionDB::IsIPCharging(user.stat)) {
        socket->Send(
            "cddd",
            44,
            uid,
            99,
            1);
        return true;
    }

    if (CIPSessionDB::IsIPCharging(user.stat)) {
        if (ipsessionDB.ConfirmIPCharge(
                static_cast<unsigned int>(uid),
                user.loginIp.s_addr,
                user.stat,
                user.serverid) == 0) {
            socket->Send(
                "cddd",
                44,
                uid,
                0,
                1);
            return true;
        }
        socket->Send(
            "cddd",
            44,
            uid,
            20,
            1);
        return false;
    }

    if (!config.usePBServer ||
        user.chargeServerId != 2) {
        socket->Send(
            "cddd",
            44,
            uid,
            99,
            0);
        if (config.useExternalFatigueSystem &&
            config.externalFatigueCompanyCode != 1) {
            fatigueMgr.FatigueLogin(uid, &user);
        }
        return true;
    }

    if (PBServerMgr::Inst()->RequestStartCharge(
            uid,
            user.originChargedServerId,
            user.loginIp) == 0) {
        socket->Send(
            "cddd",
            44,
            uid,
            0,
            2);
        return true;
    }
    socket->Send(
        "cddd",
        44,
        uid,
        20,
        2);
    return false;
}

// L2Authd.exe 0x00412A70, Src/accountdb.cpp:153-185.
int AccountDB::UpdateSessionId(
    int uid,
    unsigned int sessionId,
    int md5Key,
    int selectedServer) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return 10;
        }
        if (account->second->md5key != md5Key) {
            return 9;
        }
        if (account->second->um_mode == UM_IN_GAME) {
            return 14;
        }

        account->second->sid = sessionId;
        account->second->selectedServerid =
            static_cast<char>(selectedServer);
    }
    return 0;
}

// L2Authd.exe 0x00412B60, Src/accountdb.cpp:448-462.
void AccountDB::TimerCallback(int uid) {
    LoginUser* user = nullptr;
    if (!FindAccount(uid, &user)) {
        return;
    }

    const UserMode mode = user->um_mode;
    user->ReleaseRef();
    if (mode != UM_IN_GAME) {
        KickAccount(uid, 0, true, true);
    }
}

// L2Authd.exe 0x00412BD0, Src/accountdb.cpp:406-436.
void AccountDB::QuitGameCallback(int uid) {
    unsigned int sessionId = 0;
    void* timer = nullptr;
    bool found = false;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            sessionId = account->second->sid;
            timer = account->second->timerHandle;
            account->second->timerHandle = nullptr;
            found = true;
        }
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(nullptr, timer);
    }
    if (!found) {
        return;
    }

    CClientSession* const session =
        serverEx.FindSession(sessionId);
    if (session != nullptr) {
        session->CloseSessionActively();
        session->ReleaseRef();
    }
    LogoutProcess(uid, 0);
}

// L2Authd.exe 0x00412CD0, Src/accountdb.cpp:509-530.
std::uint32_t AccountDB::FindSessionId(
    int uid,
    bool armTimer) {
    std::uint32_t sessionId = UINT32_C(0xffffffff);
    void* newTimer = nullptr;
    void* oldTimer = nullptr;
    if (armTimer) {
        // runtime::timer_queue::CreateTimer(
        //     &newTimer,
        //     nullptr,
        //     TimerRoutine,
        //     reinterpret_cast<void*>(
        //         static_cast<std::intptr_t>(uid)),
        //     300000,
        //     0)
        const bool timerCreated = runtime::timer_queue::CreateTimer(
            &newTimer,
            nullptr,
            TimerRoutine,
            reinterpret_cast<void*>(
                static_cast<std::intptr_t>(uid)),
            300000,
            0);
        if (!timerCreated) {
            L2_LOG_ERROR(Log,
                "[sessions] Could not schedule the 300000 ms login-state check for account %d. "
                "Session lookup continues without the new timeout callback.",
                uid);
        }
    }

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            sessionId = account->second->sid;
            oldTimer = account->second->timerHandle;
            account->second->timerHandle = newTimer;
        }
    }

    if (oldTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            oldTimer);
    }
    // The selected image does not cancel newTimer when uid is absent.
    if (sessionId == UINT32_C(0xffffffff) && newTimer != nullptr) {
        L2_LOG_WARN(Log,
            "[sessions] Account %d disappeared before the new 300000 ms login-state timer could be attached. "
            "The recovered path leaves that timer scheduled until its callback runs.",
            uid);
    }
    return sessionId;
}

// L2Authd.exe 0x00413000, Src/accountdb.cpp:465-485.
void AccountDB::RemoveAll(int serverId) {
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ServerDown(%d) charging process START",
    //     serverId)
    // L2_LOG_INFO(Log,
        // "ServerDown(%d) charging process START",
        // serverId)
    L2_LOG_INFO(Log,
        "[sessions] Starting logout processing for accounts associated with world server %d.",
        serverId);

    std::map<int, LoginUser*> users;
    {
        L2_GUARD(m_lock);
        for (const auto& account : usermap) {
            if (account.second->serverid == serverId ||
                account.second->selectedServerid == serverId) {
                users.insert(account);
            }
        }
    }

    for (const auto& account : users) {
        LogoutProcess(account.first, 0);
    }
    users.clear();

    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ServerDown(%d) charging process END",
    //     serverId)
    // L2_LOG_INFO(Log,
        // "ServerDown(%d) charging process END",
        // serverId)
    L2_LOG_INFO(Log,
        "[sessions] Finished invoking logout processing for accounts associated with world server %d.",
        serverId);
}

// L2Authd.exe 0x00412DC0, Src/accountdb.cpp:537-584.
std::uint32_t AccountDB::FindSessionId(
    int uid,
    int serverId,
    bool armTimer,
    char* otherServer,
    char* accountName) {
    std::uint32_t sessionId = UINT32_C(0xffffffff);
    void* newTimer = nullptr;
    void* oldTimer = nullptr;
    if (armTimer) {
        // runtime::timer_queue::CreateTimer(
        //     &newTimer,
        //     nullptr,
        //     TimerRoutine,
        //     reinterpret_cast<void*>(
        //         static_cast<std::intptr_t>(uid)),
        //     300000,
        //     0)
        const bool timerCreated = runtime::timer_queue::CreateTimer(
            &newTimer,
            nullptr,
            TimerRoutine,
            reinterpret_cast<void*>(
                static_cast<std::intptr_t>(uid)),
            300000,
            0);
        if (!timerCreated) {
            L2_LOG_ERROR(Log,
                "[sessions] Could not schedule the 300000 ms login-state check for account %d. "
                "Session lookup continues without the new timeout callback.",
                uid);
        }
    }
    *otherServer = 0;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            LoginUser* const user = account->second;
            sessionId = user->sid;
            oldTimer = user->timerHandle;
            user->timerHandle = newTimer;

            if (serverId == user->selectedServerid) {
                user->serverid = static_cast<char>(serverId);
                if (config.PBType == 2) {
                    const WorldServer server =
                        g_ServerList.GetAt(serverId);
                    if (server.serverKind != 0x101 &&
                        server.serverKind != 0x104) {
                        user->originChargedServerId =
                            static_cast<char>(serverId);
                    }
                } else {
                    user->originChargedServerId =
                        static_cast<char>(serverId);
                }
                user->um_mode = UM_PLAY_OK;
                std::strncpy(accountName, user->account, 14);
            } else {
                *otherServer = user->selectedServerid;
                std::strncpy(accountName, user->account, 14);
            }
            user->selectedServerid = 0;
        }
    }

    if (oldTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            oldTimer);
    }
    // The selected image does not cancel newTimer when uid is absent.
    if (sessionId == UINT32_C(0xffffffff) && newTimer != nullptr) {
        L2_LOG_WARN(Log,
            "[sessions] Account %d disappeared before the new 300000 ms world-entry timer for server %d could be attached. "
            "The recovered path leaves that timer scheduled until its callback runs.",
            uid, serverId);
    }
    return sessionId;
}

// L2Authd.exe 0x004131A0, Src/accountdb.cpp:32-53.
char AccountDB::FindAccount(int uid, char* accountName) {
    void* timer = nullptr;
    char lastWorld = 0;

    bool missing = false;
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            std::strncpy(accountName, account->second->account, 15);
            timer = account->second->timerHandle;
            account->second->timerHandle = nullptr;
            lastWorld = account->second->lastworld;
        } else {
            missing = true;
        }
    }
    if (missing) {
        lastWorld = -1;
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(nullptr, timer);
    }
    return lastWorld;
}

// L2Authd.exe 0x00413EA0, Src/accountdb.cpp:488-506.
void AccountDB::RemoveAll() {
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "Total ServerDown charging process START")
    // L2_LOG_INFO(Log,
        // "Total ServerDown charging process START")
    L2_LOG_INFO(Log,
        "[sessions] Starting logout processing for all registered accounts.");

    std::map<int, LoginUser*> users;
    {
        L2_GUARD(m_lock);
        users = usermap;
    }

    for (const auto& account : users) {
        LogoutProcess(account.first, 0);
    }
    users.clear();

    // Log.AddLog(
    //     LOG_NORMAL,
    //     "Total ServerDown charging process END")
    // L2_LOG_INFO(Log,
        // "Total ServerDown charging process END")
    L2_LOG_INFO(Log,
        "[sessions] Finished invoking logout processing for all accounts in the captured session list.");
    reporter.m_InGameUser = 0;
}

// L2Authd.exe 0x00413290, Src/accountdb.cpp:57-80.
bool AccountDB::FindAccount(
    int uid,
    char* accountName,
    int* loginFlag,
    int* warnFlag,
    int* stat,
    int* md5Key,
    in_addr* address) {
    void* timer = nullptr;
    bool found = false;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            LoginUser* const user = account->second;
            std::strncpy(accountName, user->account, 15);
            *loginFlag = user->loginflag;
            *warnFlag = user->warnflag;
            *stat = user->stat;
            timer = user->timerHandle;
            *md5Key = user->md5key;
            user->timerHandle = nullptr;
            *address = user->loginIp;
            found = true;
        }
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(nullptr, timer);
    }
    return found;
}

// L2Authd.exe 0x004133C0, Src/accountdb.cpp:84-107.
bool AccountDB::FindAccount(int uid, LoginUser** user) {
    LoginUser* result = nullptr;
    void* timer = nullptr;
    bool found = false;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            result = account->second;
            result->AddRef();
            timer = result->timerHandle;
            result->timerHandle = nullptr;
            found = true;
        }
    }

    *user = result;
    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(nullptr, timer);
    }
    return found;
}

// L2Authd.exe 0x00413490, Src/accountdb.cpp:111-123.
bool AccountDB::FindAccount(int uid) {
    bool found = false;
    {
        L2_GUARD(m_lock);
        found = usermap.find(uid) != usermap.end();
    }
    return found;
}

// L2Authd.exe 0x00413500, Src/accountdb.cpp:126-139.
bool AccountDB::WriteUserData(
    int uid,
    short length,
    const unsigned char* data) {
    bool found = false;
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            account->second->user_data_len = length;
            std::memcpy(
                account->second->user_data,
                data,
                static_cast<std::size_t>(length));
            found = true;
        }
    }
    return found;
}

// L2Authd.exe 0x004135A0, Src/accountdb.cpp:1184-1194.
char AccountDB::UserTimeLogin(
    int uid,
    [[maybe_unused]] LoginUser* user,
    int* remainingTime) {
    return accountdb.CheckUserTime(uid, remainingTime) == 0
               ? 0
               : 0x13;
}

// L2Authd.exe 0x004135E0, Src/accountdb.cpp:1622-1645.
bool AccountDB::GetAccountInfo(
    int uid,
    char* accountName,
    int* loginFlag,
    int* warnFlag,
    int* md5Key,
    unsigned int* sessionId) {
    void* timer = nullptr;
    bool found = false;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            LoginUser* const user = account->second;
            std::strncpy(accountName, user->account, 15);
            *loginFlag = user->loginflag;
            *warnFlag = user->warnflag;
            *sessionId = user->sid;
            *md5Key = user->md5key;
            timer = user->timerHandle;
            user->timerHandle = nullptr;
            found = true;
        }
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(nullptr, timer);
    }
    return found;
}

// L2Authd.exe 0x00413700, Src/accountdb.cpp:1649-1665.
bool AccountDB::GetAccountInfoForIPStop(
    int uid,
    char* accountName,
    int* stat,
    in_addr* address,
    std::int32_t* billStartTime) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return false;
        }

        LoginUser* const user = account->second;
        std::strncpy(accountName, user->account, 15);
        *stat = user->stat;
        *address = user->loginIp;
        *billStartTime = user->billStartTime;
    }
    return true;
}

// L2Authd.exe 0x004137D0, Src/accountdb.cpp:1668-1683.
bool AccountDB::GetPortalUidByGameUid(
    int uid,
    int* portalUid) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return false;
        }
        *portalUid = account->second->portalUid;
    }
    return true;
}

// L2Authd.exe 0x00413860, Src/accountdb.cpp:2732-2737.
int AccountDB::GetBillTraceId() {
    return runtime::raw_atomic::IncrementI32(
        &sBillLogTraceId);
}

// L2Authd.exe 0x00413890, Src/accountdb.cpp:2741-2761.
bool AccountDB::SetPBData(
    int uid,
    char valueType,
    int remainTime,
    int reservations,
    unsigned char* valueId,
    char computationBasis,
    int itemId) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return false;
        }

        LoginUser* const user = account->second;
        user->billStartTime = runtime::crt::Time32Now();
        user->pbServer.valueType = valueType;
        user->pbServer.remainTime = remainTime;
        user->pbServer.reservations = reservations;
        std::memcpy(user->pbServer.valueId, valueId, 16);
        user->pbServer.computationBasis = computationBasis;
        user->pbServer.itemId = itemId;
    }
    return true;
}

// L2Authd.exe 0x004139A0, Src/accountdb.cpp:2765-2857.
void AccountDB::WriteBillingData(
    int uid,
    EBillActionFlag action,
    const LoginUser& user) {
    const std::time_t currentTime = std::time(nullptr);
    // const std::tm current = *std::localtime(&currentTime)
    const std::tm current = runtime::crt::LocalTimeOrZero(currentTime);
    char currentText[32];
    std::snprintf(
        currentText,
        sizeof(currentText),
        "%d-%d-%d %d:%d:%d",
        current.tm_year + 1900,
        current.tm_mon + 1,
        current.tm_mday,
        current.tm_hour,
        current.tm_min,
        current.tm_sec);

    const std::time_t startTime =
        static_cast<std::time_t>(user.billStartTime);
    // const std::tm start = *std::localtime(&startTime)
    const std::tm start = runtime::crt::LocalTimeOrZero(startTime);
    char startText[32];
    std::snprintf(
        startText,
        sizeof(startText),
        "%d-%d-%d %d:%d:%d",
        start.tm_year + 1900,
        start.tm_mon + 1,
        start.tm_mday,
        start.tm_hour,
        start.tm_min,
        start.tm_sec);

    const int elapsed =
        static_cast<int>(currentTime) - user.billStartTime;
    const auto* const address =
        reinterpret_cast<const unsigned char*>(&user.loginIp);
    char addressText[16];
    std::snprintf(
        addressText,
        sizeof(addressText),
        "%d.%d.%d.%d",
        address[0],
        address[1],
        address[2],
        address[3]);

    int chargeKind = (user.stat % 1000) / 100;
    int billingGroup = 0;
    int billingKind = 0;
    if (user.stat > 0 && user.stat < 1000) {
        billingGroup = 1;
        switch (user.stat) {
        case 1:
        case 11:
            billingKind = 2;
            break;
        case 2:
        case 3:
        case 5:
        case 10:
            billingKind = 1;
            break;
        default:
            billingKind = 0;
            break;
        }
    } else {
        if (user.chargeServerId == 2) {
            chargeKind = user.pbServer.valueType;
        }
        billingGroup = 2;
        switch (chargeKind) {
        case 1:
            billingKind = 1;
            break;
        case 2:
            billingKind = 2;
            break;
        case 3:
            billingKind = 3;
            break;
        default:
            break;
        }
    }

    char valueIdText[36]{};
    char byteText[4];
    for (int index = 0; index < 16; ++index) {
        std::snprintf(
            byteText,
            sizeof(byteText),
            "%02x",
            user.pbServer.valueId[index]);
        std::strcat(valueIdText, byteText);
    }

    billingLog.AddLog(
        LOG_NORMAL,
        "%s,%d,%s,%d,%d,%d,%d,0x%s,%s,%d,%s,%s,%d\r\n",
        currentText,
        user.billlogTraceId,
        user.account,
        uid,
        action,
        billingGroup,
        billingKind,
        valueIdText,
        user.loginCafe,
        user.chargeServerId,
        addressText,
        startText,
        elapsed);
}

// L2Authd.exe 0x00413D00, Src/accountdb.cpp:1687-1701.
int AccountDB::GetCharNum([[maybe_unused]] int uid) {
    return 0;
}

// L2Authd.exe 0x00413D20, Src/accountdb.cpp:1704-1714.
bool AccountDB::CreateChar(
    [[maybe_unused]] int uid,
    [[maybe_unused]] int characterId,
    [[maybe_unused]] char serverId) {
    return false;
}

// L2Authd.exe 0x00413D40, Src/accountdb.cpp:1717-1732.
bool AccountDB::DelChar(
    int uid,
    int characterId,
    char serverId) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::OdbcLen characterIndicator = 0;
    authd::platform::OdbcLen serverIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        1,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &uid,
        0,
        &uidIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &characterId,
        0,
        &characterIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        3,
        kInput,
        kCTinyInt,
        kSqlTinyInt,
        0,
        0,
        &serverId,
        0,
        &serverIndicator);

    char statement[256];
    std::strcpy(
        statement,
        "{CALL dbo.ap_delchar (?,?,?) }");
    const authd::platform::OdbcReturn result =
        authd::platform::SQLExecDirect(
            connection.m_stmt,
            statement,
            kSqlNts);
    return result == kSqlSuccess ||
           result == kSqlSuccessWithInfo;
}

// L2Authd.exe 0x00413E80, Src/accountdb.cpp:1735-1743.
bool AccountDB::UpdateCharLocation(
    [[maybe_unused]] int uid,
    [[maybe_unused]] int oldCharacterId,
    [[maybe_unused]] char oldServerId,
    [[maybe_unused]] int newCharacterId,
    [[maybe_unused]] int newServerId) {
    return false;
}

// L2Authd.exe 0x00413FB0, Src/accountdb.cpp:1748-1761.
bool AccountDB::GetUserInfo(
    int uid,
    char& age,
    char& gender) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return false;
        }
        age = account->second->age;
        gender = account->second->gender;
    }
    return true;
}

// L2Authd.exe 0x00414040, Src/accountdb.cpp:1791-1817.
unsigned char AccountDB::GetCardKeyNSaveState(
    int uid,
    int cardNumber,
    PreLoginUser* user) {
    const unsigned char cardKey =
        static_cast<unsigned char>((std::rand() & 0x1f) + 1);

    {
        L2_GUARD(m_preLock);
        const auto existing = preUserMap.find(uid);
        if (existing != preUserMap.end()) {
            PreLoginUser& saved = existing->second;
            saved.cdkind = user->cdkind;
            saved.subscription = user->subscription;
            saved.clientInstances = user->clientInstances;
            std::memcpy(
                &saved.account,
                &user->account,
                sizeof(CAccount));
            saved.nCardNumber = cardNumber;
            saved.bPassFlag = false;
            saved.cCardKey = cardKey;
        } else {
            user->bPassFlag = false;
            user->cCardKey = cardKey;
            user->nCheckCount = 0;
            user->nCardNumber = cardNumber;
            preUserMap.insert({uid, *user});
        }
    }
    return cardKey;
}

// L2Authd.exe 0x004141D0, Src/accountdb.cpp:1770-1782.
bool AccountDB::CheckSecurityPass(int uid) {
    bool passed = false;
    {
        L2_GUARD(m_preLock);
        const auto user = preUserMap.find(uid);
        if (user != preUserMap.end()) {
            passed = user->second.bPassFlag;
            if (passed) {
                preUserMap.erase(user);
            }
        }
    }
    return passed;
}

// L2Authd.exe 0x00414260, Src/accountdb.cpp:1825-1881.
unsigned char AccountDB::CheckSecurityCardValue(
    int uid,
    int answer,
    PreLoginUser* user) {
    unsigned char result = 0x1f;

    unsigned char cardKey = 0;
    int cardNumber = 0;
    int checkCount = 0;
    {
        L2_GUARD(m_preLock);
        const auto saved = preUserMap.find(uid);
        if (saved == preUserMap.end()) {
            result = 0x11;
            return result;
        }

        cardKey = saved->second.cCardKey;
        cardNumber = saved->second.nCardNumber;
        checkCount = ++saved->second.nCheckCount;
    }

    if (answer == CGenerator::GetCardKeyValue(
                      cardNumber,
                      cardKey)) {
        {
            L2_GUARD(m_preLock);
            const auto matched = preUserMap.find(uid);
            if (matched != preUserMap.end()) {
                matched->second.bPassFlag = true;
                std::memcpy(
                    user,
                    &matched->second,
                    sizeof(PreLoginUser));
                preUserMap.erase(matched);
                result = 0;
            } else {
                result = 0x11;
            }
        }
    } else if (checkCount >= config.securityFailureCount) {
        {
            L2_GUARD(m_preLock);
            preUserMap.erase(uid);
        }

        CDBConn connection(g_linDB);
        connection.BindParam(&uid, 1);
        connection.Execute(
            "{call ap_SetPasswordResetFlag (?)}");
        result = 0x11;
    }
    return result;
}

// L2Authd.exe 0x00414520, Src/accountdb.cpp:142-147.
bool AccountDB::IsPermittedServer(
    int serverId,
    char* forbiddenServers,
    [[maybe_unused]] unsigned int capacity) {
    const std::div_t byteAndBit =
        std::div(serverId - 1, 8);
    return ((forbiddenServers[byteAndBit.quot] >>
             (byteAndBit.rem & 0x1f)) &
            1U) == 0;
}

// L2Authd.exe 0x00414580, Src/accountdb.cpp:1885-1919.
char AccountDB::DetermineChargeServer(
    const LoginUser* user) {
    for (int index = 0; index < 4; ++index) {
        switch (config.ChargePriority[index]) {
        case 0:
            if (user->ipServer.valueType > 0 &&
                !user->ipServer.otherIPCharging) {
                return 1;
            }
            if (user->pbServer.valueType > 0) {
                return 2;
            }
            break;
        case 1:
            if (user->pbServer.valueType == 1) {
                return 2;
            }
            break;
        case 2:
            if (user->pbServer.valueType == 2) {
                return 2;
            }
            break;
        case 3:
            if (user->pbServer.valueType == 3) {
                return 2;
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

// L2Authd.exe 0x00415A40, Src/accountdb.cpp:2905-2932.
bool AccountDB::UpdateChargeServerData(
    int uid,
    int source,
    char valueType,
    int remainTime,
    int reservations,
    char computationBasis,
    int itemId) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return false;
        }

        LoginUser* const user = account->second;
        if (source == 2) {
            user->pbServer.valueType = valueType;
            user->pbServer.remainTime = remainTime;
            user->pbServer.reservations = reservations;
            user->pbServer.computationBasis = computationBasis;
            user->pbServer.itemId = itemId;
        } else if (source == 1) {
            user->ipServer.valueType = valueType;
            user->ipServer.remainTime = remainTime;
            user->ipServer.reservations = reservations;
        }
    }
    return true;
}

// L2Authd.exe 0x00415B60, Src/accountdb.cpp:2936-2970.
bool AccountDB::UpdateChargeData(
    int uid,
    int chargeServerId,
    int stat,
    UserMode mode) {
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account == usermap.end()) {
            return false;
        }

        LoginUser* const user = account->second;
        user->chargeServerId =
            static_cast<char>(chargeServerId);
        user->um_mode = mode;
        if (chargeServerId != 0 || config.useDualCharging) {
            user->stat = stat;
        }

        std::memset(user->forbidden_servers, 0, 16);
        if (chargeServerId == 2) {
            user->stat = user->pbServer.valueType * 100 + 1000;
            g_ServerList.MapForbiddenServers(
                user->pbServer.availableServers,
                user->pbServer.availableServersSize,
                user->forbidden_servers,
                16);
        }
        g_ServerList.MapForbiddenServers(
            user->age,
            user->forbidden_servers,
            16);
    }
    return true;
}

// L2Authd.exe 0x00415D30, Src/accountdb.cpp:2974-2992.
void AccountDB::SetAvailableServersData(
    int uid,
    int size,
    unsigned char* availableServers,
    LoginUser* user) {
    if (size <= 0 || availableServers == nullptr) {
        return;
    }

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            LoginUser* const saved = account->second;
            std::memset(saved->pbServer.availableServers, 0, 64);
            saved->pbServer.availableServersSize = size;
            std::memcpy(
                saved->pbServer.availableServers,
                availableServers,
                static_cast<std::size_t>(size));
        } else {
            user->pbServer.availableServersSize = size;
            std::memcpy(
                user->pbServer.availableServers,
                availableServers,
                static_cast<std::size_t>(size));
        }
    }
}

// L2Authd.exe 0x00415E20, Src/accountdb.cpp:2996-3015.
bool AccountDB::SetWaitUser(int uid, int serverId) {
    void* timer = nullptr;
    bool found = false;
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            account->second->serverid =
                static_cast<char>(serverId);
            timer = account->second->timerHandle;
            account->second->timerHandle = nullptr;
            found = true;
        }
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            timer);
    }
    return found;
}

// L2Authd.exe 0x004164B0, Src/accountdb.cpp:3199-3208.
char AccountDB::GetComputationBasis(
    const LoginUser& user) {
    if (user.chargeServerId == 2) {
        return user.pbServer.computationBasis;
    }
    return 0;
}

// L2Authd.exe 0x004164F0, Src/accountdb.cpp:3212-3238.
bool AccountDB::UpdateUserLevel(int uid, int userLevel) {
    int serverId = 0;
    bool found = false;
    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            serverId = account->second->serverid;
            if (userLevel == 0) {
                account->second->loginflag &= ~0x400;
            } else if (userLevel == 1) {
                account->second->loginflag |= 0x400;
            }
            found = true;
        }
    }

    if (!found) {
        return false;
    }
    const WorldServer server = g_ServerList.GetAt(serverId);
    return SendSocket(
               server.inner_addr,
               "cdd",
               20,
               uid,
               userLevel) == 1;
}

// L2Authd.exe 0x004166D0, Src/accountdb.cpp:3248-3351.
void AccountDB::InitLoginUserData(
    LoginUser* loginUser,
    const PreLoginUser& preLoginUser,
    const CAccount& account,
    in_addr address,
    int md5Key,
    unsigned int sessionId) {
    loginUser->loginIp = address;
    loginUser->md5key = md5Key;
    if (!config.usePBServer) {
        loginUser->stat = account.pay_stat;
    }
    loginUser->sid = sessionId;
    loginUser->gender = account.gender;
    loginUser->um_mode = UM_LOGIN;
    loginUser->loginflag = account.login_flag;
    loginUser->warnflag = account.warn_flag;
    loginUser->age = account.age;
    loginUser->cdkind = preLoginUser.cdkind;
    loginUser->uid = account.uid;

    std::strncpy(loginUser->account, account.account, 15);
    loginUser->account[14] = '\0';
    std::strncpy(
        loginUser->loginAccount,
        preLoginUser.loginAccount,
        65);
    std::strncpy(
        loginUser->loginPassword,
        preLoginUser.loginPassword,
        17);
    loginUser->loginAccount[64] = '\0';
    loginUser->loginPassword[16] = '\0';
    loginUser->sessionId = preLoginUser.sessionId;

    loginUser->lastworld = account.lastworld;
    loginUser->logintime = runtime::crt::Time32Now();
    loginUser->billStartTime = 0;
    loginUser->subscription = preLoginUser.subscription;
    loginUser->serviceflag = account.service_flag;
    loginUser->externalAuthAdditionalInfo =
        account.externalAuthAdditionalInfo;
    std::strncpy(loginUser->loginCafe, "0000000", 9);
    std::memcpy(
        loginUser->forbidden_servers,
        account.forbidden_servers,
        16);
    g_ServerList.MapForbiddenServers(
        loginUser->age,
        loginUser->forbidden_servers,
        16);
    loginUser->clientInstances = preLoginUser.clientInstances;
    loginUser->reconnectKey = 0;
    loginUser->chargeflag = false;
    // The selected EXE calls memset(pbServer.valueId, 0, 0): no bytes change.
    loginUser->billlogTraceId = GetBillTraceId();
    loginUser->webSessionId = preLoginUser.webSessionId;
    loginUser->securityFlag = preLoginUser.account.securityFlag;
    loginUser->securityUsage = preLoginUser.account.securityUsage;

    // The selected EXE likewise calls memset(ptid, 0, 0) here.
    if (config.externalFatigueCompanyCode == 4) {
        std::memcpy(
            loginUser->ptid,
            preLoginUser.externalUser.externalToken + 1,
            32);
    }

    if (config.UserData) {
        loginUser->user_data_len = account.user_data_len;
        const unsigned int dataLength =
            static_cast<unsigned short>(
                loginUser->user_data_len);
        std::memcpy(
            loginUser->user_data,
            account.user_data,
            dataLength <= 0x800 ? dataLength : 0x800);
    }

    if (config.useExternalFatigueSystem ||
        config.useFatigueSystem) {
        loginUser->auth_flag = account.auth_flag;
        loginUser->fcid = account.fcid;
        loginUser->fatigueRestTime = 0;
        loginUser->fatiguePlayTime = 0;
    }

    if (!config.usePAServer || preLoginUser.portalUser.uid == 0) {
        loginUser->portalUid = 0;
    } else {
        loginUser->portalUid = preLoginUser.portalUser.uid;
        std::strcpy(
            loginUser->portalAccount,
            preLoginUser.portalUser.account);
        loginUser->userId = preLoginUser.portalUser.userId;
    }

    loginUser->packageVersionInfo =
        config.usePackageVersionInfo
            ? preLoginUser.account.packageVersionInfo
            : 0;
    std::strcpy(
        loginUser->playAccount,
        preLoginUser.playAccount);
    std::memcpy(
        loginUser->byteChildPlayTimeInfo,
        account.byteChildPlayTimeInfo,
        24);
    loginUser->user_level = account.user_level;
    loginUser->lastLogoutTime = account.lastLogoutTime;
    loginUser->activateTime = account.activateTime;
    std::memcpy(
        loginUser->activateInformation,
        account.activateInformation,
        8);
}

// L2Authd.exe 0x00414660, Src/accountdb.cpp:2440-2442.
int AccountDB::GetReconnectKey() {
    return (std::rand() * 0x100 + std::rand()) | 1;
}

// L2Authd.exe 0x00414690, Src/accountdb.cpp:820-899.
bool AccountDB::QuitGameCharge(
    int uid,
    [[maybe_unused]] int useTime,
    int serverId,
    int* result) {
    bool changedServer = false;
    LoginUser snapshot;
    void* oldTimer = nullptr;
    void* newTimer = nullptr;
    // runtime::timer_queue::CreateTimer(
    //     &newTimer,
    //     nullptr,
    //     QuitGameTimer,
    //     reinterpret_cast<void*>(
    //         static_cast<std::intptr_t>(uid)),
    //     static_cast<std::uint32_t>(config.SocketTimeOut),
    //     0)
    const bool timerCreated = runtime::timer_queue::CreateTimer(
        &newTimer,
        nullptr,
        QuitGameTimer,
        reinterpret_cast<void*>(
            static_cast<std::intptr_t>(uid)),
        static_cast<std::uint32_t>(config.SocketTimeOut),
        0);
    if (!timerCreated) {
        L2_LOG_ERROR(Log,
            "[sessions] Could not schedule session cleanup for account %d, server %d "
            "(delay %u ms). Quit-game processing continues without the new timeout callback.",
            uid, serverId, static_cast<unsigned int>(static_cast<std::uint32_t>(config.SocketTimeOut)));
    }

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            LoginUser* const user = account->second;
            if (user->serverid == serverId || serverId == 0) {
                user->um_mode = UM_CHANGE_SERVER;
                user->lastworld = user->serverid;
                user->reconnectKey = GetReconnectKey();
                oldTimer = user->timerHandle;
                user->timerHandle = newTimer;
                *result = user->reconnectKey;
                std::memcpy(&snapshot, user, sizeof(snapshot));
                user->chargeflag = false;
                changedServer = true;
            } else if (newTimer != nullptr) {
                // The selected image cancels the newly-created timer here while
                // still holding m_lock.
                runtime::timer_queue::DeleteTimer(
                    nullptr,
                    newTimer);
            }
        }
    }

    if (oldTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            oldTimer);
    }

    // When uid is absent, the selected image leaves newTimer
    // armed. Its one-shot callback later observes the missing account.
    if (changedServer) {
        snapshot.account[14] = '\0';
        if (snapshot.serverid > 0) {
            CClientSession* const session =
                serverEx.FindSession(snapshot.sid);
            if (session != nullptr) {
                session->SetUserMode(UM_CHANGE_SERVER);
                session->ReleaseRef();
            }

            const std::int32_t now =
                runtime::crt::Time32Now();
            if (snapshot.chargeflag) {
                EndCharge(uid, snapshot, now);
            }
            if (config.useDualCharging &&
                config.useFatigueSystem &&
                snapshot.playStartTime != 0) {
                AccountContainer::Inst()->SetAccountTime(
                    snapshot.fcid,
                    now - snapshot.playStartTime);
            }
            if (snapshot.chargeServerId == 1) {
                const int sessionId =
                    ipsessionDB.DelSessionID(uid);
                if (sessionId != 0) {
                    ipsessionDB.ReleaseSessionRequest(
                        sessionId,
                        snapshot.loginIp,
                        snapshot.stat);
                }
            }
        }
    }
    return changedServer;
}

// L2Authd.exe 0x004149E0, Src/accountdb.cpp:929-945.
void AccountDB::EndChargePersonalSepcific(
    int uid,
    int useTime) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen indicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        1,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &useTime,
        0,
        &indicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &uid,
        0,
        &indicator);

    char statement[256];
    std::strcpy(
        statement,
        "{CALL dbo.ap_SUserTime (?, ?) }");
    if (authd::platform::SQLExecDirect(
            connection.m_stmt,
            statement,
            kSqlNts) != kSqlSuccess) {
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement);
    }
    connection.ResetHtmt();
}

// L2Authd.exe 0x00414B20, Src/accountdb.cpp:952-983.
void AccountDB::EndChargePersonalSepcificDate(
    int uid,
    int useTime,
    int payStat,
    std::int32_t loginTime) {
    const std::time_t rawLoginTime =
        static_cast<std::time_t>(loginTime);
    const std::tm login = *std::localtime(&rawLoginTime);
    TIMESTAMP_STRUCT loginTimestamp{};
    loginTimestamp.year = static_cast<std::int16_t>(
        login.tm_year + 1900);
    loginTimestamp.month = static_cast<std::uint16_t>(
        login.tm_mon + 1);
    loginTimestamp.day = static_cast<std::uint16_t>(
        login.tm_mday);
    loginTimestamp.hour = static_cast<std::uint16_t>(
        login.tm_hour);
    loginTimestamp.minute = static_cast<std::uint16_t>(
        login.tm_min);
    loginTimestamp.second = static_cast<std::uint16_t>(
        login.tm_sec);
    loginTimestamp.fraction = 0;

    CDBConn connection(g_linDB);
    authd::platform::OdbcLen indicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        1,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &useTime,
        0,
        &indicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &uid,
        0,
        &indicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        3,
        kInput,
        kCUInt,
        kCInt,
        0,
        0,
        &payStat,
        0,
        &indicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        4,
        kInput,
        kCTimestamp,
        kSqlTimestamp,
        19,
        0,
        &loginTimestamp,
        0,
        &indicator);

    char statement[256];
    std::strcpy(
        statement,
        "{CALL dbo.ap_SUserTime (?, ?, ?, ?) }");
    if (authd::platform::SQLExecDirect(
            connection.m_stmt,
            statement,
            kSqlNts) != kSqlSuccess) {
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement);
    }
    connection.ResetHtmt();
}

// L2Authd.exe 0x00414D30, Src/accountdb.cpp:986-1179.
void AccountDB::WritePlayLog(
    const LoginUser& user,
    int uid) {
    const std::int32_t logoutTime =
        runtime::crt::Time32Now();
    const int playDuration = logoutTime - user.logintime;

    const auto* const addressOctets =
        reinterpret_cast<const unsigned char*>(
            &user.loginIp.s_addr);
    char addressText[16];
    std::snprintf(
        addressText,
        sizeof(addressText),
        "%d.%d.%d.%d",
        addressOctets[0],
        addressOctets[1],
        addressOctets[2],
        addressOctets[3]);

    WriteLogD(
        user.chargeflag ? 0x324 : 0x33f,
        const_cast<char*>(user.account),
        user.loginIp,
        user.stat,
        user.age,
        user.gender,
        0,
        user.chargeflag ? playDuration : 0,
        uid);

    const std::time_t rawLogoutTime =
        static_cast<std::time_t>(logoutTime);
    const std::tm logout =
        *std::localtime(&rawLogoutTime);
    const std::time_t rawLoginTime =
        static_cast<std::time_t>(user.logintime);
    const std::tm login =
        *std::localtime(&rawLoginTime);

    if (user.gamePlayed) {
        TIMESTAMP_STRUCT loginTimestamp{};
        loginTimestamp.year = static_cast<std::int16_t>(
            login.tm_year + 1900);
        loginTimestamp.month = static_cast<std::uint16_t>(
            login.tm_mon + 1);
        loginTimestamp.day = static_cast<std::uint16_t>(
            login.tm_mday);
        loginTimestamp.hour = static_cast<std::uint16_t>(
            login.tm_hour);
        loginTimestamp.minute = static_cast<std::uint16_t>(
            login.tm_min);
        loginTimestamp.second = static_cast<std::uint16_t>(
            login.tm_sec);
        loginTimestamp.fraction = 0;

        TIMESTAMP_STRUCT logoutTimestamp{};
        logoutTimestamp.year = static_cast<std::int16_t>(
            logout.tm_year + 1900);
        logoutTimestamp.month = static_cast<std::uint16_t>(
            logout.tm_mon + 1);
        logoutTimestamp.day = static_cast<std::uint16_t>(
            logout.tm_mday);
        logoutTimestamp.hour = static_cast<std::uint16_t>(
            logout.tm_hour);
        logoutTimestamp.minute = static_cast<std::uint16_t>(
            logout.tm_min);
        logoutTimestamp.second = static_cast<std::uint16_t>(
            logout.tm_sec);
        logoutTimestamp.fraction = 0;

        CDBConn connection(g_linDB);
        // Selected L2Authd.exe keeps this shared indicator at zero
        // (0x00414F7A), then binds addressText at 0x004150C5-0x004150F0
        // with strlen(addressText) as BufferLength but the same zero
        // StrLen_or_IndPtr value. So ap_SLog receives an empty IP
        // string under the target ODBC contract. The PostgreSQL boundary
        // keeps that shipping behavior.
        authd::platform::OdbcLen indicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            1,
            kInput,
            kCUInt,
            kCInt,
            0,
            0,
            &uid,
            0,
            &indicator);
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            2,
            kInput,
            kCTimestamp,
            kSqlTimestamp,
            19,
            0,
            &loginTimestamp,
            0,
            &indicator);
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            3,
            kInput,
            kCTimestamp,
            kSqlTimestamp,
            19,
            0,
            &logoutTimestamp,
            0,
            &indicator);
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            4,
            kInput,
            kCUInt,
            kCInt,
            0,
            0,
            &config.gameId,
            0,
            &indicator);

        const WorldServer world =
            g_ServerList.GetAt(user.serverid);
        const char* const chargedServer =
            world.serverKind == 0x101 ||
                    world.serverKind == 0x104
                ? &user.lastworld
                : &user.serverid;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            5,
            kInput,
            kCUTinyInt,
            kSqlTinyInt,
            0,
            0,
            const_cast<char*>(chargedServer),
            0,
            &indicator);
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            6,
            kInput,
            kCChar,
            kSqlVarchar,
            15,
            0,
            addressText,
            static_cast<authd::platform::OdbcLen>(
                std::strlen(addressText)),
            &indicator);

        char statement[256];
        std::strcpy(
            statement,
            "{CALL dbo.ap_SLog (?,?,?,?,?,?) }");
        if (authd::platform::SQLExecDirect(
                connection.m_stmt,
                statement,
                kSqlNts) != kSqlSuccess) {
            connection.Error(
                kStatementHandle,
                connection.m_stmt,
                statement);
        }
        connection.ResetHtmt();
    }

    if (config.useUserUseLog) {
        const std::int32_t userUseTime =
            runtime::crt::Time32Now();
        const std::time_t rawUserUseTime =
            static_cast<std::time_t>(userUseTime);
        // const std::tm current = *std::localtime(&rawUserUseTime)
        const std::tm current = runtime::crt::LocalTimeOrZero(rawUserUseTime);
        UserUseLog.AddLog(
            LOG_NORMAL,
            "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,LOGOUT,%s,%d-%02d-%02d %02d:%02d:%02d,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
            current.tm_year + 1900,
            current.tm_mon + 1,
            current.tm_mday,
            current.tm_hour,
            current.tm_min,
            current.tm_sec,
            login.tm_year + 1900,
            login.tm_mon + 1,
            login.tm_mday,
            login.tm_hour,
            login.tm_min,
            login.tm_sec,
            user.account,
            logout.tm_year + 1900,
            logout.tm_mon + 1,
            logout.tm_mday,
            logout.tm_hour,
            logout.tm_min,
            logout.tm_sec,
            user.serverid,
            user.stat,
            ::inet_ntoa(user.loginIp),
            user.loginCafe,
            uid,
            user.stat != 0,
            user.gender,
            user.age);
    }

    if (config.useDualLog) {
        const std::int32_t dualTime =
            runtime::crt::Time32Now();
        const std::time_t rawDualTime =
            static_cast<std::time_t>(dualTime);
        // const std::tm current = *std::localtime(&rawDualTime)
        const std::tm current = runtime::crt::LocalTimeOrZero(rawDualTime);
        if (user.um_mode == UM_IN_GAME) {
            dualLog.AddLog(
                LOG_NORMAL,
                "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,SERVEROUT,%s,,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
                current.tm_year + 1900,
                current.tm_mon + 1,
                current.tm_mday,
                current.tm_hour,
                current.tm_min,
                current.tm_sec,
                login.tm_year + 1900,
                login.tm_mon + 1,
                login.tm_mday,
                login.tm_hour,
                login.tm_min,
                login.tm_sec,
                user.account,
                user.serverid,
                user.stat,
                ::inet_ntoa(user.loginIp),
                user.loginCafe,
                uid,
                user.stat != 0,
                user.gender,
                user.age);
        }
        dualLog.AddLog(
            LOG_NORMAL,
            "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,LOGOUT,%s,%d-%02d-%02d %02d:%02d:%02d,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
            current.tm_year + 1900,
            current.tm_mon + 1,
            current.tm_mday,
            current.tm_hour,
            current.tm_min,
            current.tm_sec,
            login.tm_year + 1900,
            login.tm_mon + 1,
            login.tm_mday,
            login.tm_hour,
            login.tm_min,
            login.tm_sec,
            user.account,
            logout.tm_year + 1900,
            logout.tm_mon + 1,
            logout.tm_mday,
            logout.tm_hour,
            logout.tm_min,
            logout.tm_sec,
            user.serverid,
            user.stat,
            ::inet_ntoa(user.loginIp),
            user.loginCafe,
            uid,
            user.stat != 0,
            user.gender,
            user.age);
    }

    if (config.Country == 0) {
        actionLog.AddLog(
            LOG_NORMAL,
            "%d-%d-%d %d:%d:%d,%d-%d-%d %d:%d:%d,%s,%d,%s,%d,%d,%d,%06d%07d,%d,%d,%d,%d,%s,%d,%d,%d\r\n",
            logout.tm_year + 1900,
            logout.tm_mon + 1,
            logout.tm_mday,
            logout.tm_hour,
            logout.tm_min,
            logout.tm_sec,
            login.tm_year + 1900,
            login.tm_mon + 1,
            login.tm_mday,
            login.tm_hour,
            login.tm_min,
            login.tm_sec,
            user.account,
            user.serverid,
            addressText,
            user.stat,
            playDuration,
            playDuration,
            0,
            0,
            user.serverid,
            logout.tm_wday,
            user.age,
            user.cdkind,
            user.loginCafe,
            user.billlogTraceId,
            uid,
            user.stat != 0);
    } else {
        actionLog.AddLog(
            LOG_NORMAL,
            "%d-%d-%d %d:%d:%d,%d-%d-%d %d:%d:%d,%s,%d,%s,%d,%d,%d,%06d%07d,%d,%d,%d,%d,%s,%d\r\n",
            logout.tm_year + 1900,
            logout.tm_mon + 1,
            logout.tm_mday,
            logout.tm_hour,
            logout.tm_min,
            logout.tm_sec,
            login.tm_year + 1900,
            login.tm_mon + 1,
            login.tm_mday,
            login.tm_hour,
            login.tm_min,
            login.tm_sec,
            user.account,
            user.serverid,
            addressText,
            user.stat,
            playDuration,
            playDuration,
            0,
            0,
            user.serverid,
            logout.tm_wday,
            user.age,
            user.cdkind,
            user.loginCafe,
            user.billlogTraceId);
    }
}

// L2Authd.exe 0x00415740, Src/accountdb.cpp:2446-2551.
char AccountDB::UpdateSession(
    int uid,
    int oneTimeKey,
    int reconnectKey,
    CClientSession* client) {
    char result = 20;
    LoginUser* user = nullptr;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            const bool validOneTimeKey =
                account->second->md5key == oneTimeKey ||
                account->second->md5key == -1;
            const bool validReconnectKey =
                account->second->um_mode == UM_CHANGE_SERVER &&
                reconnectKey != 0 &&
                account->second->reconnectKey == reconnectKey;
            if (!validOneTimeKey || !validReconnectKey) {
                result = 9;
            } else {
                account->second->md5key =
                    client->GetMd5Key();
                account->second->sid =
                    client->GetSessionId();
                account->second->reconnectKey = 0;
                if (!config.usePBServer &&
                    !config.UseIPServer) {
                    account->second->um_mode = UM_LOGIN;
                }
                user = account->second;
                user->AddRef();
                result = 0;
            }
        }
    }

    client->SetUid(uid);
    if (result != 0) {
        return result;
    }

    if (config.useNoBill) {
        user->stat = 2000;
    }
    if (!config.usePBServer) {
        if (!config.UseIPServer) {
            client->SetUserMode(UM_LOGIN);
            const int payClass =
                (user->stat % 1000) / 100;
            int remainingTime = 0;
            if (user->stat == 0) {
                if (config.FreeServer != 3) {
                    user->ReleaseRef();
                    return 18;
                }
                user->stat = 2000;
            } else if (payClass == 2 ||
                       payClass == 5) {
                result = UserTimeLogin(
                    uid,
                    user,
                    &remainingTime);
            }
            if (result != 0) {
                if (config.FreeServer != 3) {
                    user->ReleaseRef();
                    return result;
                }
                user->stat = 2000;
            }
            client->Send(
                "cddc",
                12,
                uid,
                client->GetMd5Key(),
                0);
        } else {
            PBServerMgr::Inst()->GetValueInfoChangeServer(
                user,
                client,
                uid,
                1,
                0,
                0,
                0,
                0,
                0);
        }
    } else {
        result = PBServerMgr::Inst()->RequestGetValueInfo(
            uid,
            user,
            client,
            8,
            user->originChargedServerId);
    }
    user->ReleaseRef();
    return result;
}

// L2Authd.exe 0x00415EE0, Src/accountdb.cpp:3121-3176.
bool AccountDB::EndCharge(
    int uid,
    LoginUser& user,
    int endTime) {
    WriteBillingData(uid, ACTION_STOP, user);
    const int elapsed = endTime - user.billStartTime;
    const int payClass = (user.stat % 1000) / 100;

    if (CIPSessionDB::IsIPCharging(user.stat)) {
        ipsessionDB.StopIPCharge(
            static_cast<unsigned int>(uid),
            user.loginIp.s_addr,
            user.stat,
            elapsed,
            user.billStartTime,
            user.serverid,
            user.account);
        if (!config.useDualCharging) {
            const int sessionId =
                ipsessionDB.DelSessionID(uid);
            if (sessionId != 0) {
                ipsessionDB.ReleaseSessionRequest(
                    sessionId,
                    user.loginIp,
                    user.stat);
            }
        }
    } else if (!config.usePBServer ||
               user.chargeServerId != 2) {
        if (payClass == 2) {
            EndChargePersonalSepcific(uid, elapsed);
        } else if (payClass == 5) {
            EndChargePersonalSepcificDate(
                uid,
                elapsed,
                user.stat,
                user.billStartTime);
        }
    } else {
        PBServerMgr::Inst()->NotifyEndCharge(uid);
    }

    if (config.useExternalFatigueSystem &&
        config.externalFatigueCompanyCode != 1) {
        fatigueMgr.FatigueLogOut(uid, user);
    }
    if (!config.useDualCharging &&
        config.useFatigueSystem) {
        AccountContainer::Inst()->SetAccountTime(
            user.fcid,
            elapsed);
    }
    if (config.useDualCharging) {
        user.billStartTime = 0;
        user.chargeflag = false;
    }
    runtime::raw_atomic::DecrementI32(
        &reporter.m_InGameUser);
    return true;
}

// L2Authd.exe 0x004160B0, Src/accountdb.cpp:3028-3072.
void AccountDB::LogoutProcess(int uid, short reason) {
    LoginUser* user = nullptr;
    void* timer = nullptr;
    bool found = false;

    {
        L2_GUARD(m_lock);
        const auto account = usermap.find(uid);
        if (account != usermap.end()) {
            timer = account->second->timerHandle;
            account->second->timerHandle = nullptr;
            user = account->second;

            // This first release before erase is present at 0x00416143 in the
            // selected image. A second release follows PostLogoutProcess.
            user->ReleaseRef();
            usermap.erase(account);
            found = true;
        }
    }

    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            timer);
    }
    if (!found) {
        return;
    }

    ConnectedIPList::Inst()->RemoveConnectedIp(
        user->serverid,
        user->loginIp,
        uid);
    user->account[14] = '\0';
    const std::int32_t now =
        runtime::crt::Time32Now();
    if (user->chargeflag) {
        EndCharge(uid, *user, now);
    }
    if (reason == 1) {
        user->serverid = 0;
        user->gamePlayed = true;
    }
    PostLogoutProcess(uid, *user);
    user->ReleaseRef();
}

// L2Authd.exe 0x00416230, Src/accountdb.cpp:3076-3117.
void AccountDB::PostLogoutProcess(
    int uid,
    const LoginUser& user) {
    const std::int32_t now =
        runtime::crt::Time32Now();
    WritePlayLog(user, uid);

    const int sessionId = ipsessionDB.DelSessionID(uid);
    if (sessionId != 0) {
        ipsessionDB.ReleaseSessionRequest(
            sessionId,
            user.loginIp,
            user.stat);
    }
    if (config.useDualCharging &&
        config.useFatigueSystem &&
        user.playStartTime != 0) {
        AccountContainer::Inst()->SetAccountTime(
            user.fcid,
            now - user.playStartTime);
    }
    if (config.useFatigueSystem &&
        AccountContainer::Inst()->IsFatigueCustomer(
            user.fcid,
            user.age,
            user.auth_flag)) {
        AccountContainer::Inst()->RemoveFatigueAccount(
            user.fcid,
            uid);
    }

    WebSessionId& webSessionId =
        const_cast<WebSessionId&>(user.webSessionId);
    if (!config.useWSM) {
        if (config.useGpProxy &&
            user.webSessionId.s_id0 != 0 &&
            user.webSessionId.s_id1 != 0 &&
            user.webSessionId.s_id2 != 0 &&
            user.webSessionId.s_id3 != 0) {
            CGpProxyServerMgr::Inst()->LogoutAccountWsid(
                ORIGIN_LOGOUT,
                nullptr,
                user,
                webSessionId);
        }
    } else {
        WSMServerMgr::Inst()->LogoutAccountWsid(
            ORIGIN_LOGOUT,
            nullptr,
            nullptr,
            webSessionId);
    }
}

// L2Authd.exe 0x004163C0, Src/accountdb.cpp:3180-3196.
void AccountDB::SendKickMessage(
    int kind,
    int uid,
    int serverId,
    unsigned int sessionId,
    char* account,
    char reason) {
    if (kind == 1) {
        if (g_ServerList.ExistId(serverId)) {
            const WorldServer server =
                g_ServerList.GetAt(serverId);
            // SendSocket(
            //     server.inner_addr,
            //     "cdcs",
            //     1,
            //     uid,
            //     reason,
            //     account)
            const int sendResult = SendSocket(
                server.inner_addr,
                "cdcs",
                1,
                uid,
                reason,
                account);
            if (sendResult == 0) {
                L2_LOG_WARN(Log,
                    "[sessions] Cannot send kick for account %d to server %d: no connection was found for its internal address. Local processing continues.",
                    uid, serverId);
            }
        } else {
            L2_LOG_WARN(Log,
                "[sessions] Cannot send kick for account %d to server %d: server is absent from the server list. Local processing continues.",
                uid, serverId);
        }
    } else if (kind == 0) {
        // Send2ClientSession(
        //     sessionId,
        //     "cc",
        //     8,
        //     reason)
        const int sendResult = Send2ClientSession(
            sessionId,
            "cc",
            8,
            reason);
        if (sendResult == 0) {
            L2_LOG_DEBUG(Log,
                "[sessions] Skipped client kick notification for account %d: client session no longer exists. Local processing continues.",
                uid);
        }
    }
}

// L2Authd.exe 0x00416B90, Src/accountdb.cpp:3445-3482.
int AccountDB::MarkActivateTime(
    int uid,
    std::int32_t activateTime,
    unsigned char* information) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        1,
        kInput,
        kCUInt,
        kCInt,
        4,
        0,
        &uid,
        0,
        &uidIndicator);

    const std::time_t timestamp =
        static_cast<std::time_t>(activateTime);
    const std::tm local = *std::localtime(&timestamp);
    char timestampText[20]{};
    std::snprintf(
        timestampText,
        sizeof(timestampText),
        "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        kInput,
        kCChar,
        kSqlVarchar,
        20,
        0,
        timestampText,
        0,
        nullptr);

    authd::platform::OdbcLen informationIndicator = 8;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        3,
        kInput,
        kCBinary,
        kSqlVarBinary,
        8,
        0,
        information,
        8,
        &informationIndicator);

    int procedureResult = 0;
    authd::platform::OdbcLen resultIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        4,
        kOutput,
        kCUInt,
        kCInt,
        4,
        0,
        &procedureResult,
        0,
        &resultIndicator);

    if (!connection.Execute(
            "{CALL dbo.ap_RegisterAccountGameSlot(?,?,?,?)}")) {
        return 1;
    }
    return procedureResult != 0 ? 1 : 0;
}

// L2Authd.exe 0x00416DA0, Src/accountdb.cpp:3485-3533.
bool AccountDB::UpdateCharacterLevel(
    int uid,
    unsigned char serverId,
    int characterId,
    int level,
    std::int32_t updateTime) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::OdbcLen serverIndicator = 0;
    authd::platform::OdbcLen characterIndicator = 0;
    authd::platform::OdbcLen levelIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 1, kInput, kCUInt, kCInt,
        4, 0, &uid, 0, &uidIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 2, kInput, kCTinyInt, kSqlTinyInt,
        1, 0, &serverId, 0, &serverIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 3, kInput, kCUInt, kCInt,
        4, 0, &characterId, 0, &characterIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 4, kInput, kCUInt, kCInt,
        4, 0, &level, 0, &levelIndicator);

    const std::time_t timestamp =
        static_cast<std::time_t>(updateTime);
    const std::tm local = *std::localtime(&timestamp);
    char timestampText[20]{};
    std::snprintf(
        timestampText,
        sizeof(timestampText),
        "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 5, kInput, kCChar, kSqlVarchar,
        20, 0, timestampText, 0, nullptr);

    unsigned char force = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 6, kInput, kCBit, kSqlBit,
        1, 0, &force, 0, nullptr);
    int procedureResult = 0;
    authd::platform::OdbcLen resultIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 7, kOutput, kCUInt, kCInt,
        4, 0, &procedureResult, 0, &resultIndicator);

    return connection.Execute(
               "{CALL dbo.ap_RegisterAccountGameCharacter(?,?,?,?,?,?,?)}") &&
           procedureResult == 0;
}

// L2Authd.exe 0x00417030, Src/accountdb.cpp:3536-3584.
bool AccountDB::ForceUpdateCharacterLevel(
    int uid,
    unsigned char serverId,
    int characterId,
    int level,
    std::int32_t updateTime) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::OdbcLen serverIndicator = 0;
    authd::platform::OdbcLen characterIndicator = 0;
    authd::platform::OdbcLen levelIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 1, kInput, kCUInt, kCInt,
        4, 0, &uid, 0, &uidIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 2, kInput, kCTinyInt, kSqlTinyInt,
        1, 0, &serverId, 0, &serverIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 3, kInput, kCUInt, kCInt,
        4, 0, &characterId, 0, &characterIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 4, kInput, kCUInt, kCInt,
        4, 0, &level, 0, &levelIndicator);

    const std::time_t timestamp =
        static_cast<std::time_t>(updateTime);
    const std::tm local = *std::localtime(&timestamp);
    char timestampText[20]{};
    std::snprintf(
        timestampText,
        sizeof(timestampText),
        "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 5, kInput, kCChar, kSqlVarchar,
        20, 0, timestampText, 0, nullptr);

    unsigned char force = 1;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 6, kInput, kCBit, kSqlBit,
        1, 0, &force, 0, nullptr);
    int procedureResult = 0;
    authd::platform::OdbcLen resultIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 7, kOutput, kCUInt, kCInt,
        4, 0, &procedureResult, 0, &resultIndicator);

    return connection.Execute(
               "{CALL dbo.ap_RegisterAccountGameCharacter(?,?,?,?,?,?,?)}") &&
           procedureResult == 0;
}

// L2Authd.exe 0x004172C0, Src/accountdb.cpp:3587-3622.
bool AccountDB::DeleteCharacterLevel(
    int uid,
    unsigned char serverId,
    int characterId) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::OdbcLen serverIndicator = 0;
    authd::platform::OdbcLen characterIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 1, kInput, kCUInt, kCInt,
        4, 0, &uid, 0, &uidIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 2, kInput, kCTinyInt, kSqlTinyInt,
        1, 0, &serverId, 0, &serverIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 3, kInput, kCUInt, kCInt,
        4, 0, &characterId, 0, &characterIndicator);

    int procedureResult = 0;
    authd::platform::OdbcLen resultIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 4, kOutput, kCUInt, kCInt,
        4, 0, &procedureResult, 0, &resultIndicator);
    return connection.Execute(
               "{CALL dbo.ap_DeleteAccountGameCharacter(?,?,?,?)}") &&
           procedureResult == 0;
}

// L2Authd.exe 0x00417470, Src/accountdb.cpp:3625-3682.
bool AccountDB::GetCharacterMaxLevel(
    int uid,
    unsigned char& serverId,
    int& characterId,
    int& level) {
    CDBConn connection(g_linDB);
    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::OdbcLen serverIndicator = 0;
    authd::platform::OdbcLen characterIndicator = 0;
    authd::platform::OdbcLen levelIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 1, kInput, kCUInt, kCInt,
        4, 0, &uid, 0, &uidIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        serverId == 0 ? kOutput : kInput,
        kCTinyInt,
        kSqlTinyInt,
        1,
        0,
        &serverId,
        0,
        &serverIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 3, kOutput, kCUInt, kCInt,
        4, 0, &characterId, 0, &characterIndicator);
    authd::platform::SQLBindParameter(
        connection.m_stmt, 4, kOutput, kCUInt, kCInt,
        4, 0, &level, 0, &levelIndicator);

    int procedureResult = 0;
    authd::platform::OdbcLen resultIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt, 5, kOutput, kCUInt, kCInt,
        4, 0, &procedureResult, 0, &resultIndicator);
    const bool executed = serverId == 0
        ? connection.Execute(
              "{CALL dbo.ap_GetAccountMaxLevelCharacter(?,?,?,?,?)}")
        : connection.Execute(
              "{CALL dbo.ap_GetAccountGameCharacter(?,?,?,?,?)}");
    if (!executed) {
        return false;
    }
    if (procedureResult != 0) {
        characterId = 0;
        level = 0;
    }
    return true;
}

// L2Authd.exe 0x004176B0, Src/accountdb.cpp:3687-3695.
int AccountDB::RequestAuthnTokenByPAServer(
    CSocketServer* socket,
    LoginUser* user,
    char* appId) {
    const char result =
        PAServerMgr::Inst()->RequestAuthnToken(
            socket->serverid,
            user,
            appId);
    return result == 0
        ? 0
        : static_cast<std::int8_t>(result);
}

// L2Authd.exe 0x00417700, Src/accountdb.cpp:3710-3734.
bool AccountDB::ExternalSteamTokenLogin(
    CClientSession* client,
    int companyNo,
    unsigned char* token,
    int affiliatedGameId,
    int gameId,
    int subscription,
    unsigned short cdKind,
    char clientInstances,
    [[maybe_unused]] in_addr address) {
    PreLoginUser user{};
    user.externalUser.companyNo = companyNo;
    std::memcpy(
        user.externalUser.externalToken,
        token,
        1024);
    user.externalUser.affiliatedGameId = affiliatedGameId;
    user.externalUser.gameId = gameId;
    user.subscription = subscription;
    user.cdkind = cdKind;
    user.clientInstances = clientInstances;

    const char result =
        PAServerMgr::Inst()->LoginExternalToken(
            0,
            client,
            nullptr,
            &user);
    if (result != 0) {
        client->SendFail(1, result, nullptr);
    }
    return result != 0;
}

// L2Authd.exe 0x004177F0, Src/accountdb.cpp:3912-3924.
bool AccountDB::IsBase64ExternalToken(
    const int companyNo,
    const int affiliatedGameId) {
    if (affiliatedGameId == 7 ||
        companyNo == 8 ||
        companyNo == 10) {
        return false;
    }
    return true;
}

// L2Authd.exe 0x00417830, Src/accountdb.cpp:3927-3936.
bool AccountDB::IsExternalLoginAuthProvider(
    const char* authProviderCode) {
    return StrCmp(authProviderCode, "shanda", false) == 0 ||
           StrCmp(authProviderCode, "innova", false) == 0 ||
           StrCmp(authProviderCode, "tencent", false) == 0;
}

// L2Authd.exe 0x00417890, Src/accountdb.cpp:3939-3948.
bool AccountDB::IsExternalLoginCompanyNo(
    const int companyNo) {
    return companyNo == 4 ||
           companyNo == 8 ||
           companyNo == 10;
}

// L2Authd.exe 0x004178C0, Src/accountdb.cpp:3951-3963.
int AccountDB::GetCompanyNo(
    const char* authProviderCode) {
    if (StrCmp(authProviderCode, "shanda", false) == 0) {
        return 4;
    }
    if (StrCmp(authProviderCode, "innova", false) == 0) {
        return 8;
    }
    if (StrCmp(authProviderCode, "tencent", false) == 0) {
        return 10;
    }
    return 99999;
}

// L2Authd.exe 0x00417930, Src/accountdb.cpp:399-403.
#if defined(_WIN32)
void __stdcall QuitGameTimer(
#else
void QuitGameTimer(
#endif
    void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    accountdb.QuitGameCallback(
        static_cast<int>(
            reinterpret_cast<std::intptr_t>(parameter)));
}

// L2Authd.exe 0x00417950, Src/accountdb.cpp:439-445.
#if defined(_WIN32)
void __stdcall TimerRoutine(
#else
void TimerRoutine(
#endif
    void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    accountdb.TimerCallback(
        static_cast<int>(
            reinterpret_cast<std::intptr_t>(parameter)));
}
