#include "Shared/src/Log.h"

#include "Shared/src/PledgeType.h"

#include "L2Core/src/inc/Guard.h"

#include <algorithm>
#include <array>


namespace {

constexpr std::array<PledgeType, 8> kPledgeTypes{{
    ACADEMY,
    MASTER_PLEDGE,
    SUB_1,
    SUB_2,
    SUB_1_1,
    SUB_1_2,
    SUB_2_1,
    SUB_2_2,
}};

} // namespace

// Current Cached.exe 0x0051f9a0 range-constructs these eight kept values.
CPledgeTypeValidator::CPledgeTypeValidator() : m_pledgeTypeVec(kPledgeTypes.begin(), kPledgeTypes.end()) {}

bool CPledgeTypeValidator::IsValid(PledgeType type) {
    return std::binary_search(m_pledgeTypeVec.begin(), m_pledgeTypeVec.end(), type);
}

std::vector<PledgeType> CPledgeTypeValidator::GetPledgetTypeList() { return m_pledgeTypeVec; }

unsigned int CPledgeTypeValidator::GetIndex(PledgeType type) {
    L2_GUARD_FUNCTION();
    const auto found = std::find(m_pledgeTypeVec.begin(), m_pledgeTypeVec.end(), type);
    if (found == m_pledgeTypeVec.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid pledge type, type[%d]",
        //     "..\\Shared\\PledgeType.cpp",
        //     0x1a,
        //     static_cast<int>(type))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[pledge] Unknown subdivision type %d. Using the main clan's index.",
        //     static_cast<int>(type))
        L2_LOG_ERROR(Log, u"[pledge] Unknown subdivision type %d; using the main clan's index.", static_cast<int>(type));
        return GetIndex(MASTER_PLEDGE);
    }
    return static_cast<unsigned int>(std::distance(m_pledgeTypeVec.begin(), found));
}

PledgeType CPledgeTypeValidator::GetPledgeTypeByIndex(unsigned int index) {
    L2_GUARD_FUNCTION();
    if (index >= m_pledgeTypeVec.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]invalid pledge index, index[%d]",
        //     "..\\Shared\\PledgeType.cpp",
        //     0x27,
        //     static_cast<std::int32_t>(index))
        // Log.Add(
        //     LOG_ERROR,
        //     "[pledge] Subdivision index %u is outside the table of %zu entries. Using the main clan.",
        //     index,
        //     m_pledgeTypeVec.size())
        L2_LOG_ERROR(
            Log, "[pledge] Subdivision index %u is outside the table of %zu entries; using the main clan.", index,
            m_pledgeTypeVec.size()
        );
        return MASTER_PLEDGE;
    }
    return runtime::CheckedVectorAt(m_pledgeTypeVec, index);
}

CPledgeTypeValidator g_PledgeTypeValidator;
