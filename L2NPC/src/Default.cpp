#include "Default.h"

#include "NPCServer.h"
#include "Shared/src/LockStatusStack.h"

// default.cpp:5 in the adjacent PDB. Current EXE initializer 0x00586af8
// constructs the same process-global NPCServer at 0x00da9ea0.
NPCServer g_ns;

// default.cpp:7 in the adjacent PDB. Current EXE initializer 0x00586b18
// constructs twelve 0xea608-byte stacks beginning at 0x00dad700.
CLockStatusStack g_LockStack[ThreadID::MAX];

volatile std::int32_t g_nNPCCount = 0;
volatile std::int32_t g_nMaxNPCCount = 0;
runtime::SystemTime g_tMaxNPCCount{};
int g_nIsEventServer = 0;
