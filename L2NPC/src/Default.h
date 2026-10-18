#pragma once

#include "Runtime.h"

#include <cstdint>

// PDB default.obj globals. Windows `volatile long` is always 32-bit. The
// current image uses LOCK INC/DEC only for g_nNPCCount. G_nMaxNPCCount uses
// ordinary aligned volatile reads and writes.
extern volatile std::int32_t g_nNPCCount;
extern volatile std::int32_t g_nMaxNPCCount;
extern runtime::SystemTime g_tMaxNPCCount;

// Exact default.obj/PDB global. WinMain loads it from
// [Setting] IsEventServer and GlobalObject::IsEventServer reads it directly.
extern int g_nIsEventServer;
