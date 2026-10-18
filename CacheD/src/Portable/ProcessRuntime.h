#pragma once

#include "Shared/src/Runtime/ProcessRuntime.h"

namespace runtime::process {

// Cached.exe copies its configured database/world selector into request TLS
// before dispatching timers and incoming packets.
int ConfiguredDatabaseId() noexcept;

// Shared/QsCheck.cpp is created only in CacheD and reads these CacheD
// globals in the target image.
bool IsQsDataEnabled() noexcept;
bool ExchangeQsDataEnabled(bool enabled) noexcept;
bool IsServerToCacheDPacketStatMonitorEnabled() noexcept;
bool IsCacheDToServerPacketStatMonitorEnabled() noexcept;

} // namespace runtime::process
