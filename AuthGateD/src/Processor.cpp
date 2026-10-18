#include "Processor.h"

#include <thread>

// AuthGateD.exe 0x00412D50, Processor.cpp.
std::uint32_t OsGetNumProcessors() {
    const std::uint32_t count = std::thread::hardware_concurrency();
    return count == 0 ? 1U : count;
}
