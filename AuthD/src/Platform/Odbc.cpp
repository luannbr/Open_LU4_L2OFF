#include "Odbc.h"

#if defined(_WIN32)

#include <sqlext.h>

// Windows ODBC headers map the unsuffixed ANSI entry points through macros.
// Keep those mappings from renaming this namespace's portable wrappers.
#ifdef SQLSetConnectAttr
#undef SQLSetConnectAttr
#endif
#ifdef SQLExecDirect
#undef SQLExecDirect
#endif
#ifdef SQLPrepare
#undef SQLPrepare
#endif
#ifdef SQLGetDiagRec
#undef SQLGetDiagRec
#endif
#ifdef SQLDriverConnect
#undef SQLDriverConnect
#endif

namespace authd::platform {

OdbcReturn SQLSetEnvAttr(
    void* environment,
    OdbcInteger attribute,
    void* value,
    OdbcInteger stringLength) {
    return static_cast<OdbcReturn>(
        ::SQLSetEnvAttr(
            environment,
            attribute,
            value,
            stringLength));
}

OdbcReturn SQLAllocHandle(
    OdbcSmallInt handleType,
    void* inputHandle,
    void** outputHandle) {
    return static_cast<OdbcReturn>(
        ::SQLAllocHandle(
            handleType,
            inputHandle,
            outputHandle));
}

OdbcReturn SQLFreeHandle(
    OdbcSmallInt handleType,
    void* handle) {
    return static_cast<OdbcReturn>(
        ::SQLFreeHandle(handleType, handle));
}

OdbcReturn SQLSetConnectAttr(
    void* connection,
    OdbcInteger attribute,
    void* value,
    OdbcInteger stringLength) {
    return static_cast<OdbcReturn>(
        ::SQLSetConnectAttrA(
            connection,
            attribute,
            value,
            stringLength));
}

OdbcReturn SQLDisconnect(void* connection) {
    return static_cast<OdbcReturn>(
        ::SQLDisconnect(connection));
}

OdbcReturn SQLEndTran(
    OdbcSmallInt handleType,
    void* handle,
    OdbcSmallInt completionType) {
    return static_cast<OdbcReturn>(
        ::SQLEndTran(
            handleType,
            handle,
            completionType));
}

OdbcReturn SQLExecDirect(
    void* statement,
    const char* text,
    OdbcInteger textLength) {
    return static_cast<OdbcReturn>(
        ::SQLExecDirectA(
            statement,
            reinterpret_cast<SQLCHAR*>(
                const_cast<char*>(text)),
            textLength));
}

OdbcReturn SQLPrepare(
    void* statement,
    const char* text,
    OdbcInteger textLength) {
    return static_cast<OdbcReturn>(
        ::SQLPrepareA(
            statement,
            reinterpret_cast<SQLCHAR*>(
                const_cast<char*>(text)),
            textLength));
}

OdbcReturn SQLExecute(void* statement) {
    return static_cast<OdbcReturn>(
        ::SQLExecute(statement));
}

OdbcReturn SQLFetch(void* statement) {
    return static_cast<OdbcReturn>(
        ::SQLFetch(statement));
}

OdbcReturn SQLGetDiagRec(
    OdbcSmallInt handleType,
    void* handle,
    OdbcSmallInt recordNumber,
    char* sqlState,
    OdbcInteger* nativeError,
    char* message,
    OdbcSmallInt messageCapacity,
    OdbcSmallInt* messageLength) {
    return static_cast<OdbcReturn>(
        ::SQLGetDiagRecA(
            handleType,
            handle,
            recordNumber,
            reinterpret_cast<SQLCHAR*>(sqlState),
            reinterpret_cast<SQLINTEGER*>(nativeError),
            reinterpret_cast<SQLCHAR*>(message),
            messageCapacity,
            messageLength));
}

OdbcReturn SQLBindCol(
    void* statement,
    OdbcUSmallInt columnNumber,
    OdbcSmallInt targetType,
    void* value,
    OdbcLen valueCapacity,
    OdbcLen* lengthOrIndicator) {
    return static_cast<OdbcReturn>(
        ::SQLBindCol(
            statement,
            columnNumber,
            targetType,
            value,
            static_cast<SQLLEN>(valueCapacity),
            reinterpret_cast<SQLLEN*>(lengthOrIndicator)));
}

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
    OdbcLen* lengthOrIndicator) {
    return static_cast<OdbcReturn>(
        ::SQLBindParameter(
            statement,
            parameterNumber,
            inputOutputType,
            valueType,
            parameterType,
            static_cast<SQLULEN>(columnSize),
            decimalDigits,
            parameterValue,
            static_cast<SQLLEN>(bufferLength),
            reinterpret_cast<SQLLEN*>(lengthOrIndicator)));
}

