#include "L2NPC/src/HateInfo.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/ScriptTypeInfo.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace {

using HateInfoMapValue = std::map<int, HateInfo>::value_type;

std::int64_t AddAndClamp(std::int64_t left, std::int64_t right) {
    const std::int64_t sum = runtime::AddInt64Wrapping(left, right);
    return sum > HateInfo::MAX_HATE_VALUE ? HateInfo::MAX_HATE_VALUE : sum;
}

struct HateInfoHateLess {
    bool operator()(const HateInfoMapValue& left, const HateInfoMapValue& right) const {
        return left.second.HateValue() < right.second.HateValue();
    }
};

struct HateInfoTempLess {
    bool operator()(const HateInfoMapValue& left, const HateInfoMapValue& right) const {
        return left.second.Temp() < right.second.Temp();
    }
};

struct HateInfoHateLess1 {
    bool operator()(const HateInfo& left, const HateInfo& right) const { return left.HateValue() < right.HateValue(); }
};

struct HateInfoTempLess1 {
    bool operator()(const HateInfo& left, const HateInfo& right) const { return left.Temp() < right.Temp(); }
};

template <typename PRED>
void TargetSwap(HateInfo* left, HateInfo* right, PRED) {
    if (left == right) {
        return;
    }
    const HateInfo temporary = *left;
    *left = *right;
    *right = temporary;
}

template <typename PRED>
void TargetMedianOfThree(HateInfo* first, HateInfo* middle, HateInfo* last, PRED pred) {
    if (pred(*middle, *first)) {
        TargetSwap(middle, first, pred);
    }
    if (pred(*last, *middle)) {
        TargetSwap(last, middle, pred);
    }
    if (pred(*middle, *first)) {
        TargetSwap(middle, first, pred);
    }
}

template <typename PRED>
void TargetChooseMedian(HateInfo* first, HateInfo* middle, HateInfo* last, PRED pred) {
    HateInfo* firstMedian = first;
    HateInfo* lastMedian = last;
    if (last - first > 40) {
        const std::ptrdiff_t step = (last - first + 1) / 8;
        firstMedian = first + step;
        TargetMedianOfThree(first, firstMedian, first + step * 2, pred);
        TargetMedianOfThree(middle - step, middle, middle + step, pred);
        lastMedian = last - step;
        TargetMedianOfThree(last - step * 2, lastMedian, last, pred);
    }
    TargetMedianOfThree(firstMedian, middle, lastMedian, pred);
}

template <typename PRED>
std::pair<HateInfo*, HateInfo*> TargetUnguardedPartition(HateInfo* first, HateInfo* last, PRED pred) {
    HateInfo* middle = first + (last - first) / 2;
    TargetChooseMedian(first, middle, last - 1, pred);

    HateInfo* pivotFirst = middle;
    HateInfo* pivotLast = pivotFirst + 1;
    while (first < pivotFirst && !pred(*(pivotFirst - 1), *pivotFirst) && !pred(*pivotFirst, *(pivotFirst - 1))) {
        --pivotFirst;
    }
    while (pivotLast < last && !pred(*pivotLast, *pivotFirst) && !pred(*pivotFirst, *pivotLast)) {
        ++pivotLast;
    }

    HateInfo* scanRight = pivotLast;
    HateInfo* scanLeft = pivotFirst;
    for (;;) {
        for (; scanRight < last; ++scanRight) {
            if (pred(*scanRight, *pivotFirst)) {
                break;
            }
            if (!pred(*pivotFirst, *scanRight)) {
                TargetSwap(pivotLast, scanRight, pred);
                ++pivotLast;
            }
        }
        for (; first < scanLeft; --scanLeft) {
            if (pred(*pivotFirst, *(scanLeft - 1))) {
                break;
            }
            if (!pred(*(scanLeft - 1), *pivotFirst)) {
                --pivotFirst;
                TargetSwap(pivotFirst, scanLeft - 1, pred);
            }
        }

        if (scanLeft == first && scanRight == last) {
            return {pivotFirst, pivotLast};
        }
        if (scanLeft == first) {
            if (pivotLast != scanRight) {
                TargetSwap(pivotFirst, pivotLast, pred);
            }
            ++pivotLast;
            TargetSwap(pivotFirst, scanRight, pred);
            ++pivotFirst;
            ++scanRight;
        } else if (scanRight == last) {
            --scanLeft;
            --pivotFirst;
            TargetSwap(scanLeft, pivotFirst, pred);
            --pivotLast;
            TargetSwap(pivotFirst, pivotLast, pred);
        } else {
            --scanLeft;
            TargetSwap(scanRight, scanLeft, pred);
            ++scanRight;
        }
    }
}

