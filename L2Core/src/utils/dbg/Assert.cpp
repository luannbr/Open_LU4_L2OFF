#include "L2Core/src/utils/dbg/Assert.h"

#include <csignal>
#include <cstdlib>

[[noreturn]] void EASSERT() noexcept {
    // Shipping EASSERT writes through address zero. Use the matching
    // synchronous process-fatal signal portably, after restoring its default
    // disposition so an application-installed returning handler cannot let
    // execution continue past the assertion.
    std::signal(SIGSEGV, SIG_DFL);
    std::raise(SIGSEGV);

    // raise() reports failure by returning. This is also the last-resort
    // boundary on platforms whose C runtime cannot create SIGSEGV.
    std::_Exit(128 + SIGSEGV);
}
