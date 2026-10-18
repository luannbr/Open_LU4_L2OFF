#include "ZoneFatigue.h"

#include "Platform/Odbc.h"
#include "Shared/src/Runtime/Time32.h"
#include "DBConn.h"
#include "Util.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr authd::platform::OdbcReturn kSqlSuccess = 0;
constexpr authd::platform::OdbcInteger kSqlNts = -3;
constexpr authd::platform::OdbcSmallInt kStatementHandle = 3;
constexpr authd::platform::OdbcSmallInt kInput = 1;
constexpr authd::platform::OdbcSmallInt kOutput = 4;
constexpr authd::platform::OdbcSmallInt kCInt = 4;
constexpr authd::platform::OdbcSmallInt kCTinyInt = -6;
constexpr authd::platform::OdbcSmallInt kCBinary = -2;
constexpr authd::platform::OdbcSmallInt kCTimestamp = 93;
constexpr authd::platform::OdbcSmallInt kSqlInteger = 4;
constexpr authd::platform::OdbcSmallInt kSqlTinyInt = -6;
constexpr authd::platform::OdbcSmallInt kSqlBinary = -2;
constexpr authd::platform::OdbcSmallInt kSqlTimestamp = 93;

} // namespace

// L2Authd.exe 0x00499410, Src/zonefatigue.cpp:21-25.
ZoneFatigue::ZoneFatigue() {
    mGameAccountNo = 0;
    mZone = 0;
    mRemainTime = 0;
    mCapsuleCount = 0;
    mCapsuleLastUsedTime = 0;
    mLastDbIO = 0;
    std::memset(mCapsuleUsedFlag, 0, sizeof(mCapsuleUsedFlag));
    std::memset(mCapsuleId, 0, sizeof(mCapsuleId));
    std::memset(mCapsuleTimeAmount, 0, sizeof(mCapsuleTimeAmount));
}

// L2Authd.exe 0x004994B0, Src/zonefatigue.cpp:29-61.
int ZoneFatigue::ResetCapsuleInfo(unsigned char zone, const char* pCapsuleInfo) {
    if (zone >= 4 || pCapsuleInfo == nullptr) {
        return 4;
    }

    unsigned char capsuleCount = 0;
    unsigned char capsuleId[128];
    int capsuleTimeAmount[128];
    std::memset(capsuleId, 0, sizeof(capsuleId));
    std::memset(capsuleTimeAmount, 0, sizeof(capsuleTimeAmount));

    char* pOneCapsule = nullptr;
    char* pCapInfo = const_cast<char*>(pCapsuleInfo);
    while ((pOneCapsule = StrTok_R(pCapInfo, ",", &pCapInfo)) != nullptr) {
        char* pCapId = StrTok_R(pOneCapsule, ":", &pOneCapsule);
        char* pCapValue = StrTok_R(pOneCapsule, ":", &pOneCapsule);
        char* pEndPtr = nullptr;

        capsuleId[capsuleCount] = static_cast<unsigned char>(std::strtol(pCapId, &pEndPtr, 10));
        if (pCapId == pEndPtr) {
            return 4;
        }

        capsuleTimeAmount[capsuleCount] = static_cast<int>(std::strtol(pCapValue, &pEndPtr, 10));
        if (pCapValue == pEndPtr) {
            return 4;
        }
        ++capsuleCount;
    }

    const unsigned char capsuleIdSize = capsuleCount;
    const unsigned char capsuleTimeAmountSize = static_cast<unsigned char>(capsuleCount << 2U);
    return ResetCapsuleInfo(capsuleCount, zone, capsuleId, capsuleIdSize,
                            reinterpret_cast<unsigned char*>(capsuleTimeAmount), capsuleTimeAmountSize);
}

