#pragma once

namespace runtime {

// Install once before starting console workers. Poll from the normal run loop.
void InstallConsoleSignalHandlers();
bool ConsoleStopRequested() noexcept;

} // namespace runtime
