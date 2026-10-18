#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

namespace runtime {

bool ParseGuid(const char* text, _GUID& result) noexcept;
bool IsNullGuid(const _GUID& value) noexcept;

} // namespace runtime
