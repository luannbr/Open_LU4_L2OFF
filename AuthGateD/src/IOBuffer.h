#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class CIOBuffer {
public:
    static constexpr std::size_t kCapacity = 0x2000;

    CIOBuffer() = default;
    ~CIOBuffer() = default;

    static CIOBuffer* Alloc();
    void Free();

    char m_buffer[kCapacity]{};
    std::uint32_t m_size = 0;
    CIOBuffer* m_pNext = nullptr;
    std::atomic<std::int32_t> m_ref{0};
};
