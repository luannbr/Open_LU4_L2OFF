#pragma once

#include <string>

// Exact matching-PDB enum and values from Shared/SystemMail.h.
enum MailType : int {
    MAIL_MIN = -1,
    MAIL_CRASH = 0,
    MAIL_UNITTEST_FAILED = 1,
    MAIL_UNITTEST_SUCCESS = 2,
    MAIL_CONVERT_INT64_FAILED = 3,
    MAIL_CANNOT_FIND_SP = 4,
    MAIL_CRITICAL_CALLSTACK = 5,
    MAIL_NOT_SET_THREAD_LOCAL_WORLD_ID = 6,
    MAIL_SERVER_START_COMPLETED = 7,
    MAIL_NOT_SET_DB_LIST = 8,
    RESET_PROFILE = 9,
    REPORT_PROFILE = 10,
    MAIL_L2SERVER_CONFIG = 11,
    MAIL_NP_CRITICAL_MESSAGE = 12,
    MAIL_TIMER_CRITICAL_MESSAGE = 13,
    MAIL_MAX = 14,
};

// Exact process global. WinMain allocates its 1000-byte storage after loading
// CConfig, just as current L2NPC.exe does.
extern char* mailServer;

class CommonMailMessage {
public:
    CommonMailMessage(wchar_t* fileName, MailType mailType, const char* host) :
        m_MailType(mailType), m_pFileName(fileName), m_host(host) {}

    MailType GetMailType() const { return m_MailType; }
    const char* GetHost() const { return m_host; }

    // Current CacheD.exe 0x0040ec64.
    virtual wchar_t* GetFileName();
    // Current CacheD.exe 0x0059a774.
    virtual int SendMail();
    virtual void CreateContents(void* file) = 0;

private:
    MailType m_MailType;
    wchar_t* m_pFileName;
    const char* m_host;
};


class CrashMailMessage : public CommonMailMessage {
public:
    CrashMailMessage(const char* host, const wchar_t* file) :
        CommonMailMessage(const_cast<wchar_t*>(L""), MAIL_CRASH, host), m_file(file) {}

    // Current CacheD.exe 0x0059a898.
    int SendMail() override;
    // Current CacheD.exe 0x0047b248.
    void CreateContents(void*) override {}

private:
    const wchar_t* m_file;
};


#if defined(L2_BUILD_CACHED)
// The exact name, field order and 0x48 layout come from the matching
// NASC64_2015 Shared/SystemMail PDB. Current CacheD RTTI and the
// 0x00599b48/0x0059a9d0 bodies confirm this creation.
class CannotFindSPMailMessage : public CommonMailMessage {
public:
    CannotFindSPMailMessage(
        const char* host,
        const wchar_t* storedProcedure,
        const wchar_t* sqlState,
        int nativeError,
        const wchar_t* errorMessage,
        const wchar_t* catalogName
    ) :
        CommonMailMessage(const_cast<wchar_t*>(L"CannotFileSP.txt"), MAIL_CANNOT_FIND_SP, host),
        m_sName(storedProcedure),
        m_SQLState(sqlState),
        m_errMsg(errorMessage),
        m_catalogName(catalogName),
        m_native(nativeError) {}

    // Current CacheD.exe 0x00599b48.
    void CreateContents(void* file) override;

private:
    const wchar_t* m_sName;
    const wchar_t* m_SQLState;
    const wchar_t* m_errMsg;
    const wchar_t* m_catalogName;
    int m_native;
};

#endif

class CriticalCallstackMessage : public CommonMailMessage {
public:
    explicit CriticalCallstackMessage(const char* host) :
        CommonMailMessage(const_cast<wchar_t*>(L"CriticalCallStack.txt"), MAIL_CRITICAL_CALLSTACK, host) {}

    // Current CacheD.exe 0x00599ad0.
    void CreateContents(void* file) override;
};


#if !defined(L2_BUILD_CACHED)
class NotSetThreadLocalWorldIDMessage : public CommonMailMessage {
public:
    explicit NotSetThreadLocalWorldIDMessage(const char* host) :
        CommonMailMessage(const_cast<wchar_t*>(L"NotSetThreadLocalWorldID.txt"), MAIL_NOT_SET_THREAD_LOCAL_WORLD_ID, host) {}

    void CreateContents(void* file) override;
};

#endif

class ProfileResetMessage : public CommonMailMessage {
public:
    explicit ProfileResetMessage(const char* host) :
        CommonMailMessage(const_cast<wchar_t*>(L"ProfileResetMessage.txt"), RESET_PROFILE, host) {}

    // Current CacheD.exe 0x00599afc.
    void CreateContents(void* file) override;
};


class ProfileReportMessage : public CommonMailMessage {
public:
    explicit ProfileReportMessage(const char* host) :
        CommonMailMessage(const_cast<wchar_t*>(L"ProfileReportMessage.txt"), REPORT_PROFILE, host) {}

    // Current CacheD.exe 0x00599b0c.
    void CreateContents(void* file) override;
};


#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
class ServerStartCompletedMessage : public CommonMailMessage {
public:
    explicit ServerStartCompletedMessage(const char* host) :
        CommonMailMessage(const_cast<wchar_t*>(L"ServerStartCompleted.txt"), MAIL_SERVER_START_COMPLETED, host) {}

    // Current CacheD.exe 0x00599c40.
    void CreateContents(void* file) override;
};

#endif

#if defined(L2_BUILD_L2SERVER)
class L2ServerINISettingMessage : public CommonMailMessage {
public:
    explicit L2ServerINISettingMessage(const char* host) :
        CommonMailMessage(const_cast<wchar_t*>(L"L2ServerINISettingMessage.txt"), MAIL_L2SERVER_CONFIG, host) {}

    void CreateContents(void* file) override;
};


class NPCriticalMessage : public CommonMailMessage {
public:
    NPCriticalMessage(const char* host, const char* message, const char* npUserGuid) :
        CommonMailMessage(const_cast<wchar_t*>(L"NPCriticalMessage.txt"), MAIL_NP_CRITICAL_MESSAGE, host),
        m_message(message),
        m_npUserGuid(npUserGuid) {}

    void CreateContents(void* file) override;

private:
    const char* m_message;
    const char* m_npUserGuid;
};

#endif

// This target-visible helper is compiled as a public symbol by the matching
// PDB even though it is implemented in SystemMail.cpp.
std::string GetTitleBody(MailType mailType);

namespace SystemMail {

// Matching Shared PDB return type. Current CacheD.exe 0x00599d8c.
int SetReceivers(const char* receivers);
#if !defined(L2_BUILD_CACHED)
int sendNotSetThreadLocalWorldID(const char* server);
#endif
int sendCriticalCallstack(const char* server);
int sendResetProfile(const char* server);
int sendReportProfile(const char* server);
#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
int sendServerStartCompleted(const char* server);
#endif
#if defined(L2_BUILD_L2SERVER)
int sendL2ServerINISetting(const char* server);
int sendNPCriticalMessage(const char* server, const char* message, const char* npUserGuid);
#endif
#if defined(L2_BUILD_CACHED)
int sendCannotFindSPMail(
    const char* server,
    const wchar_t* storedProcedure,
    const wchar_t* sqlState,
    int nativeError,
    const wchar_t* errorMessage,
    const wchar_t* catalogName
);
#endif
int sendCrashMail(const char* server, const wchar_t* attachmentPath);

} // namespace SystemMail