template <typename PRED>
void TargetInsertionSort(HateInfo* first, HateInfo* last, PRED pred) {
    if (first == last) {
        return;
    }
    for (HateInfo* next = first + 1; next != last; ++next) {
        const HateInfo value = *next;
        if (pred(value, *first)) {
            for (HateInfo* destination = next; destination != first; --destination) {
                *destination = *(destination - 1);
            }
            *first = value;
            continue;
        }
        HateInfo* destination = next;
        while (pred(value, *(destination - 1))) {
            *destination = *(destination - 1);
            --destination;
        }
        *destination = value;
    }
}

template <typename PRED>
void TargetPushHeap(HateInfo* first, std::ptrdiff_t hole, std::ptrdiff_t top, const HateInfo& value, PRED pred) {
    while (top < hole) {
        const std::ptrdiff_t parent = (hole - 1) / 2;
        if (!pred(first[parent], value)) {
            break;
        }
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = value;
}

template <typename PRED>
void TargetAdjustHeap(HateInfo* first, std::ptrdiff_t hole, std::ptrdiff_t count, const HateInfo& value, PRED pred) {
    const std::ptrdiff_t top = hole;
    std::ptrdiff_t secondChild = hole * 2 + 2;
    while (secondChild < count) {
        if (pred(first[secondChild], first[secondChild - 1])) {
            --secondChild;
        }
        first[hole] = first[secondChild];
        hole = secondChild;
        secondChild = hole * 2 + 2;
    }
    if (secondChild == count) {
        first[hole] = first[count - 1];
        hole = count - 1;
    }
    TargetPushHeap(first, hole, top, value, pred);
}

template <typename PRED>
void TargetHeapSort(HateInfo* first, HateInfo* last, PRED pred) {
    const std::ptrdiff_t count = last - first;
    for (std::ptrdiff_t hole = count / 2; 0 < hole;) {
        --hole;
        const HateInfo value = first[hole];
        TargetAdjustHeap(first, hole, count, value, pred);
    }
    for (HateInfo* heapLast = last; 1 < heapLast - first;) {
        const HateInfo value = *(heapLast - 1);
        *(heapLast - 1) = *first;
        --heapLast;
        TargetAdjustHeap(first, 0, heapLast - first, value, pred);
    }
}

template <typename PRED>
void TargetMsvc2010Sort(HateInfo* first, HateInfo* last, std::ptrdiff_t ideal, PRED pred) {
    std::ptrdiff_t count = last - first;
    if (32 < count) {
        do {
            if (ideal < 1) {
                break;
            }
            const auto middle = TargetUnguardedPartition(first, last, pred);
            ideal = ideal / 2 + (ideal / 2) / 2;
            if (middle.first - first < last - middle.second) {
                TargetMsvc2010Sort(first, middle.first, ideal, pred);
                first = middle.second;
            } else {
                TargetMsvc2010Sort(middle.second, last, ideal, pred);
                last = middle.first;
            }
            count = last - first;
        } while (32 < count);
        if (32 < count) {
            TargetHeapSort(first, last, pred);
            return;
        }
    }
    if (1 < count) {
        TargetInsertionSort(first, last, pred);
    }
}

template <typename PRED>
void TargetMsvc2010Sort(std::vector<HateInfo, std::allocator<HateInfo>>& values, PRED pred) {
    TargetMsvc2010Sort(values.data(), values.data() + values.size(), static_cast<std::ptrdiff_t>(values.size()), pred);
}

struct _SumHelper {
    explicit _SumHelper(int result) : mResult(result) {}

    void operator()(const HateInfoMapValue& value) {
        mResult = runtime::AddInt64Wrapping(mResult, value.second.HateValue());
    }

    std::int64_t result() const { return mResult; }

    std::int64_t mResult;
};

HateInfo* CopyToTempObject(const HateInfo& source) {
    HateInfo* const destination = Singleton<ThreadLocalStorage<TempObjectPool<HateInfo, 32>, 8>>::inst()->Alloc();
    if (!destination) {
        return nullptr;
    }
    *destination = source;
    return destination;
}

bool _LessDistCube(const FVector& vPos, CSharedCreatureData* c0, CSharedCreatureData* c1) {
    const FVector& leftPosition = c0->m_Pos;
    const FVector& rightPosition = c1->m_Pos;
    const double leftX = leftPosition.x - vPos.x;
    const double leftY = leftPosition.y - vPos.y;
    const double leftZ = leftPosition.z - vPos.z;
    const double rightX = rightPosition.x - vPos.x;
    const double rightY = rightPosition.y - vPos.y;
    const double rightZ = rightPosition.z - vPos.z;
    return leftX * leftX + leftY * leftY + leftZ * leftZ < rightX * rightX + rightY * rightY + rightZ * rightZ;
}

} // namespace

