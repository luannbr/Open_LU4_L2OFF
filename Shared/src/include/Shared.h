// Shared.h  -  umbrella header for the common server Shared target.
//
// Include this to pull in the whole shared surface: the portable runtime, the
// libpq DB layer, packet framing/serialization/configuration helpers,
// the wire opcode contract, and the confirmed Shared/wire-DTO contracts.
//
// CMake consumers link Shared. Its public include roots are propagated by the
// target instead of being reconstructed in each executable.
#pragma once

// ---- portable runtime (SystemTime / GetTickCount / Log / EngineLock) -----------
#include "Runtime.h"

// ---- database layer (recovered DBConn/CSqlPoolManager owners over libpq) --------
#include "DBConn.h"

// ---- network stack --------------------------------------------------------------
#include "Config.h" // l2cache.ini reader
#include "Protocol.h" // exact 2-byte length + opcode framing + dispatch
#include "Serialize.h" // Assemble / Disassemble / ParseByFmt / EmitByFmt

// ---- wire protocol contract -----------------------------------------------------
#include "OpcodeTableGen.h" // opcode -> handler-name/format map (188 requests)
#include "Wire.h" // EtcMani wire-DTO format contract
