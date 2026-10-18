#pragma once

#include "NPProtocol.h"

#include "Shared/src/IOC.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

namespace NPSystem {

class CNPSocket;
class CSTSResponse;

using ResponseHandler = bool (*)(CNPSocket*, STSRequestTI*, CSTSResponse*);

enum ELoginStep : std::int32_t {
    LS_NONE = 0,
    LS_NP_GAMESERVER_LOGIN_COMPLETE = 1,
    LS_NP_GAMECLIENT_LOGIN_COMPLETE = 2,
    LS_NP_LOGIN_COMPLETE = 3,
    LS_MAX = 4,
};

using RequestTransactionMap = std::map<std::int32_t, STSRequestTI>;
using ResponseTransactionMap = std::map<std::int32_t, CSTSResponse*>;

extern RequestTransactionMap g_requestTransactionMap;
extern ResponseTransactionMap g_responseTransactionMap;
extern CYieldLock g_requestTransactionMapSect;
extern CYieldLock g_responseTransactionMapSect;
extern ResponseHandler g_ResponseHandler[COMMAND_MAX];

class CNPD {
public:
    using CSocketSP = CSPointer<CNPSocket>;
    using CIOBuffer = CIOBufferEx<32768>;

    CNPD();
    ~CNPD();

    bool ConnectNP();
    void CloseNP();
    bool IsValid() { return m_isConnected && static_cast<bool>(m_spNPSocket); }
    void SetClosed() { m_isConnected = false; }
    void SendCriticalMailToTC(CNPSocket* socket, bool closeSocket, const CSTSResponse* response);
    bool SendConnectServer();
    bool SendPing();
    bool SendAuthLoginTokenForServer(int accountId, const char* loginToken);
    bool SendAuthRequestTokenForClient(int accountId, const char* userGuid);
    bool SendPresenceLogout(int accountId, const char* userGuid);
    bool SendListGrades(int accountId, const char* userGuid);

    static bool Response_Dummy(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Response_AuthLoginTokenForServer(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Response_AuthRequestTokenForClient(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Notify_PresenceUserInfo(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Response_PresenceLogout(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Notify_PresenceUserLogout(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Response_ListGrades(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);
    static bool Notify_GradeChanged(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response);

    static bool LoginStep_Init(unsigned int accountId);
    static bool LoginStep_SetServerLoginComplete(unsigned int accountId, const char* userGuid);
    static bool LoginStep_SetClientLoginComplete(unsigned int accountId);
    static bool LoginStep_Logout(unsigned int accountId);
    static bool LoginStep_IsComplete(unsigned int accountId);

    bool m_isConnected;
    CSocketSP m_spNPSocket;
};

extern CNPD g_NPD;

} // namespace NPSystem

static_assert(NPSystem::LS_NONE == 0);
static_assert(NPSystem::LS_NP_GAMESERVER_LOGIN_COMPLETE == 1);
static_assert(NPSystem::LS_NP_GAMECLIENT_LOGIN_COMPLETE == 2);
static_assert(NPSystem::LS_NP_LOGIN_COMPLETE == 3);
static_assert(NPSystem::LS_MAX == 4);
