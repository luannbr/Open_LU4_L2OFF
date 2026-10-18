#pragma once

#include "Shared/src/Runtime/ProcessRuntime.h"

namespace l2npc::portable {

// L2NPC cannot continue once L2Server or CacheD closes its connection: the NPC
// state belongs to that peer and there is no reconnect path. The original
// forced a crash report at this point. Flush the logs and end the process with
// this exit code instead. The process ends without running destructors, so the
// closed socket owners are not released a second time by the normal shutdown.
inline constexpr int kRequiredPeerClosedExitCode = 3;

[[noreturn]] void ExitAfterRequiredPeerClosed();

} // namespace l2npc::portable