// L2Authd.exe 0x004996A0, Src/zonefatigue.cpp:65-126.
int ZoneFatigue::ResetCapsuleInfo(unsigned char capsuleCount,
                                  unsigned char zone,
                                  unsigned char* capsuleId,
                                  unsigned char capsuleIdSize,
                                  unsigned char* capsuleTimeAmount,
                                  unsigned char capsuleTimeAmountSize) {
    int result = 0;
    CDBConn conn(g_linDB);
    conn.ResetHtmt();

    authd::platform::OdbcLen cbResult = 0;
    authd::platform::OdbcLen cbZone = 0;
    authd::platform::OdbcLen cbCapsuleCount = 0;
    authd::platform::OdbcLen cbCapsuleId = capsuleIdSize;
    authd::platform::OdbcLen cbCapsuleTimeAmount = capsuleTimeAmountSize;
    authd::platform::SQLBindParameter(conn.m_stmt, 1, kOutput, kCInt, kSqlInteger, 0, 0, &result, 0, &cbResult);
    authd::platform::SQLBindParameter(conn.m_stmt, 2, kInput, kCTinyInt, kSqlTinyInt, 0, 0, &zone, 0, &cbZone);
    authd::platform::SQLBindParameter(conn.m_stmt, 3, kInput, kCTinyInt, kSqlTinyInt, 0, 0, &capsuleCount, 0,
                                      &cbCapsuleCount);
    authd::platform::SQLBindParameter(conn.m_stmt, 4, kInput, kCBinary, kSqlBinary, capsuleIdSize, 0, capsuleId,
                                      capsuleIdSize, &cbCapsuleId);
    authd::platform::SQLBindParameter(conn.m_stmt, 5, kInput, kCBinary, kSqlBinary, capsuleTimeAmountSize, 0,
                                      capsuleTimeAmount, capsuleTimeAmountSize, &cbCapsuleTimeAmount);

    char buffer[257];
    buffer[0] = 0;
    StrPrintf(buffer, 0x101, "{? = CALL dbo.ap_SZoneFatigueReset (?,?,?,?) }");
    const authd::platform::OdbcReturn RetCode = authd::platform::SQLExecDirect(conn.m_stmt, buffer, kSqlNts);
    if (RetCode == kSqlSuccess) {
        switch (result) {
        case 0:
            result = 0;
            break;
        case 1:
            result = 2;
            break;
        case 2:
        case 3:
            result = 1;
            break;
        default:
            result = 0x14;
            break;
        }
    } else {
        conn.Error(kStatementHandle, conn.m_stmt, buffer);
        result = 0x14;
    }
    conn.ResetHtmt();
    return result;
}

// L2Authd.exe 0x00499940, Src/zonefatigue.cpp:130-192.
int ZoneFatigue::UpdateRemainTime(int gameAccountNo, unsigned char zone, int remainTime) {
    int result = 0;
    mGameAccountNo = gameAccountNo;
    mZone = zone;
    mRemainTime = remainTime;

    CDBConn conn(g_linDB);
    conn.ResetHtmt();
    authd::platform::OdbcLen cbResult = 0;
    authd::platform::OdbcLen cbGameAccountNo = 0;
    authd::platform::OdbcLen cbZone = 0;
    authd::platform::OdbcLen cbRemainTime = 0;
    authd::platform::SQLBindParameter(conn.m_stmt, 1, kOutput, kCInt, kSqlInteger, 0, 0, &result, 0, &cbResult);
    authd::platform::SQLBindParameter(conn.m_stmt, 2, kInput, kCInt, kSqlInteger, 0, 0, &mGameAccountNo, 0,
                                      &cbGameAccountNo);
    authd::platform::SQLBindParameter(conn.m_stmt, 3, kInput, kCTinyInt, kSqlTinyInt, 0, 0, &mZone, 0, &cbZone);
    authd::platform::SQLBindParameter(conn.m_stmt, 4, kInput, kCInt, kSqlInteger, 0, 0, &mRemainTime, 0, &cbRemainTime);

    char buffer[257];
    buffer[0] = 0;
    StrPrintf(buffer, 0x101, "{? = CALL dbo.ap_SZoneFatigueRemainTime (?, ?, ?) }");
    const authd::platform::OdbcReturn RetCode = authd::platform::SQLExecDirect(conn.m_stmt, buffer, kSqlNts);
    if (RetCode == kSqlSuccess) {
        switch (result) {
        case 0:
            result = 0;
            break;
        case 1:
            result = 2;
            break;
        case 2:
        case 3:
            result = 1;
            break;
        default:
            result = 0x14;
            break;
        }
    } else {
        conn.Error(kStatementHandle, conn.m_stmt, buffer);
        result = 0x14;
    }
    conn.ResetHtmt();
    return result;
}

