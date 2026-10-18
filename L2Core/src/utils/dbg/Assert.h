#pragma once

// L2Core/utils/dbg/Assert.h in the donor source exposes this process-fatal
// assertion boundary. The shipping L2NPC body faults and
// cannot return to its caller.
[[noreturn]] void EASSERT() noexcept;
