#pragma once

#include <string>

// The original owner is Shared/Utils.cpp. This narrow declaration boundary
// prevents users of the current 0x005c85cc string helper from importing the
// unrelated legacy logging, stream and NPC configuration surfaces that are
// still co-located in the recovered portable Utils.h.
namespace Utils {

void CopyStrInLowerCase(const char16_t* input, char16_t* output);
void CopyStrInLowerCase(const wchar_t* input, wchar_t* output);

// Current Shared/Utils.cpp helper at Cached.exe 0x004c851c. Its original
// identifier is absent from the available PDB, so the semantic name records
// the observable operation instead of inventing a source spelling.
std::u16string UnescapeSqlApostrophes(const char16_t* input);

// Current Shared/Utils.cpp helper at Cached.exe 0x004c8224. It inserts one
// additional apostrophe after every apostrophe in the source string.
std::wstring EscapeSqlApostrophes(const wchar_t* input);

} // namespace Utils
