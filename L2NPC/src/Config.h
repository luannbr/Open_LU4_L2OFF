#pragma once

#include "Shared/src/Config.h"

#include <vector>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <netinet/in.h>
#endif

// Exact one-byte config.cpp owner. All state loaded by this class is
// process-global in the target.
class CConfig {
public:
    CConfig();
    ~CConfig();

    void Load();
    void SaveTimerOverflow();
};


extern CConfig config;

// Exact config.cpp / npcserver.cpp process globals. These are deliberately
// in the global namespace: that is how they are compiled in L2NPC.exe and in
// the matching NASC64_2015 PDB.
extern bool g_bFreeOpenServer;
extern double g_dF2PQuestRewardPenaltyPer;
extern std::vector<int> g_vF2PRewardPenaltyQuests;
extern bool g_bUseServerStartMail;
extern bool g_bRunTestCase;
extern bool g_bUseResourceLoader;
extern bool g_bShowTestCaseProgress;
extern bool g_bTraceNpcAI;
extern bool g_bTraceGlobalObjectAI;
extern bool g_bEnableDirectCommand;
extern bool g_bSetReportNpcEventCount;
extern bool g_bAddTimerCheck;
extern int g_nAddTimerCheckCount;

// Exact PDB/current-image globals. The portable composition root converts
// cacheServerAddr to text only at its socket transport boundary.
extern in_addr cacheServerAddr;
extern short cacheServerPort;

// config.cpp:49-60. The target tokenizes the supplied buffer in place and
// appends every semicolon-delimited _wtoi result to the process vector.
void MakePenaltyRewardQuestList(wchar_t* questIds);

// Exact CConfig globals consumed by CNPC::AsyncTimerExpired1.
extern int g_TimerOverflowLimit;
extern int g_TimerOverflowCheckTick;

// Process globals populated by CConfig::Load in the shipping executable.
extern int g_nRaidServerID;

// Process-global value written by CConfig::Load from
// [Setting]/FastEnterworld in the shipping executable.
extern bool g_bFastEnterworld;
extern bool g_bSpawnLog;
extern bool g_bScriptDebugLog;

// Exact process globals consumed directly by NPC.cpp, DesirePQ.cpp,
// user.cpp and serversocket.cpp.
extern int g_nMaxChaseCount;
extern int g_nUseChasePenalty;

// Matching NASC64_2015 PDB places these CConfig::Load-owned globals in
// config.cpp. The npcpos spawn-definition constructors consume them.
extern bool g_bBossRespawnFactorEnabled;
extern int g_nBossRespawnFactor;
