#pragma once

#include "Platform/WindowsTypes.h"

namespace authd::platform {

// Cross-platform boundary for the main GUID text accepted by the
// target's CLSIDFromString calls. Product owners keep the same validation and
// fallback decisions. Only the unavailable COM parser is replaced here.
bool ParseGuid(
    const char* text,
    _GUID& result) noexcept;

bool IsNullGuid(const _GUID& value) noexcept;

} // namespace authd::platform
