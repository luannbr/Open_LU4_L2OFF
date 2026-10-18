#include "Shared/src/LockStatusStack.h"

// Current Cached.exe carries the same per-thread Shared lock-status stacks as
// the other server processes. This process-owned definition supplies the one
// array consumed by Shared lock and exception code.
CLockStatusStack g_LockStack[ThreadID::PORTABLE_CAPACITY];
