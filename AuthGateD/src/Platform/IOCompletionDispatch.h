#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>

class CIOObject;

namespace authgated::platform {

// CIOObject itself has only a deleting destructor in the target vtable.
// CIOSocket and CIOServer add the same +4 OnIOCallback slot.
// RTTI selects that concrete owner without altering either original class.
void DispatchIOCompletion(
    CIOObject* object,
    int success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped);

// The work port carries CClientPacket and CAuthPacket objects. Those two
// target classes expose the same +4 OnIOCallback vtable slot.
// keep that queue's concrete-owner selection separate from socket IO.
void DispatchWorkCompletion(
    CIOObject* object,
    int success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped);

} // namespace authgated::platform
