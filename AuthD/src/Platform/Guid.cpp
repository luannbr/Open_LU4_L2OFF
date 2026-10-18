#include "Platform/Guid.h"

#include "Shared/src/Runtime/Guid.h"

namespace authd::platform {

bool ParseGuid(const char* text, _GUID& result) noexcept { return runtime::ParseGuid(text, result); }

bool IsNullGuid(const _GUID& value) noexcept { return runtime::IsNullGuid(value); }

} // namespace authd::platform
