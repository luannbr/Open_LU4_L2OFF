#include "PledgeFunction.h"
#include "Shared/src/MemorySub.h"

#include "PledgeMastery.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <bitset>
#include <stdexcept>
#include <string>

namespace {


std::uint32_t FunctionMask(PledgeFunctionType::Value type) {
    const auto index = static_cast<std::uint32_t>(type);
    if (index >= PledgeFunctionType::MAX_COUNT) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    return std::uint32_t{1} << index;
}

} // namespace

// static_assert(L2ServerMemory::kPledgeFunctionClassId == 225)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PledgeFunction) == 225);

namespace PledgeFunctionStuff {

bool ValidContidion(const int level, const PledgeMastery* const pMastery, const PledgeConditionInfo& Condition) {
    L2_GUARD_FUNCTION();
    if (!pMastery) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeFunction.cpp",
        //     0x5a,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeFunction.cpp",
        //     0x5a,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(Log, "Cannot check clan-function requirements at level %d: mastery state is unavailable.", level);
        return false;
    }
    return Condition.CheckLevel(level) && Condition.CheckMastery(pMastery->GetPoint(Condition.mastery_id));
}

} // namespace PledgeFunctionStuff

L2_DEFINE_OBJECT_CLASS_ID(PledgeFunction)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeFunction, m_pClassName_PledgeFunction)

L2_DEFINE_OBJECT_NEW(PledgeFunction)

L2_DEFINE_OBJECT_ALLOCATOR(PledgeFunction, __GetClassName())

L2_DEFINE_OBJECT_ADD_REF(PledgeFunction)

L2_DEFINE_OBJECT_RELEASE(PledgeFunction)

PledgeFunction::PledgeFunction() : m_nMaxMemberCount(10), m_nMaxEliteCount(0), m_Bits(0) {}

PledgeFunction::~PledgeFunction() = default;

void PledgeFunction::Builder::Enable(PledgeFunction* function, PledgeFunctionType::Value type) {
    function->m_Bits |= FunctionMask(type);
}

void PledgeFunction::Builder::Disable(PledgeFunction* function, PledgeFunctionType::Value type) {
    function->m_Bits &= ~FunctionMask(type);
}

std::uint32_t PledgeFunction::Builder::GetBits(const PledgeFunction* function) noexcept { return function->m_Bits; }

bool PledgeFunction::IsEnable(PledgeFunctionType::Value type) const {
    L2_GUARD_FUNCTION();
    return (m_Bits & FunctionMask(type)) != 0;
}

void PledgeFunction::UpdateFunction(const int level, const PledgeMastery* const pMastery) {
    // Current L2Server 0x009981fc.
    L2_GUARD_FUNCTION();
    if (!pMastery) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeFunction.cpp",
        //     0x1a,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeFunction.cpp",
        //     0x1a,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot update clan functions at level %d: mastery state is unavailable; existing function limits and flags are "
            "retained.",
            level
        );
        return;
    }

    m_nMaxMemberCount = 10;
    m_nMaxEliteCount = 0;
    m_Bits = 0;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] Fuction value reset. "
    //     L"pledge[DBid : %d, level : %d]",
    //     L"PledgeFunction.cpp",
    //     0x20,
    //     pMastery->GetPledgeDBid(),
    //     level)
    // Noise: Resetting function limits before recalculating clan privileges is routine work, not an error.
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] Fuction value reset. "
    //     L"pledge[DBid : %d, level : %d]",
    //     L"PledgeFunction.cpp",
    //     0x20,
    //     pMastery->GetPledgeDBid(),
    //     level)

    const std::vector<PledgeFunctionInfo*>& infos = PledgeFunctionDB::inst().GetInfos();
    for (PledgeFunctionInfo* const each : infos) {
        if (!PledgeFunctionStuff::ValidContidion(level, pMastery, each->Condition)) {
            continue;
        }
        if (each->type == PledgeFunctionType::MEMBER_COUNT) {
            m_nMaxMemberCount = std::max(m_nMaxMemberCount, each->MemberCount.total_count);
            m_nMaxEliteCount = std::max(m_nMaxEliteCount, each->MemberCount.elite_count);
        }
        m_Bits |= FunctionMask(each->type);
        // Noise: This bit-string allocation served only the suppressed per-function trace.
        // const std::string bits =
        //     std::bitset<PledgeFunctionType::MAX_COUNT>(m_Bits)
        //         .to_string('0', '1')
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] Fuction value set. "
        //     L"pledge[DBid : %d, level : %d, type : %d] Bits[%S]",
        //     L"PledgeFunction.cpp",
        //     0x30,
        //     pMastery->GetPledgeDBid(),
        //     level,
        //     each->type,
        //     bits.c_str())
        // Noise: Per-function bitset traces during privilege recalculation add routine noise. The actual limits and flag
        // updates remain unchanged. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] Fuction value set. "
        //     L"pledge[DBid : %d, level : %d, type : %d] Bits[%S]",
        //     L"PledgeFunction.cpp",
        //     0x30,
        //     pMastery->GetPledgeDBid(),
        //     level,
        //     each->type,
        //     bits.c_str())
    }
}