HateInfo::HateInfo() { Init(); }

HateInfo::HateInfo(CSharedCreatureData* spCreature, std::int64_t nHateValue, std::int64_t nTempValue) :
    m_spCreature(spCreature), m_nHateValue(nHateValue), m_nTempValue(nTempValue) {}

void HateInfo::Init() {
    m_spCreature = nullptr;
    m_nHateValue = 0;
    m_nTempValue = 0;
}

CSharedCreatureData* HateInfo::GetCreature() const {
    // Current L2NPC.exe 0x00552750.
    return m_spCreature;
}

std::int64_t HateInfo::HateValue() const {
    // Current L2NPC.exe 0x00552758.
    return m_nHateValue;
}

std::int64_t HateInfo::TestFunc(std::int64_t a [[maybe_unused]]) { return 0; }

std::int64_t HateInfo::Temp() const { return m_nTempValue; }

HateInfo& HateInfo::operator+=(const HateInfo& rhs) {
    m_nHateValue = AddAndClamp(m_nHateValue, rhs.m_nHateValue);
    m_nTempValue = AddAndClamp(m_nTempValue, rhs.m_nTempValue);
    return *this;
}

void HateInfo::CollectFunc(ScriptTypeInfo* info) {
    ScriptTypeInfo& int64Type = GetBuiltinScriptType(ScriptTypeId::Int64);
    info->AddFunction(L"HateValue", &HateInfo::HateValue, &int64Type, {});
    info->AddFunction(L"TestFunc", &HateInfo::TestFunc, &int64Type, {&int64Type});
}

void HateInfo::CollectVar(ScriptTypeInfo* info) {
    const auto addVariable = [info](const wchar_t* name, int offset, ScriptTypeId typeId) {
        auto* const variable = new ScriptVarInfo;
        variable->SetScriptVarName(name);
        variable->Offset(offset);
        variable->Type(&GetBuiltinScriptType(typeId));
        info->AddVarInfo(variable);
    };
    addVariable(L"creature", 0x08, ScriptTypeId::CreatureData);
    addVariable(L"hate", 0x10, ScriptTypeId::Int64);
    addVariable(L"temp0", 0x18, ScriptTypeId::Int64);
}

ScriptAccessibleEx* HateInfo::GetScriptAccessibleEx() const { return const_cast<HateInfo*>(this); }

void HateInfoList::SetMaxSize(int nMax) {
    L2_GUARD(mLock);
    if (nMax < 1) {
        nMax = DEFAULT_SIZE;
    } else if (nMax > MAX_SIZE) {
        nMax = MAX_SIZE;
    }
    mMaxSize = static_cast<std::uint64_t>(nMax);
}

int HateInfoList::GetCount() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    return static_cast<int>(mHateInfos.size());
}

std::int64_t HateInfoList::GetAvgHateValue() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (mHateInfos.empty()) {
        return 0;
    }

    _SumHelper summer(0);
    summer = std::for_each(mHateInfos.begin(), mHateInfos.end(), summer);
    const std::uint64_t total = static_cast<std::uint64_t>(summer.result());
    const std::uint64_t average = total / static_cast<std::uint64_t>(mHateInfos.size());
    std::int64_t result = 0;
    static_assert(sizeof(result) == sizeof(average));
    std::memcpy(&result, &average, sizeof(result));
    return result;
}

int HateInfoList::AddHateInfo(
    CSharedCreatureData* spCreature, std::int64_t nHateValue, std::int64_t nTempValue, int bForce, std::int64_t bAddValue
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (!spCreature) {
        return -1;
    }

    const int key = spCreature->index;
    ITERATOR lb = mHateInfos.find(key);
    if (lb == mHateInfos.end()) {
        if (mHateInfos.size() < mMaxSize || bForce != 0) {
            mHateInfos.insert(VALUE(key, HateInfo(spCreature, nHateValue, nTempValue)));
        }
        if (mMaxSize < mHateInfos.size()) {
            const ITERATOR iMin = std::min_element(mHateInfos.begin(), mHateInfos.end(), HateInfoHateLess{});
            if (iMin != mHateInfos.end()) {
                mHateInfos.erase(iMin);
            }
        }
    } else if (bAddValue == 0) {
        lb->second = HateInfo(spCreature, nHateValue, nTempValue);
    } else {
        lb->second += HateInfo(spCreature, nHateValue, nTempValue);
    }
    return static_cast<int>(mHateInfos.size());
}

HateInfo* HateInfoList::GetByIndex(int nIndex) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (nIndex < 0 || static_cast<std::size_t>(nIndex) >= mHateInfos.size()) {
        return nullptr;
    }
    ITERATOR iInfo = mHateInfos.begin();
    std::advance(iInfo, nIndex);
    return CopyToTempObject(iInfo->second);
}

