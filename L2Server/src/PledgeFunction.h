#pragma once

#include "PledgeFunctionInfo.h"
#include "Shared/src/Obj.h"

#include <cstddef>
#include <cstdint>

class PledgeMastery;


namespace PledgeFunctionStuff {

bool ValidContidion(int level, const PledgeMastery* pMastery, const PledgeConditionInfo& Condition);

} // namespace PledgeFunctionStuff

class PledgeFunction : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(PledgeFunction)

    struct Builder {
        static void Enable(PledgeFunction* function, PledgeFunctionType::Value type);
        static void Disable(PledgeFunction* function, PledgeFunctionType::Value type);
        static std::uint32_t GetBits(const PledgeFunction* function) noexcept;
    };

    PledgeFunction();
    ~PledgeFunction() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void UpdateFunction(int level, const PledgeMastery* pMastery);
    bool IsEnable(PledgeFunctionType::Value type) const;
    int GetMaxMemberCount() const noexcept { return m_nMaxMemberCount; }
    int GetMaxEliteCount() const noexcept { return m_nMaxEliteCount; }

private:
    int m_nMaxMemberCount; // +0x18
    int m_nMaxEliteCount; // +0x1c
    std::uint32_t m_Bits; // +0x20
    [[maybe_unused]] std::uint32_t m_alignment24To28{};

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(PledgeFunction)
};
