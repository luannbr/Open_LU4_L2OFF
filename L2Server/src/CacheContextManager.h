#pragma once

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TemplateHelper.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>


// Current L2Server's fixed CacheD request-context ring. The two counters
// are monotonically increasing tickets. M_pEmptySlot maps those tickets to
// one of the 100000 fixed 0x20-byte context records.
class CCacheContextManager : public Singleton<CCacheContextManager> {
public:
    static constexpr std::int64_t kContextCount = 100000;
    static constexpr std::size_t kContextSize = 0x20;

    CCacheContextManager() :
        m_nStart(0),
        m_nEnd(kContextCount),
        m_pEmptySlot{},
        m_CacheContextTable{},
        m_nLastRegisteredSlot(-1),
        m_alignment36ee9c{} {
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kContextCount); ++slot) {
            m_pEmptySlot[slot] = slot;
        }
    }

    int GetLastRegisteredSlot() const noexcept {
        // The adjacent L2Server PDB surface records this public method, and
        // current startup inlines the exact +0x36ee98 read at 0x0080a80f.
        return m_nLastRegisteredSlot;
    }

    int RegisterContext(const char* format, ...) {
        if (runtime::raw_atomic::LoadI64Relaxed(&m_nStart) >= runtime::raw_atomic::LoadI64Relaxed(&m_nEnd)) {
            return -1;
        }

        const std::int64_t ticket = runtime::raw_atomic::AddI64(&m_nStart, 1) - 1;
        const std::size_t emptyIndex = static_cast<std::size_t>(ticket % kContextCount);
        const std::int32_t slot = m_pEmptySlot[emptyIndex];
        m_pEmptySlot[emptyIndex] = -1;

        va_list arguments;
        va_start(arguments, format);
        ::VAssemble(m_CacheContextTable[slot], static_cast<int>(kContextSize), format, arguments);
        va_end(arguments);

        m_nLastRegisteredSlot = slot;
        return slot;
    }

    bool GetContext(int slot, const char* format, ...) {
        // The signed lower-bound oddity is current: only -1 is rejected.
        if (slot == -1 || slot >= static_cast<std::int32_t>(kContextCount)) {
            return false;
        }

        va_list arguments;
        va_start(arguments, format);
        ::VDisassemble(reinterpret_cast<const unsigned char*>(m_CacheContextTable[slot]), format, arguments);
        va_end(arguments);

        const std::int64_t ticket = runtime::raw_atomic::AddI64(&m_nEnd, 1) - 1;
        m_pEmptySlot[static_cast<std::size_t>(ticket % kContextCount)] = slot;
        return true;
    }

    bool GetContextWithoutRelease(int slot, const char* format, ...) {
        // Current L2Server 0x006a2e84. The paged warehouse loader must
        // keep this context until the final CacheD package arrives.
        // The signed lower-bound oddity is identical to GetContext.
        if (slot == -1 || slot >= static_cast<std::int32_t>(kContextCount)) {
            return false;
        }

        va_list arguments;
        va_start(arguments, format);
        ::VDisassemble(reinterpret_cast<const unsigned char*>(m_CacheContextTable[slot]), format, arguments);
        va_end(arguments);
        return true;
    }

    bool ReleaseContext(int slot) {
        // Current LoadWarehouseItemsPacket inlines this ring publication at
        // each terminal page. Only -1 is rejected on the signed side.
        if (slot == -1 || slot >= static_cast<std::int32_t>(kContextCount)) {
            return false;
        }

        const std::int64_t ticket = runtime::raw_atomic::AddI64(&m_nEnd, 1) - 1;
        m_pEmptySlot[static_cast<std::size_t>(ticket % kContextCount)] = slot;
        return true;
    }

private:
    std::int64_t m_nStart; // +0x000008
    std::int64_t m_nEnd; // +0x000010
    std::int32_t m_pEmptySlot[kContextCount]; // +0x000018
    char m_CacheContextTable[kContextCount][kContextSize]; // +0x061a98
    std::int32_t m_nLastRegisteredSlot; // +0x36ee98
    [[maybe_unused]] std::byte m_alignment36ee9c[0x04];
};
