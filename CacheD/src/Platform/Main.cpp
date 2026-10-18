#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Cached.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (!runtime::InitializeDirectoryArguments(argc, argv)) return 2;

    // Cached.exe completes 31 ordered class-id initializers before WinMain.
    // Portable registration has already populated the actual class count.
    // g_nClassCount = 31
    const int exitCode = CacheDMain(argc, argv);

    // Characters and items were saved when L2Server disconnected; what is left
    // is cache memory. Destroying it in static destruction order crashed: the
    // account map is destroyed after the item cache and the log it still uses
    // (CWareHouse::~CWareHouse -> CItem::Delete -> g_itemCache), and the crash
    // handler then kept the process alive until stop-all ended it by force.
    // End here, after the log is written out, like the original process exit.
    Log.FlushLogFile();
    std::fflush(nullptr);
    std::_Exit(exitCode);
}
