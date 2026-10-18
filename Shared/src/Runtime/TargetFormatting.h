#pragma once

#include <cstdint>
#include <string>

namespace runtime {

// Preserve the 32-bit pointer carrier used by the shipping diagnostics when
// the portable host has wider native pointers.
inline std::uint32_t PointerLow32(const void* pointer) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// Recovered log-field builders all serialize signed decimal values as ASCII
// code units in the target's UTF-16 carrier.
inline std::u16string DecimalUtf16(std::int64_t value) {
    const std::string decimal = std::to_string(value);
    return std::u16string(decimal.begin(), decimal.end());
}

} // namespace runtime