HateInfo* HateInfoList::GetByCreature(CSharedCreatureData* spCreature) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (!spCreature || mHateInfos.empty()) {
        return nullptr;
    }
    const ITERATOR iInfo = mHateInfos.find(spCreature->index);
    return iInfo == mHateInfos.end() ? nullptr : CopyToTempObject(iInfo->second);
}

HateInfo* HateInfoList::GetMaxElement(int byField) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (mHateInfos.empty()) {
        return nullptr;
    }

    ITERATOR iMax;
    if (byField == 0) {
        iMax = std::max_element(mHateInfos.begin(), mHateInfos.end(), HateInfoHateLess{});
    } else if (byField == 1) {
        iMax = std::max_element(mHateInfos.begin(), mHateInfos.end(), HateInfoTempLess{});
    } else {
        return nullptr;
    }
    return iMax == mHateInfos.end() ? nullptr : CopyToTempObject(iMax->second);
}

HateInfo* HateInfoList::GetMinElement(int byField) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (mHateInfos.empty()) {
        return nullptr;
    }

    ITERATOR iMin;
    if (byField == 0) {
        iMin = std::min_element(mHateInfos.begin(), mHateInfos.end(), HateInfoHateLess{});
    } else if (byField == 1) {
        iMin = std::min_element(mHateInfos.begin(), mHateInfos.end(), HateInfoTempLess{});
    } else {
        return nullptr;
    }
    return iMin == mHateInfos.end() ? nullptr : CopyToTempObject(iMin->second);
}

HateInfo* HateInfoList::GetNthElement(int byField, int nIndex, bool bAscendingOrder) {
    L2_GUARD_FUNCTION();
    std::optional<HateInfoVec> vectorStorage;
    {
        L2_GUARD(mLock);
        if (mHateInfos.empty()) {
            return nullptr;
        }

        auto& hateInfoVec = vectorStorage.emplace();
        for (ITERATOR it = mHateInfos.begin(); it != mHateInfos.end(); ++it) {
            hateInfoVec.push_back(it->second);
        }
    }

    auto& hateInfoVec = *vectorStorage;
    if (byField == 0) {
        TargetMsvc2010Sort(hateInfoVec, HateInfoHateLess1{});
    } else if (byField == 1) {
        TargetMsvc2010Sort(hateInfoVec, HateInfoTempLess1{});
    } else {
        return nullptr;
    }

    const int nMaxIndex = static_cast<int>(hateInfoVec.size()) - 1;
    const int selected =
        bAscendingOrder ? std::min(nIndex, nMaxIndex) : std::max(runtime::SubtractInt32Wrapping(nMaxIndex, nIndex), 0);
    return CopyToTempObject(hateInfoVec[static_cast<std::size_t>(selected)]);
}

HateInfo* HateInfoList::GetClosest(const FVector& vFrom) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (mHateInfos.empty()) {
        return nullptr;
    }

    ITERATOR iFirst = mHateInfos.begin();
    ITERATOR iFound = iFirst;
    const ITERATOR iLast = mHateInfos.end();
    for (++iFirst; iFirst != iLast; ++iFirst) {
        HateInfo& hateInfo = iFound->second;
        const double bestZ = hateInfo.GetCreature()->m_Pos.z;
        const int nZdiff = runtime::AbsoluteInt32Wrapping(runtime::Win64CvttDoubleToInt32(bestZ - vFrom.z));
        // L2NPC.exe 0x00451ee9 loads the same iFound creature into both
        // comparator arguments. This is also present in NASC64_2015.
        if (_LessDistCube(vFrom, hateInfo.GetCreature(), hateInfo.GetCreature()) || nZdiff > 500) {
            iFound = iFirst;
        }
    }
    return CopyToTempObject(iFound->second);
}

void HateInfoList::RemoveByCreature(CSharedCreatureData* spCreature) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (!spCreature || mHateInfos.empty()) {
        return;
    }
    const ITERATOR iInfo = mHateInfos.find(spCreature->index);
    if (iInfo != mHateInfos.end()) {
        mHateInfos.erase(iInfo);
    }
}

bool HateInfoList::TransferByCreature(CSharedCreatureData* spFrom, CSharedCreatureData* spTo) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (!spFrom || !spTo || mHateInfos.empty()) {
        return false;
    }
    const ITERATOR iInfo = mHateInfos.find(spFrom->index);
    if (iInfo == mHateInfos.end()) {
        return false;
    }

    iInfo->second.m_spCreature = spTo;
    return true;
}

void HateInfoList::RemoveAllHateInfo() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    mHateInfos.clear();
}
