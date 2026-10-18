#pragma once

#include "Shared/src/IOCPacket.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <limits>

// PDB/target-vtable-confirmed packet assembly interface from
// Shared/packethelper.h.
class IPacketHelper {
public:
    virtual ~IPacketHelper() = default;

    virtual char* GetPacket() = 0;
    virtual int GetBufferSize() const = 0;
    virtual int GetLength() const = 0;
    virtual void SetLength(int length) = 0;
    virtual int Assemble(const char* format, ...) = 0;
    virtual void Reset() = 0;
    virtual int GetAssembledCount() = 0;
};

namespace packet_helper_detail {

inline int AssemblePacket(char* buffer, int capacity, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = ::VAssemble(buffer, capacity, format, arguments);
    va_end(arguments);
    return result;
}

} // namespace packet_helper_detail

template <std::size_t Capacity>
class CPacketHelperEx : public IPacketHelper {
    static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<int>::max()));

public:
    char* GetPacket() override {
        // Current Cached.exe 0x0040fcc8 returns this + 8, the first byte
        // after the vptr.
        return m_buffer.data();
    }

    int GetBufferSize() const override {
        // Current CPacketHelperEx<20480> vtable slot: Cached.exe
        // 0x004133d0 returns 0x5000. The same +0x10 slot for
        // CPacketHelperEx<8192> is 0x005a0c80 and returns 0x2000.
        return static_cast<int>(Capacity);
    }

    int GetLength() const override {
        // Current Cached.exe 0x0040fcd0 reads +0x5008. The 8192-byte
        // specialization at 0x0059d8ac reads +0x2008.
        return m_length;
    }

    void SetLength(int length) override {
        // Both current specializations use the source's fixed 0x5000 packet
        // ceiling here. In particular, L2NPC.exe 0x00522360 also clamps
        // CPacketHelperEx<8192> to 0x4fff instead of Capacity - 1. Current
        // Cached.exe 0x004133ac is the 20480-byte specialization and
        // 0x005a0c54 is the exact 8192-byte specialization.
        m_length = std::min(length, 0x4fff);
    }

    // Non-virtual source factoring for callers that already own a va_list.
    // It adds no target vtable slot or object state. Current CacheD
    // CPackageItemPacketManager::PackageAssemble at 0x004f2370 inlines this
    // exact operation against its current package helper, including the
    // assembled-count update.
    int AssembleV(const char* format, va_list arguments) {
        const int assembled =
            ::VAssemble(m_buffer.data() + m_length, static_cast<int>(Capacity) - m_length, format, arguments);

        if (assembled > 0) {
            m_length += assembled;
            ++m_assembledCount;
        }
        return assembled;
    }

    int Assemble(const char* format, ...) override {
        // Current Cached.exe 0x0040fcd8 forwards the remaining buffer and
        // the incoming va_list to Shared/ioc.cpp::VAssemble, then updates
        // both fields only for a positive result.
        va_list arguments;
        va_start(arguments, format);
        const int assembled = AssembleV(format, arguments);
        va_end(arguments);
        return assembled;
    }

    void Reset() override {
        // Current Cached.exe 0x004133c0 clears the +0x5008/+0x500c pair.
        // 0x005a0c68 clears the +0x2008/+0x200c pair.
        m_length = 0;
        m_assembledCount = 0;
    }

    int GetAssembledCount() override {
        // Current Cached.exe 0x0040fd24 reads +0x500c. 0x005a0c78 reads
        // the 8192-byte specialization's count at +0x200c.
        return m_assembledCount;
    }

private:
    std::array<char, Capacity> m_buffer{};
    int m_length = 0;
    int m_assembledCount = 0;
};


// The target constructs the 0x5010-byte packet-helper base and then replaces
// only its vptr with this derived class's table. With zero length it supplies
// the same non-null buffer/zero-length pair used by DB requests without item
// manipulation data.
class CPacketHelperNullObject final : public CPacketHelperEx<20480> {
public:
    ~CPacketHelperNullObject() override = default;
};
