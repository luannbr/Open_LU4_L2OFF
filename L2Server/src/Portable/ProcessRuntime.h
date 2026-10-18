#pragma once

#include "Shared/src/Runtime/ProcessRuntime.h"

namespace runtime::process {

// Current L2Server keeps the outgoing CacheD world selector in request TLS,
// separately from the incoming database selector restored by CDBPacket.
int CurrentWorldId() noexcept;
void SetCurrentWorldId(int worldId) noexcept;

// CDoor::Delete and CItem release diagnostics capture the previous guard
// frame from the process-owned guard stack.
const wchar_t* CurrentGuardCaller() noexcept;

} // namespace runtime::process
