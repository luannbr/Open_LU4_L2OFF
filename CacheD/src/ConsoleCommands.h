#pragma once

#include <functional>
#include <string_view>

namespace cached::console {

// Returns false for rejected commands or incomplete database changes.
bool HandleCommand(std::string_view line, const std::function<void()>& requestStop = {});

} // namespace cached::console
