#include "NPConst.h"

#include "NPSystem.h"

#include "Shared/src/MemorySub.h"

#include <cstddef>
#include <cstring>

namespace {

void* AllocatePugiMemory(std::size_t size) {
    if (size == 0) {
        return nullptr;
    }
    return g_Memory.Malloc(size, true, nullptr, 0);
}

void FreePugiMemory(void* allocation) {
    if (allocation) {
        g_Memory.Free(allocation, 0);
    }
}

} // namespace

bool cstring_compare::operator()(const char* left, const char* right) const { return std::strcmp(left, right) < 0; }

namespace NPSystem {

const char* const PROTOCOL_VERSION = "STS/1.0";
const char* const CRLF = "\r\n";
const char* const CRLFCRLF = "\r\n\r\n";
const char HEADER_NVSEP = ':';

const char* g_szMethods[METHOD_MAX] = {
    "GET",
    "POST",
};

const char* g_szHeaderNamesShort[HEADERNAME_MAX] = {
    "a",
    "e",
    "l",
    "c",
    "f",
    "r",
    "p",
    "m",
    "s",
    "z",
    "t",
    "v",
    "o",
    "g",
    "n",
    "k",
    "x",
};

const char* g_szHeaderNamesFull[HEADERNAME_MAX] = {
    "accept",
    "content-encoding",
    "content-length",
    "content-type",
    "from",
    "refer-to",
    "reply-to",
    "stream",
    "subject",
    "date",
    "to",
    "via",
    "accept-encoding",
    "accept-language",
    "content-language",
    "geolocation",
    "transfer-encoding",
};

const char* g_szProtocolNames[PROTOCOL_MAX] = {
    "Gate",
    "Auth",
    "SecondPwd",
    "Sts",
    "Game",
    "Presence",
    "Mtx",
    "Slot",
    "License",
    "Friend",
    "Account",
    "Grade",
};

const char* g_szCommandNames[COMMAND_MAX][2] = {
    {"Connect", "/Sts/Connect"},
    {"Ping", "/Sts/Ping"},
    {"Auth", "/Auth/LoginToken"},
    {"Auth", "/Auth/RequestToken"},
    {"Presence", "/Presence/UserInfo"},
    {"Presence", "/Presence/Logout"},
    {"Presence", "/Presence/UserLogout"},
    {"Grade", "/Grade/ListGrades"},
    {"Grade", "/Grade/NotifyGradeChanged"},
};

const std::int32_t g_nCommandToProtocol[COMMAND_MAX][2] = {
    {COMMAND_STS_CONNECT, PROTOCOL_STS},
    {COMMAND_STS_PING, PROTOCOL_STS},
    {COMMAND_AUTH_LOGINTOKENFORSERVER, PROTOCOL_AUTH},
    {COMMAND_AUTH_REQUESTTOKENFORCLIENT, PROTOCOL_AUTH},
    {NOTIFY_PRESENCE_USERINFO, PROTOCOL_PRESENCE},
    {COMMAND_PRESENCE_LOGOUT, PROTOCOL_PRESENCE},
    {NOTIFY_PRESENCE_USERLOGOUT, PROTOCOL_PRESENCE},
    {COMMAND_LIST_GRADE, PROTOCOL_GRADE},
    {NOTIFY_GRADE_CHANGED, PROTOCOL_GRADE},
};

CStringToIdMap g_MethodToIdMap;
CStringToIdMap g_HeaderShortNameToIdMap;
CStringToIdMap g_HeaderFullNameToIdMap;
CStringToIdMap g_szProtocolNameToIdMap;
CStringToIdMap g_szSimpleCommandNameToIdMap;
CStringToIdMap g_szFullCommandNameToIdMap;

bool Initialize() {
    g_MethodToIdMap.clear();
    g_HeaderShortNameToIdMap.clear();
    g_HeaderFullNameToIdMap.clear();
    g_szProtocolNameToIdMap.clear();
    g_szSimpleCommandNameToIdMap.clear();
    g_szFullCommandNameToIdMap.clear();

    {
        L2_GUARD(g_requestTransactionMapSect);
        g_requestTransactionMap.clear();
    }
    for (auto& transaction : g_responseTransactionMap) {
        delete transaction.second;
    }
    g_responseTransactionMap.clear();

    for (std::int32_t method = METHOD_MIN; method < METHOD_MAX; ++method) {
        g_MethodToIdMap.emplace(g_szMethods[method], method);
    }
    for (std::int32_t header = HEADERNAME_MIN; header < HEADERNAME_MAX; ++header) {
        g_HeaderShortNameToIdMap.emplace(g_szHeaderNamesShort[header], header);
        g_HeaderFullNameToIdMap.emplace(g_szHeaderNamesFull[header], header);
    }
    for (std::int32_t protocol = PROTOCOL_MIN; protocol < PROTOCOL_MAX; ++protocol) {
        g_szProtocolNameToIdMap.emplace(g_szProtocolNames[protocol], protocol);
    }
    for (std::int32_t command = COMMAND_MIN; command < COMMAND_MAX; ++command) {
        g_szSimpleCommandNameToIdMap.emplace(g_szCommandNames[command][0], command);
        g_szFullCommandNameToIdMap.emplace(g_szCommandNames[command][1], command);
    }

    pugi::set_memory_management_functions(&AllocatePugiMemory, &FreePugiMemory);
    return true;
}

char g_pServerAppKey[64]{};
char g_pClientAppKey[64]{};
char g_pAppGroupCode[64]{};
std::int32_t g_nProgramId = 0;
char g_szClientAddr[128]{};
volatile std::uint32_t s_nLastRequestTransactionNO = 0;

} // namespace NPSystem
