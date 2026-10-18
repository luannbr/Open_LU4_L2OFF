#pragma once

#include "NPProtocol.h"

#include <cstdint>

struct cstring_compare {
    bool operator()(const char* left, const char* right) const;
};

namespace NPSystem {

extern const char* const PROTOCOL_VERSION;
extern const char* const CRLF;
extern const char* const CRLFCRLF;
extern const char HEADER_NVSEP;

extern const char* g_szMethods[METHOD_MAX];
extern const char* g_szHeaderNamesShort[HEADERNAME_MAX];
extern const char* g_szHeaderNamesFull[HEADERNAME_MAX];
extern const char* g_szProtocolNames[PROTOCOL_MAX];
extern const char* g_szCommandNames[COMMAND_MAX][2];
extern const std::int32_t g_nCommandToProtocol[COMMAND_MAX][2];

using CStringToIdMap = std::map<const char*, std::int32_t, ::cstring_compare>;

extern CStringToIdMap g_MethodToIdMap;
extern CStringToIdMap g_HeaderShortNameToIdMap;
extern CStringToIdMap g_HeaderFullNameToIdMap;
extern CStringToIdMap g_szProtocolNameToIdMap;
extern CStringToIdMap g_szSimpleCommandNameToIdMap;
extern CStringToIdMap g_szFullCommandNameToIdMap;

bool Initialize();

extern char g_pServerAppKey[64];
extern char g_pClientAppKey[64];
extern char g_pAppGroupCode[64];
extern std::int32_t g_nProgramId;
extern char g_szClientAddr[128];
extern volatile std::uint32_t s_nLastRequestTransactionNO;

} // namespace NPSystem