// L2Authd.exe 0x00499BE0, Src/zonefatigue.cpp:196-285. The first two
// parameters are present in the PDB signature but the selected machine code
// uses the account and zone already stored in this object.
int ZoneFatigue::UseCapsule([[maybe_unused]] int gameAccountNo,
                            [[maybe_unused]] unsigned char zone,
                            unsigned char capsuleId,
                            int totalRemainTime) {
    bool found = false;
    unsigned char capsuleIdx = 0;
    for (; capsuleIdx < mCapsuleCount; ++capsuleIdx) {
        if (capsuleId == mCapsuleId[capsuleIdx]) {
            found = true;
            break;
        }
    }

    if (!found || mCapsuleUsedFlag[capsuleIdx] == 1) {
        return 0x46;
    }

    mCapsuleUsedFlag[capsuleIdx] = 1;
    mRemainTime = totalRemainTime;

    int result = 0;
    CDBConn conn(g_linDB);
    conn.ResetHtmt();
    authd::platform::OdbcLen cbResult = 0;
    authd::platform::OdbcLen cbGameAccountNo = 0;
    authd::platform::OdbcLen cbZone = 0;
    authd::platform::OdbcLen cbRemainTime = 0;
    authd::platform::OdbcLen cbCapsuleId = 0;
    authd::platform::OdbcLen cbCapsuleUsedFlag = mCapsuleCount;
    authd::platform::SQLBindParameter(conn.m_stmt, 1, kOutput, kCInt, kSqlInteger, 0, 0, &result, 0, &cbResult);
    authd::platform::SQLBindParameter(conn.m_stmt, 2, kInput, kCInt, kSqlInteger, 0, 0, &mGameAccountNo, 0,
                                      &cbGameAccountNo);
    authd::platform::SQLBindParameter(conn.m_stmt, 3, kInput, kCTinyInt, kSqlTinyInt, 0, 0, &mZone, 0, &cbZone);
    authd::platform::SQLBindParameter(conn.m_stmt, 4, kInput, kCInt, kSqlInteger, 0, 0, &mRemainTime, 0, &cbRemainTime);
    authd::platform::SQLBindParameter(conn.m_stmt, 5, kInput, kCTinyInt, kSqlTinyInt, 0, 0, &capsuleId, 0, &cbCapsuleId);
    authd::platform::SQLBindParameter(conn.m_stmt, 6, kInput, kCBinary, kSqlBinary, mCapsuleCount, 0, mCapsuleUsedFlag,
                                      mCapsuleCount, &cbCapsuleUsedFlag);

    char buffer[257];
    buffer[0] = 0;
    StrPrintf(buffer, 0x101, "{? = CALL dbo.ap_SZoneFatigueCapsuleUse (?, ?, ?, ?, ?) }");
    const authd::platform::OdbcReturn RetCode = authd::platform::SQLExecDirect(conn.m_stmt, buffer, kSqlNts);
    if (RetCode == kSqlSuccess) {
        switch (result) {
        case 0:
            result = 0;
            break;
        case 1:
            result = 2;
            break;
        case 2:
        case 3:
            result = 1;
            break;
        default:
            result = 0x14;
            break;
        }
    } else {
        conn.Error(kStatementHandle, conn.m_stmt, buffer);
        result = 0x14;
    }
    conn.ResetHtmt();
    return result;
}

