#pragma once

class CObject;

namespace runtime::runtime_timing {

// Portable diagnostic sidecar. Recovered target classes keep their exact
// layouts and behavior. Tracing is enabled only by the isolated launcher and
// is bounded by a process-wide record limit.
bool Matches(CObject* first, CObject* second = nullptr) noexcept;

void Record(const char* event, CObject* actor, CObject* target, const char* detailFormat, ...);

} // namespace runtime::runtime_timing
