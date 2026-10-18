#pragma once

#include "DirectLog.h"

#include <string_view>

namespace runtime {

enum class UserInterface { Console, Gui };

inline UserInterface ReadUserInterface(std::string_view value) {
    if (value == "console") return UserInterface::Console;
    if (value == "gui") {
#if defined(_WIN32)
        return UserInterface::Gui;
#else
        L2_LOG_DIRECT(stderr, logging::Level::Warning,
            "[config] UserInterface=gui is only supported on Windows. Using console.");
        return UserInterface::Console;
#endif
    }
    L2_LOG_DIRECT(stderr, logging::Level::Warning,
        "[config] UserInterface must be console or gui. Using console.");
    return UserInterface::Console;
}

} // namespace runtime
