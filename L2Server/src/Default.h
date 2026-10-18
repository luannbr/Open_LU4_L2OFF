#pragma once

#include "Runtime.h"
#include "Shared/src/Utils.h"

#include <cstdint>

// Exact default.obj storage consumed by Shared/logsocket.cpp. Current
// CUserSocket::IncUserCount/DecUserCount at 0x00c23e00/0x00c23f04 are the
// producers. The current PE fixes all three initial values to zero.
extern volatile std::int32_t g_nServerUserCount;
extern volatile std::int32_t g_nMaxServerUserCount;
// Current default.obj storage 0x02fd4318. CConfig publishes
// World/UserLimit here and the builder userlimit pair reads/writes it.
extern volatile std::int32_t g_nServerUserCountLimit;
// Current default.obj storage 0x02fd431c. CConfig::Load publishes the
// signed World/LetBuilder value. TotalChecker2015 supplies the original
// physical spelling while the current store fixes this owner and width.
extern std::int32_t g_nLetBuilder;
// Current default.obj storage 0x02fd4310. NewAction increments it when a
// user opens a store. User/user_item own the matching close-path decrements.
extern volatile std::int32_t g_nStoreUserCount;
// Current default.obj storage 0x02fd430c. User::LeaveWorldForRaid decrements
// it when the exact m_bPlaying byte was set and forwards the result to the
// shared event/drop-tick state.
extern volatile std::int32_t g_nPlayingUserCount;
extern runtime::SystemTime g_tMaxServerUserCount;
extern bool g_bDualBillingSystem;
extern std::int32_t g_nDualBillingSystemLevel;
extern SharedUtilsWideChar g_szPremiumAccount[15];

// Current default.obj storage 0x02fd4378. Its dynamic initializer at
// 0x00d89300 publishes _time32(nullptr). CConfig::LoadSettingSection later
// replaces it with Setting/birthday_event_start at 0x005c8f7c/0x005c9f4d.
extern std::int32_t g_bBirthdayEventStart;

// Current default.obj byte 0x02fd4325, published from
// Setting/debug_hide_off. The matching donor MAP supplies the spelling.
extern bool g_bDefaultDebugHideOff;

// Current default.obj byte 0x015d0db0. The PE initializer is true.
// CConfig::LoadSettingSection later publishes Setting/ncguard at
// 0x005c9df4.
extern bool g_bInitedNCGuard;

// Current default.obj bytes 0x02fd4320/21 and 0x02fd4323/24. The PE starts
// all four at false. CConfig::LoadSettingSection publishes the normalized
// gameguard/newinspector feature and check-only switches into these owners.
extern bool g_bCheckGameGuardAuth;
extern bool g_bCheckGameGuardAuthCheckOnly;
extern bool g_bCheckNewInspectorAuth;
extern bool g_bCheckNewInspectorAuthCheckOnly;

// Exact default.obj pointer consumed by current
// L2Exception::PrintCrashSummary at 0x00728cc0/0x00728ce8. The PE initializer
// points at the writable-pointer carrier for the `HISTORY_SIMPLE` literal.
extern wchar_t* g_GuardHistoryStr;

// Current default.obj signed-long carrier at 0x015d0da8. The PE initializes
// it to two and VersionPacket publishes it in the server-status reply.
extern std::int32_t g_nServerGuardHistoryType;

// Current default.obj storage 0x015d0d04. The PE initializer is one and
// CConfig::Load publishes World/WorldId at 0x005cbb2b.
extern int g_nServerId;

// Current default.obj byte 0x015d0db3. The PE initializer is true. Current
// CCreature::AddPassingPos and the world-movement path read it directly, and
// the builder command at 0x00807a98 may toggle it at runtime.
extern bool g_bNewAddPassingPos;

// Current default.obj byte 0x02fd4327. The PE initializes it to false.
// builder command 0x00807a7d toggles it and the movement paths consume it.
extern bool g_bMoveToLeader;

// Current L2Server default.obj counter at 0x016658478. Instant-zone
// creation rejects a new instance once this reaches 90001. NPC creation and
// destruction are the other producers in the complete server.
extern volatile std::int32_t g_nNPCCount;
extern volatile std::int32_t g_nMaxNPCCount;
extern runtime::SystemTime g_tMaxNPCCount;

extern int g_nItemDropLevel1;
extern int g_nItemDropLevel2;
// Current default.obj threshold owners selected by
// FreeToPlay/ItemDropLevelPenaltyWholeServer before the shared penalty
// coefficient recomputation. Current stores close the widths. The matching
// donor supplies the original default.obj spellings.
extern int g_nAdenaDropLevel1;
extern int g_nAdenaDropLevel2;
