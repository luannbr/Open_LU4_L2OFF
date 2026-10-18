#pragma once

#include <cstdint>

// Original SDK owner: sqltypes.h. L2Authd.pdb type 0x104C, sizeof 0x10.
// Keep the Win32 ODBC spelling and fixed-width layout at the portable ODBC
// boundary instead of duplicating the SDK value type in product headers.
#if defined(_WIN32)
#include "Platform/WindowsTypes.h"
#include <sqltypes.h>
#else
struct tagTIMESTAMP_STRUCT {
    std::int16_t year;
    std::uint16_t month;
    std::uint16_t day;
    std::uint16_t hour;
    std::uint16_t minute;
    std::uint16_t second;
    std::uint32_t fraction;
};

using TIMESTAMP_STRUCT = tagTIMESTAMP_STRUCT;
#endif


namespace authd::platform {

// Original ODBC handles are opaque pointers. SQLLEN/SQLULEN follow the host
// ODBC ABI: they are 32-bit in the selected x86 image and pointer-width in
// unixODBC/iODBC on a 64-bit portable host.
using OdbcReturn = std::int16_t;
using OdbcSmallInt = std::int16_t;
using OdbcUSmallInt = std::uint16_t;
using OdbcInteger = std::int32_t;
using OdbcUInteger = std::uintptr_t;
using OdbcLen = std::intptr_t;

OdbcReturn SQLSetEnvAttr(
    void* environment,
    OdbcInteger attribute,
    void* value,
    OdbcInteger stringLength);

OdbcReturn SQLAllocHandle(
    OdbcSmallInt handleType,
    void* inputHandle,
    void** outputHandle);

OdbcReturn SQLFreeHandle(
    OdbcSmallInt handleType,
    void* handle);

OdbcReturn SQLSetConnectAttr(
    void* connection,
    OdbcInteger attribute,
    void* value,
    OdbcInteger stringLength);

OdbcReturn SQLDisconnect(void* connection);

OdbcReturn SQLEndTran(
    OdbcSmallInt handleType,
    void* handle,
    OdbcSmallInt completionType);

OdbcReturn SQLExecDirect(
    void* statement,
    const char* text,
    OdbcInteger textLength);

OdbcReturn SQLPrepare(
    void* statement,
    const char* text,
    OdbcInteger textLength);

OdbcReturn SQLExecute(void* statement);
OdbcReturn SQLFetch(void* statement);

OdbcReturn SQLGetDiagRec(
    OdbcSmallInt handleType,
    void* handle,
    OdbcSmallInt recordNumber,
    char* sqlState,
    OdbcInteger* nativeError,
    char* message,
    OdbcSmallInt messageCapacity,
    OdbcSmallInt* messageLength);

OdbcReturn SQLBindCol(
    void* statement,
    OdbcUSmallInt columnNumber,
    OdbcSmallInt targetType,
    void* value,
    OdbcLen valueCapacity,
    OdbcLen* lengthOrIndicator);

OdbcReturn SQLBindParameter(
    void* statement,
    OdbcUSmallInt parameterNumber,
    OdbcSmallInt inputOutputType,
    OdbcSmallInt valueType,
    OdbcSmallInt parameterType,
    OdbcUInteger columnSize,
    OdbcSmallInt decimalDigits,
    void* parameterValue,
    OdbcLen bufferLength,
    OdbcLen* lengthOrIndicator);

OdbcReturn SQLFreeStmt(
    void* statement,
    OdbcUSmallInt option);

OdbcReturn SQLDriverConnect(
    void* connection,
    void* window,
    const char* input,
    OdbcSmallInt inputLength,
    char* output,
    OdbcSmallInt outputCapacity,
    OdbcSmallInt* outputLength,
    OdbcUSmallInt completion);

} // namespace authd::platform
