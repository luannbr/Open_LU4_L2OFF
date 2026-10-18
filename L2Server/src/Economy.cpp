#include "Economy.h"

#include "Creature.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/platform/Runtime.h"

bool CEconomy::SetShortTimeStamp(std::uint32_t* timeStamp, std::uint32_t delay) {
    L2_GUARD_FUNCTION();
    const std::uint32_t candidate = runtime::GetTickCount() + delay;
    if (runtime::raw_atomic::LoadU32Relaxed(timeStamp) >= candidate) {
        return false;
    }
    runtime::raw_atomic::ExchangeU32(timeStamp, candidate);
    return true;
}

bool CEconomy::IsInShortTime(std::uint32_t timeStamp, std::uint32_t delay) {
    L2_GUARD_FUNCTION();
    const std::uint32_t currentTick = runtime::GetTickCount();
    return currentTick < timeStamp + delay && timeStamp != 0;
}

bool CEconomy::SetCreatureData(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Creature is invalid.",
        //     "Economy.cpp",
        //     0x34)
        // Log.Add(LOG_ERROR,
        //     L"[economy] Cannot set creature data: no creature was provided.")
        L2_LOG_ERROR(Log, u"[economy] Cannot set creature data: no creature was provided.");
        return false;
    }

    nNPCClassId = creature->m_nClassId;
    nNPCServerId = static_cast<std::int32_t>(creature->Id());
    if (creature->m_data != nullptr) {
        nNPCSId = creature->m_data->index;
    }
    return true;
}
