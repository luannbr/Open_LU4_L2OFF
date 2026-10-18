#pragma once

#include <cstdint>

namespace TransformType {

enum Enum : std::int32_t {
    NotTransform = 0,
    NonCombat = 1,
    Combat = 2,
    Cursed = 3,
    ModeChange = 4,
    Riding = 5,
    Flying = 6,
    PureStat = 7,
    Max = 8,
};

} // namespace TransformType
