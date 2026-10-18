#pragma once

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

class User;

class CRelationManager : public Singleton<CRelationManager> {
public:
    static constexpr std::size_t kSlotCount = 8192;

    CRelationManager() : m_nStart(0), m_nEnd(static_cast<std::int32_t>(kSlotCount)), m_pEmptySlot{}, m_pRelationTable{} {
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
            m_pEmptySlot[slot] = slot;
        }
    }

    int GetEmptySlot() {
        if (m_nStart >= m_nEnd) {
            return -1;
        }

        const std::int32_t index = runtime::raw_atomic::IncrementI32(&m_nStart) - 1;
        const std::uint32_t slot = m_pEmptySlot[index % static_cast<std::int32_t>(kSlotCount)];
        InitializeRelation(slot);
        return static_cast<int>(slot);
    }

    void ReturnSlot(unsigned int slot) {
        if (slot == static_cast<unsigned int>(-1)) {
            return;
        }
        const std::int32_t index = runtime::raw_atomic::IncrementI32(&m_nEnd) - 1;
        m_pEmptySlot[index % static_cast<std::int32_t>(kSlotCount)] = slot;
    }

    void InitializeRelation(unsigned int slot) {
        if (slot >= kSlotCount) {
            return;
        }

        std::memset(m_pRelationTable[slot], 0, kSlotCount);
        for (std::size_t other = 0; other < kSlotCount; ++other) {
            m_pRelationTable[other][slot] = 0;
        }
    }

    unsigned char GetRelation(User* user, User* target);
    unsigned char GetCachedRelation(unsigned int userSlot, unsigned int targetSlot) {
        return m_pRelationTable[userSlot][targetSlot];
    }
    void SetRelation(unsigned int userSlot, unsigned int targetSlot, unsigned char relation) {
        m_pRelationTable[userSlot][targetSlot] = relation;
    }
    static CRelationManager& GetGlobalRelaitonManager() { return Singleton<CRelationManager>::inst(); }

private:
    std::int32_t m_nStart;
    std::int32_t m_nEnd;
    std::uint32_t m_pEmptySlot[kSlotCount];
    std::uint8_t m_pRelationTable[kSlotCount][kSlotCount];
};