// L2Authd.exe 0x00499F90, Src/zonefatigue.cpp:289-389.
int ZoneFatigue::GetZoneFatigue(int gameAccountNo, unsigned char zone) {
    int result = 0;
    mGameAccountNo = gameAccountNo;
    mZone = zone;

    CDBConn conn(g_linDB);
    conn.ResetHtmt();
    tagTIMESTAMP_STRUCT capLastUsedTime;
    tagTIMESTAMP_STRUCT recentResetTime;
    authd::platform::OdbcLen cbResult = 0;
    authd::platform::OdbcLen cbGameAccountNo = 0;
    authd::platform::OdbcLen cbZone = 0;
    authd::platform::OdbcLen cbRemainTime = 0;
    authd::platform::OdbcLen cbCapsuleCount = 0;
    authd::platform::OdbcLen cbCapsuleFlag = kSqlNts;
    authd::platform::OdbcLen cbCapsuleId = kSqlNts;
    authd::platform::OdbcLen cbCapsuleTimeAmount = kSqlNts;
    authd::platform::OdbcLen cbLastUsedTime = 0;
    authd::platform::OdbcLen cbResetTime = 0;
    authd::platform::SQLBindParameter(conn.m_stmt, 1, kOutput, kCInt, kSqlInteger, 0, 0, &result, 0, &cbResult);
    authd::platform::SQLBindParameter(conn.m_stmt, 2, kInput, kCInt, kSqlInteger, 0, 0, &mGameAccountNo, 0,
                                      &cbGameAccountNo);
    authd::platform::SQLBindParameter(conn.m_stmt, 3, kInput, kCTinyInt, kSqlTinyInt, 0, 0, &mZone, 0, &cbZone);
    authd::platform::SQLBindParameter(conn.m_stmt, 4, kOutput, kCInt, kSqlInteger, 0, 0, &mRemainTime, 0, &cbRemainTime);
    authd::platform::SQLBindParameter(conn.m_stmt, 5, kOutput, kCTinyInt, kSqlTinyInt, 0, 0, &mCapsuleCount, 0,
                                      &cbCapsuleCount);
    authd::platform::SQLBindParameter(conn.m_stmt, 6, kOutput, kCBinary, kSqlBinary, sizeof(mCapsuleUsedFlag), 0,
                                      mCapsuleUsedFlag, sizeof(mCapsuleUsedFlag), &cbCapsuleFlag);
    authd::platform::SQLBindParameter(conn.m_stmt, 7, kOutput, kCBinary, kSqlBinary, sizeof(mCapsuleId), 0, mCapsuleId,
                                      sizeof(mCapsuleId), &cbCapsuleId);
    authd::platform::SQLBindParameter(conn.m_stmt, 8, kOutput, kCBinary, kSqlBinary, sizeof(mCapsuleTimeAmount), 0,
                                      mCapsuleTimeAmount, sizeof(mCapsuleTimeAmount), &cbCapsuleTimeAmount);
    authd::platform::SQLBindParameter(conn.m_stmt, 9, kOutput, kCTimestamp, kSqlTimestamp, sizeof(capLastUsedTime), 0,
                                      &capLastUsedTime, 0, &cbLastUsedTime);
    authd::platform::SQLBindParameter(conn.m_stmt, 10, kOutput, kCTimestamp, kSqlTimestamp, sizeof(recentResetTime), 0,
                                      &recentResetTime, 0, &cbResetTime);

    char buffer[257];
    buffer[0] = 0;
    StrPrintf(buffer, 0x101, "{? = CALL dbo.ap_GZoneFatigue (?,?,?,?,?,?,?,?,?) }");
    const authd::platform::OdbcReturn RetCode = authd::platform::SQLExecDirect(conn.m_stmt, buffer, kSqlNts);
    if (RetCode == kSqlSuccess) {
        switch (result) {
        case 0:
            if (mRemainTime == 0) {
                mCapsuleLastUsedTime = 0;
            } else {
                mCapsuleLastUsedTime = static_cast<int>(DBTimeStampToTime(capLastUsedTime));
            }
            if (mCapsuleLastUsedTime < 0x386CC4F1) {
                mCapsuleLastUsedTime = 0;
            }
            mLastDbIO = runtime::crt::Time32Now();
            result = 0;
            break;
        case 1:
        case 5:
            result = 2;
            break;
        case 2:
        case 3:
            result = 1;
            break;
        default:
            result = 0x14;
            break;
        }
    } else {
        conn.Error(kStatementHandle, conn.m_stmt, buffer);
        result = 0x14;
    }
    conn.ResetHtmt();
    return result;
}
