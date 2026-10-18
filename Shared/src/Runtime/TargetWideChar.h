#pragma once

// The Win64 source used 16-bit wchar_t for these boundaries. Game text is
// char16_t on every host, including Windows where wchar_t is a distinct type.
using SharedUtilsWideChar = char16_t;
