#include "L2Core/src/inc/MiniDump.h"

namespace {

// Current L2Server.exe 0x01600420. MiniDump.obj owns this initialized
// process flag. DontMakeDumpFile is its sole shutdown-reachable consumer.
bool g_bMakeDumpFile = true;

} // namespace

bool EXCEPTION::CMiniDump::DontMakeDumpFile() {
    // Current L2Server.exe 0x00ce8150, size 10.
    g_bMakeDumpFile = false;
    return false;
}
