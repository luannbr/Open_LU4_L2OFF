#pragma once

#include "Skill.h"

#include <cstdint>

enum TraitCaterogyEnum : std::int32_t {
    TRAIT_CATEGORY_INVALID = 0,
    TRAIT_CATEGORY_WEAPON = 1,
    TRAIT_CATEGORY_RACE = 2,
    TRAIT_CATEGORY_STATUS = 3,
    TRAIT_CATEGORY_MAX = 4,
};

extern TraitCaterogyEnum g_TraitCateroty[TRAIT_MAX];
