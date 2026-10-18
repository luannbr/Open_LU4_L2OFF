#pragma once

#include <cstdint>

struct HWND__;
struct HINSTANCE__;
struct HFONT__;
struct HBRUSH__;

struct WindowRect32 {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

struct WindowSize32 {
    std::int32_t cx = 0;
    std::int32_t cy = 0;
};
