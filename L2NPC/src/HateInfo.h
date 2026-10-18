#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <vector>


class CSharedCreatureData;

namespace layout {

inline constexpr std::size_t kHateInfoSize = 0x20;

namespace hate {
inline constexpr std::size_t creature = 0x08;
inline constexpr std::size_t hate = 0x10;
inline constexpr std::size_t temp0 = 0x18;
} // namespace hate

} // namespace layout

// The current target widened both donor-PDB integer fields to __int64 while
// retaining their source names and public placement.
class HateInfo : public ScriptAccessibleEx {
public:
    static constexpr std::int64_t MAX_HATE_VALUE = 1000000000000LL;

    HateInfo();
    HateInfo(const HateInfo&) = default;
    HateInfo(CSharedCreatureData* spCreature, std::int64_t nHateValue, std::int64_t nTempValue);
    ~HateInfo() override = default;

    void Init();
    CSharedCreatureData* GetCreature() const;
    std::int64_t HateValue() const;
    std::int64_t TestFunc(std::int64_t a);
    std::int64_t Temp() const;
    HateInfo& operator=(const HateInfo&) = default;
    HateInfo& operator+=(const HateInfo& rhs);

    void CollectFunc(ScriptTypeInfo* info) override;
    void CollectVar(ScriptTypeInfo* info) override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

    CSharedCreatureData* m_spCreature = nullptr;
    std::int64_t m_nHateValue = 0;
    std::int64_t m_nTempValue = 0;
};


class HateInfoList {
    enum {
        DEFAULT_SIZE = 5,
        MAX_SIZE = 200,
    };

public:
    HateInfoList() : mMaxSize(DEFAULT_SIZE) {}

    void SetMaxSize(int nMax);
    int GetMaxSize() const { return static_cast<int>(mMaxSize); }

    int GetCount();
    std::int64_t GetAvgHateValue();

    int AddHateInfo(
        CSharedCreatureData* spCreature, std::int64_t nHateValue, std::int64_t nTempValue, int bForce, std::int64_t bAddValue
    );
    HateInfo* GetByIndex(int nIndex);
    HateInfo* GetByCreature(CSharedCreatureData* spCreature);
    HateInfo* GetMaxElement(int byField);
    HateInfo* GetMinElement(int byField);
    HateInfo* GetNthElement(int byField, int nIndex, bool bAscendingOrder);
    HateInfo* GetClosest(const FVector& vFrom);

    void RemoveByCreature(CSharedCreatureData* spCreature);
    bool TransferByCreature(CSharedCreatureData* spFrom, CSharedCreatureData* spTo);
    void RemoveAllHateInfo();

    template <typename PRED>
    int RemoveAllIF(PRED BinaryFunctor) {
        L2_GUARD_FUNCTION();
        L2_GUARD(mLock);
        int removed = 0;
        for (auto current = mHateInfos.begin(); current != mHateInfos.end();) {
            if (BinaryFunctor(*current)) {
                current = mHateInfos.erase(current);
                ++removed;
            } else {
                ++current;
            }
        }
        return removed;
    }

    CYieldLock& GetLock() { return mLock; }

private:
    using MAP_TYPE = std::map<int, HateInfo>;
    using ITERATOR = MAP_TYPE::iterator;
    using VALUE = MAP_TYPE::value_type;
    using HateInfoVec = std::vector<HateInfo, std::allocator<HateInfo>>;
    using VecValue = HateInfo;

    std::uint64_t mMaxSize;
    CYieldLock mLock;
    MAP_TYPE mHateInfos;
};
