#pragma once

#include <string_view>

namespace authd {

// Returns true when the operator requests a normal shutdown.
bool ExecuteConsoleCommand(std::string_view line);

} // namespace authd
