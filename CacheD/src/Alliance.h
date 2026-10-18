#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CAlliance;
class CAllianceDb;

using CAllianceSP = CSPointer<CAlliance>;
using PledgeIdList = std::vector<std::uint32_t>;


// Shipping owner: CacheD/Alliance.{h,cpp}. The UTF-16 array and xstd
// container remain embedded at their current Cached.exe offsets. Host text
// conversion happens only at the method boundary.
class CAlliance : public MemoryObject {
public:
    CAlliance(
        std::uint32_t allianceId,
        const char16_t* allianceName,
        std::uint32_t masterPledgeId,
        std::uint32_t oustTime,
        std::uint32_t crestId,
        std::uint32_t worldId
    );
    ~CAlliance() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    // These three bodies are compiled as standalone COMDATs by the current
    // executable, but every embedded source location names Alliance.h. They
    // were therefore header-defined in the shipping source.
    void AddMemberPledge(std::uint32_t memberPledgeId) {
        // Current Cached.exe 0x0044d200.
        L2_GUARD_FUNCTION();
        L2_WRITE_GUARD(m_lock);
        m_memberPledges.insert(memberPledgeId);
    }

    void RemoveMemberPledge(std::uint32_t memberPledgeId) {
        // Current Cached.exe 0x0044d3a0.
        L2_GUARD_FUNCTION();
        L2_WRITE_GUARD(m_lock);
        m_memberPledges.erase(memberPledgeId);
    }

    void GetAllMemberPledgeId(PledgeIdList& pledgeList) const {
        // Current Cached.exe 0x0044f260 clears the result before locking.
        L2_GUARD_FUNCTION();
        pledgeList.clear();

        L2_READ_GUARD(m_lock);
        for (const std::uint32_t memberPledgeId : m_memberPledges) {
            pledgeList.push_back(memberPledgeId);
        }
    }

    std::uint32_t id() const noexcept;
    const char16_t* name() const noexcept;
    std::uint32_t masterPledgeId() const noexcept;
    std::uint32_t oustTime() const noexcept;
    std::uint32_t crestId() const noexcept;
    std::uint32_t worldId() const noexcept;

private:
    friend class CAllianceDb;

    void AssignName(const char16_t* allianceName);

    L2_DECLARE_OBJECT_REGISTRATION(CAlliance)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAlliance)

    std::uint32_t m_allianceId; // +0x18
    char16_t m_name[25]; // +0x1c
    std::uint8_t m_targetNamePadding[2]; // +0x4e
    std::uint32_t m_masterPledgeId; // +0x50
    std::uint32_t m_oustTime; // +0x54
    std::uint32_t m_crestId; // +0x58
    std::uint32_t m_worldId; // +0x5c
    mutable RWLock m_lock; // +0x60
    std::set<std::uint32_t> m_memberPledges; // +0x98
};