OdbcReturn SQLFreeStmt(
    void* statement,
    OdbcUSmallInt option) {
    return static_cast<OdbcReturn>(
        ::SQLFreeStmt(statement, option));
}

OdbcReturn SQLDriverConnect(
    void* connection,
    void* window,
    const char* input,
    OdbcSmallInt inputLength,
    char* output,
    OdbcSmallInt outputCapacity,
    OdbcSmallInt* outputLength,
    OdbcUSmallInt completion) {
    return static_cast<OdbcReturn>(
        ::SQLDriverConnectA(
            connection,
            static_cast<SQLHWND>(window),
            reinterpret_cast<SQLCHAR*>(
                const_cast<char*>(input)),
            inputLength,
            reinterpret_cast<SQLCHAR*>(output),
            outputCapacity,
            outputLength,
            completion));
}

} // namespace authd::platform

#else

#include <dlfcn.h>

#include <array>
#include <cstring>

namespace authd::platform {
namespace {

constexpr OdbcReturn kSqlError = -1;

using SetEnvAttrFn = OdbcReturn (*)(
    void*, OdbcInteger, void*, OdbcInteger);
using AllocHandleFn = OdbcReturn (*)(
    OdbcSmallInt, void*, void**);
using FreeHandleFn = OdbcReturn (*)(
    OdbcSmallInt, void*);
using SetConnectAttrFn = OdbcReturn (*)(
    void*, OdbcInteger, void*, OdbcInteger);
using DisconnectFn = OdbcReturn (*)(void*);
using EndTranFn = OdbcReturn (*)(
    OdbcSmallInt, void*, OdbcSmallInt);
using ExecDirectFn = OdbcReturn (*)(
    void*, unsigned char*, OdbcInteger);
using PrepareFn = OdbcReturn (*)(
    void*, unsigned char*, OdbcInteger);
using ExecuteFn = OdbcReturn (*)(void*);
using FetchFn = OdbcReturn (*)(void*);
using GetDiagRecFn = OdbcReturn (*)(
    OdbcSmallInt,
    void*,
    OdbcSmallInt,
    unsigned char*,
    OdbcInteger*,
    unsigned char*,
    OdbcSmallInt,
    OdbcSmallInt*);
using BindColFn = OdbcReturn (*)(
    void*,
    OdbcUSmallInt,
    OdbcSmallInt,
    void*,
    OdbcLen,
    OdbcLen*);
using BindParameterFn = OdbcReturn (*)(
    void*,
    OdbcUSmallInt,
    OdbcSmallInt,
    OdbcSmallInt,
    OdbcSmallInt,
    OdbcUInteger,
    OdbcSmallInt,
    void*,
    OdbcLen,
    OdbcLen*);
using FreeStmtFn = OdbcReturn (*)(
    void*, OdbcUSmallInt);
using DriverConnectFn = OdbcReturn (*)(
    void*,
    void*,
    unsigned char*,
    OdbcSmallInt,
    unsigned char*,
    OdbcSmallInt,
    OdbcSmallInt*,
    OdbcUSmallInt);

template <typename Function>
Function Resolve(
    void* library,
    const char* name,
    const char* alternate = nullptr) {
    if (library == nullptr) {
        return nullptr;
    }
    void* symbol = ::dlsym(library, name);
    if (symbol == nullptr && alternate != nullptr) {
        symbol = ::dlsym(library, alternate);
    }
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

struct OdbcApi {
    OdbcApi() {
        constexpr std::array<const char*, 7> candidates{
            "libodbc.so.2",
            "libodbc.so",
            "libodbc.dylib",
            "/opt/homebrew/lib/libodbc.dylib",
            "/usr/local/lib/libodbc.dylib",
            "libiodbc.dylib",
            "/usr/lib/libiodbc.dylib"};
        for (const char* candidate : candidates) {
            library = ::dlopen(
                candidate,
                RTLD_LAZY | RTLD_LOCAL);
            if (library != nullptr) {
                break;
            }
        }

        setEnvAttr = Resolve<SetEnvAttrFn>(library, "SQLSetEnvAttr");
        allocHandle = Resolve<AllocHandleFn>(library, "SQLAllocHandle");
        freeHandle = Resolve<FreeHandleFn>(library, "SQLFreeHandle");
        setConnectAttr = Resolve<SetConnectAttrFn>(
            library, "SQLSetConnectAttr", "SQLSetConnectAttrA");
        disconnect = Resolve<DisconnectFn>(library, "SQLDisconnect");
        endTran = Resolve<EndTranFn>(library, "SQLEndTran");
        execDirect = Resolve<ExecDirectFn>(
            library, "SQLExecDirect", "SQLExecDirectA");
        prepare = Resolve<PrepareFn>(
            library, "SQLPrepare", "SQLPrepareA");
        execute = Resolve<ExecuteFn>(library, "SQLExecute");
        fetch = Resolve<FetchFn>(library, "SQLFetch");
        getDiagRec = Resolve<GetDiagRecFn>(
            library, "SQLGetDiagRec", "SQLGetDiagRecA");
        bindCol = Resolve<BindColFn>(library, "SQLBindCol");
        bindParameter = Resolve<BindParameterFn>(
            library, "SQLBindParameter");
        freeStmt = Resolve<FreeStmtFn>(library, "SQLFreeStmt");
        driverConnect = Resolve<DriverConnectFn>(
            library, "SQLDriverConnect", "SQLDriverConnectA");
    }

    void* library = nullptr;
    SetEnvAttrFn setEnvAttr = nullptr;
    AllocHandleFn allocHandle = nullptr;
    FreeHandleFn freeHandle = nullptr;
    SetConnectAttrFn setConnectAttr = nullptr;
    DisconnectFn disconnect = nullptr;
    EndTranFn endTran = nullptr;
    ExecDirectFn execDirect = nullptr;
    PrepareFn prepare = nullptr;
    ExecuteFn execute = nullptr;
    FetchFn fetch = nullptr;
    GetDiagRecFn getDiagRec = nullptr;
    BindColFn bindCol = nullptr;
    BindParameterFn bindParameter = nullptr;
    FreeStmtFn freeStmt = nullptr;
    DriverConnectFn driverConnect = nullptr;
};

OdbcApi& Api() {
    static OdbcApi api;
    return api;
}

} // namespace

OdbcReturn SQLSetEnvAttr(
    void* environment,
    OdbcInteger attribute,
    void* value,
    OdbcInteger stringLength) {
    const auto function = Api().setEnvAttr;
    return function == nullptr
        ? kSqlError
        : function(
              environment,
              attribute,
              value,
              stringLength);
}

OdbcReturn SQLAllocHandle(
    OdbcSmallInt handleType,
    void* inputHandle,
    void** outputHandle) {
    const auto function = Api().allocHandle;
    return function == nullptr
        ? kSqlError
        : function(handleType, inputHandle, outputHandle);
}

OdbcReturn SQLFreeHandle(
    OdbcSmallInt handleType,
    void* handle) {
    const auto function = Api().freeHandle;
    return function == nullptr
        ? kSqlError
        : function(handleType, handle);
}

OdbcReturn SQLSetConnectAttr(
    void* connection,
    OdbcInteger attribute,
    void* value,
    OdbcInteger stringLength) {
    const auto function = Api().setConnectAttr;
    return function == nullptr
        ? kSqlError
        : function(
              connection,
              attribute,
              value,
              stringLength);
}

OdbcReturn SQLDisconnect(void* connection) {
    const auto function = Api().disconnect;
    return function == nullptr
        ? kSqlError
        : function(connection);
}

OdbcReturn SQLEndTran(
    OdbcSmallInt handleType,
    void* handle,
    OdbcSmallInt completionType) {
    const auto function = Api().endTran;
    return function == nullptr
        ? kSqlError
        : function(handleType, handle, completionType);
}

OdbcReturn SQLExecDirect(
    void* statement,
    const char* text,
    OdbcInteger textLength) {
    const auto function = Api().execDirect;
    return function == nullptr
        ? kSqlError
        : function(
              statement,
              reinterpret_cast<unsigned char*>(
                  const_cast<char*>(text)),
              textLength);
}

OdbcReturn SQLPrepare(
    void* statement,
    const char* text,
    OdbcInteger textLength) {
    const auto function = Api().prepare;
    return function == nullptr
        ? kSqlError
        : function(
              statement,
              reinterpret_cast<unsigned char*>(
                  const_cast<char*>(text)),
              textLength);
}

OdbcReturn SQLExecute(void* statement) {
    const auto function = Api().execute;
    return function == nullptr
        ? kSqlError
        : function(statement);
}

OdbcReturn SQLFetch(void* statement) {
    const auto function = Api().fetch;
    return function == nullptr
        ? kSqlError
        : function(statement);
}

OdbcReturn SQLGetDiagRec(
    OdbcSmallInt handleType,
    void* handle,
    OdbcSmallInt recordNumber,
    char* sqlState,
    OdbcInteger* nativeError,
    char* message,
    OdbcSmallInt messageCapacity,
    OdbcSmallInt* messageLength) {
    const auto function = Api().getDiagRec;
    return function == nullptr
        ? kSqlError
        : function(
              handleType,
              handle,
              recordNumber,
              reinterpret_cast<unsigned char*>(sqlState),
              nativeError,
              reinterpret_cast<unsigned char*>(message),
              messageCapacity,
              messageLength);
}

OdbcReturn SQLBindCol(
    void* statement,
    OdbcUSmallInt columnNumber,
    OdbcSmallInt targetType,
    void* value,
    OdbcLen valueCapacity,
    OdbcLen* lengthOrIndicator) {
    const auto function = Api().bindCol;
    return function == nullptr
        ? kSqlError
        : function(
              statement,
              columnNumber,
              targetType,
              value,
              valueCapacity,
              lengthOrIndicator);
}

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
    OdbcLen* lengthOrIndicator) {
    const auto function = Api().bindParameter;
    return function == nullptr
        ? kSqlError
        : function(
              statement,
              parameterNumber,
              inputOutputType,
              valueType,
              parameterType,
              columnSize,
              decimalDigits,
              parameterValue,
              bufferLength,
              lengthOrIndicator);
}

OdbcReturn SQLFreeStmt(
    void* statement,
    OdbcUSmallInt option) {
    const auto function = Api().freeStmt;
    return function == nullptr
        ? kSqlError
        : function(statement, option);
}

OdbcReturn SQLDriverConnect(
    void* connection,
    void* window,
    const char* input,
    OdbcSmallInt inputLength,
    char* output,
    OdbcSmallInt outputCapacity,
    OdbcSmallInt* outputLength,
    OdbcUSmallInt completion) {
    const auto function = Api().driverConnect;
    return function == nullptr
        ? kSqlError
        : function(
              connection,
              window,
              reinterpret_cast<unsigned char*>(
                  const_cast<char*>(input)),
              inputLength,
              reinterpret_cast<unsigned char*>(output),
              outputCapacity,
              outputLength,
              completion);
}

} // namespace authd::platform

#endif
