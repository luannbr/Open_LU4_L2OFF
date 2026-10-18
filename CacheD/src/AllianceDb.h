#pragma once

#include "Alliance.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/RWLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

using CAllianceKey = std::CDualKey<int, int>;
using CAllianceMap = CObjectMap<CAlliance, 1, 5, RWLock, CAllianceKey>;


// Shipping owner: CacheD/AllianceDb.{h,cpp}. The current global façade is
// addressed sixteen bytes before its kept CObjectMap. No current
// instruction assigns meaning to that prefix, so it remains ABI padding.
class CAllianceDb {
public:
    CAllianceDb();
    ~CAllianceDb();

    void Init();
    CAllianceSP GetAlliance(std::uint32_t allianceId, std::uint32_t worldId);

    int CreateAlliance(const wchar_t* allianceName, std::uint32_t masterPledgeId, int worldId);
    bool JoinAlliance(std::uint32_t allianceId, std::uint32_t memberPledgeId);
    bool Withdraw(std::uint32_t allianceId, std::uint32_t memberPledgeId, std::uint32_t withdrawTime);
    bool OustMemberPledge(std::uint32_t allianceId, std::uint32_t memberPledgeId, std::uint32_t oustTime);
    bool DismissAlliance(std::uint32_t allianceId, std::uint32_t masterPledgeId, std::uint32_t dismissTime);

    bool SetAllianceInfo(std::uint32_t allianceId, std::uint32_t updateFieldName, std::uint32_t updateValue);
    bool ModifyAllianceName(std::uint32_t allianceId, const wchar_t* allianceName);

    const CAllianceMap& alliances() const noexcept;

private:

    [[maybe_unused]] std::array<std::byte, 0x10> m_targetPrefix{};
    CAllianceMap m_alliances;
};



extern CAllianceDb g_allianceDb;
