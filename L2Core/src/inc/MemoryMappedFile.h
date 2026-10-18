#pragma once

#include <cstdint>

struct _SECURITY_ATTRIBUTES;

// L2Core/memorymappedfile.cpp, matching PDB signature. The implementation
// owns the named mutex used to reject a second live owner of the same MMF.
void* L2CreateFileMapping(void* hFile,
                          _SECURITY_ATTRIBUTES* lpAttributes,
                          unsigned long flProtect,
                          unsigned long dwMaximumSizeHigh,
                          unsigned long dwMaximumSizeLow,
                          wchar_t* lpName);

// Volatile __int64 in the PDB. L2CreateFileMapping updates it with a
// lock-prefixed operation and the memory-pool report prints it in MiB.
extern volatile std::int64_t g_MemMapAlloc;
