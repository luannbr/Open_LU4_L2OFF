#pragma once

#include <cstdint>

// Original owner: Src/thread.h / Src/thread.cpp. `long` in the selected x86
// PDB is represented as 32 bits on LP64 portable hosts.
extern void* g_hIOCompletionPort;
extern void* g_hIOCompletionPortInt;
extern volatile std::int32_t g_nRunningThread;
extern bool g_bTerminating;
extern void** g_hServerThread;
extern void** g_hServerThreadInt;
extern unsigned int* g_nServerThreadId;
extern unsigned int* g_nServerThreadIntId;

unsigned int TimerThread(void* param);
unsigned int IOThreadServer(void* param);
unsigned int IOThreadInt(void* param);
unsigned int ListenThread(void* param);
int CreateIOThread();
