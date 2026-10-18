#include "Shared/src/Runtime/FixedSharedMemoryTransport.h"

#include "Shared/src/Runtime/SharedMemoryRegion.h"
#include "Shared/src/Log.h"

#include <cstdint>
#include <memory>
#include <string>

namespace portable {
namespace {

void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

std::string WideNameToUtf8(const wchar_t* name) {
    std::string result;
    if (!name) {
        return result;
    }

    for (const wchar_t* cursor = name; *cursor; ++cursor) {
        AppendUtf8(result, static_cast<std::uint32_t>(*cursor));
    }
    return result;
}

} // namespace

FixedSharedMemoryBinding OpenFixedSharedMemory(const wchar_t* name, std::size_t bytes) {
    auto mapping = std::make_unique<SharedMemoryRegion>();
    if (!mapping->AttachNamed(WideNameToUtf8(name), bytes)) {
        return {nullptr, nullptr};
    }

    void* const records = mapping->Data();
    return {mapping.release(), records};
}

FixedSharedMemoryBinding CreateFixedSharedMemory(const wchar_t* name, std::size_t bytes) {
    auto mapping = std::make_unique<SharedMemoryRegion>();
    if (!mapping->CreateNamed(WideNameToUtf8(name), bytes)) {
        return {nullptr, nullptr};
    }

    void* const records = mapping->Data();
    return {mapping.release(), records};
}

void CloseFixedSharedMemory(void* owner) { delete static_cast<SharedMemoryRegion*>(owner); }

void ReportFixedSharedArrayIndexError(int index, int count) {
    // Current L2NPC.exe 0x005356a8 keeps __FILEW__ and __LINE__ from
    // SharedType.h in this template instantiation.
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[%s][%d] index[%d] is invalid. nCount(%d)",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"SharedType.h",
    //     0x92,
    //     index,
    //     count)
    // Log.Add(LOG_ERROR,
    //     "[shared memory] Array index %d is outside [0, %d). Using element 0 instead.",
    //     index, count)
    L2_LOG_ERROR(Log, "[shared memory] Array index %d is outside [0, %d); using element 0 instead.", index, count);
}

} // namespace portable
