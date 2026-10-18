#pragma once

#include "IOObject.h"
#include "Lock.h"

#include <cstdint>

#if defined(_WIN32)
std::intptr_t CALLBACK LoginDlgProc(
    HWND window,
    unsigned int message,
    std::uintptr_t wParam,
    std::intptr_t lParam);
#endif

class CDBConn;

// Original owner: Src/dbconn.h. L2Authd.pdb type 0x11B7, target size 0x138.
// The Win32 ODBC environment handle is opaque at the portable boundary. All
// product-visible methods, inheritance and fields follow field list 0x11B5.
class DBEnv : public CIOObject {
public:
    DBEnv();
    ~DBEnv() override;

    void OnEventCallback() override {}
    void OnTimerCallback() override;
    void OnIOCallback(
        int,
        std::uint32_t,
        _OVERLAPPED*) override {}

    bool Init(int connectionCount, int environmentType);
    bool LoadConnStr();
    bool LoadConnStrFromReg();
    void SaveConnStrToReg();
    bool TestConnect();
    std::int32_t GetActiveConn() {
        return m_connCount;
    }
    void IncrementActiveConn();
    void DecrementActiveConn();

protected:
    friend class CDBConn;
#if defined(_WIN32)
    friend std::intptr_t CALLBACK LoginDlgProc(
        HWND window,
        unsigned int message,
        std::uintptr_t wParam,
        std::intptr_t lParam);
#endif

    CLock m_lock;
    void* m_henv;
    unsigned char m_connStr[256];
    std::int32_t m_connCount;
    bool m_timerStatus;
    std::int32_t m_envType;
};

// Original owner: Src/dbconn.h. L2Authd.pdb type 0x1043 records a 0x2018-byte
// Win32 object. The handles remain transport-neutral pointers in the portable
// build. The statement buffer, counters, visibility and overload set follow
// field list 0x1042 exactly.
class CDBConn {
public:
    explicit CDBConn(DBEnv& environment);
    ~CDBConn();

    bool SetAutoCommit(bool enabled);
    bool EndTran(short completionType);
    bool Execute(const char* statement, ...);
    bool ExecuteIndirect(const char* statement, ...);
    bool ExecuteInsert(const char* statement, ...);
    bool ExecuteInsertIndirect(const char* statement, ...);
    bool ExecuteDelete(const char* statement, ...);
    bool Fetch(bool& noMoreRows);
    void Error(
        short handleType,
        void* handle,
        const char* statement);
    void ErrorExceptInsert(
        short handleType,
        void* handle,
        const char* statement);
    void SetColumnNumber(int column);

    void Bind(char* value, int capacity);
    void Bind(char* value);
    void Bind(unsigned char* value);
    void Bind(unsigned int* value);
    void Bind(int* value);
    void Bind(short* value);

    void SetParamNumber(int parameter);
    void BindParam(short* value, short length);
    void BindParam(int* value, short length);
    void BindParam(unsigned int* value, short length);
    void BindParam(unsigned char* value, short length);
    void BindParam(char* value, short length);
    void BindParam(char* value, int capacity, short length);

    void ResetHtmt();
    void CloseCursor();
    bool GetConnection();
    bool IsConnected() {
        return m_isConnected;
    }

    static std::int32_t GetAvgTimeCost() noexcept {
        return g_nAvgTimeCost;
    }

    void* m_stmt;             // target +0x0000
    std::int32_t m_colNum;    // target +0x0004
    std::int32_t m_paramNum;  // target +0x0008

protected:
    static std::int32_t g_nAvgTimeCost;

    void* m_hdbc;             // target +0x000c
    DBEnv* m_pEnv;            // target +0x0010
    bool m_isConnected;       // target +0x0014
    char m_szStatement[8192]; // target +0x0015
};

extern DBEnv g_linDB;
extern DBEnv g_secuDB;
extern DBEnv g_GloActNcsDB;
