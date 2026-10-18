// Cross-platform transport used by the target SharedType<T> and
// CSharedArray<T, Count> templates. The target stores a Win32 mapping handle
// and mapped pointer in each carrier. Portable builds keep an opaque mapping
// owner in the handle-sized field and the same direct record pointer. Like
// OpenFileMappingW/MapViewOfFile(..., 0), attachment does not validate the
// producer mapping against the consumer's expected record extent.
#pragma once

#include <cstddef>

namespace portable {

struct FixedSharedMemoryBinding {
    void* owner;
    void* records;
};

FixedSharedMemoryBinding OpenFixedSharedMemory(const wchar_t* name, std::size_t bytes);

FixedSharedMemoryBinding CreateFixedSharedMemory(const wchar_t* name, std::size_t bytes);

void CloseFixedSharedMemory(void* owner);

void ReportFixedSharedArrayIndexError(int index, int count);

} // namespace portable
