#include "Default.h"
#include "NpcServer.h"
#include "Protocol/src/L2Constants.h"
#include "Relay.h"
#include "Server.h"
#include "Shared/src/LockStatusStack.h"
#include "Shared/src/Runtime/Time32.h"

// Current default.obj CRT order is significant: construct the base server
// before its NpcServer peer, then the lock-stack array and birthday clock.
// Their registered teardown order is so NpcServer before Server.
Server g_server;
NpcServer g_npcserver;

// default.obj-owned current L2Server storage: 32 lock-status stacks at
// 0x02fd4540, one 0xea608-byte stack per ThreadID slot.
CLockStatusStack g_LockStack[ThreadID::MAX];

volatile std::int32_t g_nServerUserCount = 0;
volatile std::int32_t g_nMaxServerUserCount = 0;
volatile std::int32_t g_nServerUserCountLimit = 0;
std::int32_t g_nLetBuilder = 1;
volatile std::int32_t g_nStoreUserCount = 0;
volatile std::int32_t g_nPlayingUserCount = 0;
runtime::SystemTime g_tMaxServerUserCount{};
bool g_bDualBillingSystem = false;
std::int32_t g_nDualBillingSystemLevel = 85;
SharedUtilsWideChar g_szPremiumAccount[15]{};
std::int32_t g_bBirthdayEventStart = runtime::crt::Time32Now();
bool g_bDefaultDebugHideOff = false;
bool g_bInitedNCGuard = true;
bool g_bCheckGameGuardAuth = false;
bool g_bCheckGameGuardAuthCheckOnly = false;
bool g_bCheckNewInspectorAuth = false;
bool g_bCheckNewInspectorAuthCheckOnly = false;
int g_nServerId = 1;
bool g_bNewAddPassingPos = true;
bool g_bMoveToLeader = false;
// Current default.obj BSS byte. The reconnect_relay_server builder command
// is its only writer. The startup connect worker consumes the zero default.
bool g_bConnectRelayServer = false;
volatile std::int32_t g_nNPCCount = 0;
int g_nItemDropLevel1 = 5;
int g_nItemDropLevel2 = 10;
int g_nAdenaDropLevel1 = 8;
int g_nAdenaDropLevel2 = 15;

// Current L2Server.exe default.obj storage 0x015d0d08 contains the pointer
// 0x00f8a120, whose complete UTF-16 payload is `HISTORY_SIMPLE`.
wchar_t* g_GuardHistoryStr = const_cast<wchar_t*>(L"HISTORY_SIMPLE");
std::int32_t g_nServerGuardHistoryType = 2;
