#include "Alliance.h"
#include "Shared/src/MemorySub.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <iterator>


L2_DEFINE_OBJECT_CLASS_ID(CAlliance)

L2_DEFINE_OBJECT_CLASS_NAME(CAlliance, m_pClassName_CAlliance)

L2_DEFINE_OBJECT_NEW(CAlliance)

// Current allocator 0x0044cf70 publishes 0xb8 bytes. The complete CRT
// table makes 0x0069d89c the ninth MemoryObject class initializer and
// therefore assigns class ID 9.
L2_DEFINE_OBJECT_ALLOCATOR(CAlliance, m_pClassName_CAlliance)

L2_DEFINE_OBJECT_RELEASE(CAlliance)

CAlliance::CAlliance(
    std::uint32_t allianceId,
    const char16_t* allianceName,
    std::uint32_t masterPledgeId,
    std::uint32_t oustTime,
    std::uint32_t crestId,
    std::uint32_t worldId
) :
    m_allianceId(allianceId),
    m_name{},
    m_targetNamePadding{},
    m_masterPledgeId(masterPledgeId),
    m_oustTime(oustTime),
    m_crestId(crestId),
    m_worldId(worldId) {
    // Exact field order and secure-copy capacity in current 0x0044d880.
    AssignName(allianceName);
}

// Current Cached.exe complete destructor 0x0044dab0.
CAlliance::~CAlliance() = default;

void CAlliance::AssignName(const char16_t* allianceName) {
    std::fill(std::begin(m_name), std::end(m_name), u'\0');
    if (!allianceName) {
        runtime::process::HandleInvalidParameter(L"allianceName != nullptr");
        return;
    }

    const std::u16string_view targetName(allianceName);
    if (targetName.size() >= std::size(m_name)) {
        // wcscpy_s(target, 25, source) clears target[0] before invoking the
        // current process invalid-parameter owner on an oversized source.
        runtime::process::HandleInvalidParameter(L"allianceName length < 25");
        return;
    }
    std::copy(targetName.begin(), targetName.end(), std::begin(m_name));
}

std::uint32_t CAlliance::id() const noexcept { return m_allianceId; }

const char16_t* CAlliance::name() const noexcept { return m_name; }

std::uint32_t CAlliance::masterPledgeId() const noexcept { return m_masterPledgeId; }

std::uint32_t CAlliance::oustTime() const noexcept { return m_oustTime; }

std::uint32_t CAlliance::crestId() const noexcept { return m_crestId; }

std::uint32_t CAlliance::worldId() const noexcept { return m_worldId; }
