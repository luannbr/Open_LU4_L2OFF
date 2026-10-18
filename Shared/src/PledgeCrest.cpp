#include "PledgeCrest.h"
#include "Shared/src/MemorySub.h"


#include <cstring>


CPledgeEmblemIDMapDB g_PledgeEmblemIDMapDB;
#if defined(L2_BUILD_L2SERVER)
CPledgeCrestMap g_PledgeCrestMap;
#endif

L2_DEFINE_OBJECT_CLASS_ID(CPledgeCrest)

L2_DEFINE_OBJECT_CLASS_NAME(CPledgeCrest, m_pClassName_CPledgeCrest)

L2_DEFINE_OBJECT_NEW(CPledgeCrest)

// Current allocator 0x0050d2b0 publishes 0xc20 bytes. The complete CRT
// initializer order makes the class initializer at 0x006a35f0 ID 27.
L2_DEFINE_OBJECT_ALLOCATOR(CPledgeCrest, m_pClassName_CPledgeCrest)

L2_DEFINE_OBJECT_RELEASE(CPledgeCrest)

CPledgeCrest::CPledgeCrest(std::uint32_t crestId, int bitmapSize, const std::uint8_t* bitmap) :
    m_pledgeCrestId(crestId), m_bitmapSize(bitmapSize), m_bitmap{} {
    // Current constructor 0x0050d3d4 clears all 0xc00 bytes before copying.
    // Production callers admit only 1..3072 or a database smallint result.
    if (bitmapSize <= MAX_BITMAP_SIZE) {
        if (bitmapSize > 0) {
            std::memcpy(m_bitmap.data(), bitmap, static_cast<std::size_t>(bitmapSize));
        }
    } else {
        m_bitmapSize = 0;
    }
}

CPledgeCrest::~CPledgeCrest() = default;

std::uint32_t CPledgeCrest::id() const noexcept { return m_pledgeCrestId; }

int CPledgeCrest::bitmapSize() const noexcept { return m_bitmapSize; }

const std::uint8_t* CPledgeCrest::bitmap() const noexcept { return m_bitmap.data(); }

bool CPledgeEmblemIDMapDB::IsExist(const int worldId, const int emblemId) {
    // Current L2Server 0x0098bcec.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const CPledgeEmblemKey key(worldId, emblemId);
    return m_PledgeEmblemMap.find(key) != m_PledgeEmblemMap.end();
}

std::u16string* CPledgeEmblemIDMapDB::Find(const int worldId, const int emblemId) {
    // Current L2Server 0x0098bde0 returns the mapped value
    // after releasing the lock.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const CPledgeEmblemKey key(worldId, emblemId);
    const auto position = m_PledgeEmblemMap.find(key);
    return position != m_PledgeEmblemMap.end() ? &position->second : nullptr;
}

void CPledgeEmblemIDMapDB::Delete(const int worldId, const int emblemId) {
    // Current L2Server 0x0098c6a8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const CPledgeEmblemKey key(worldId, emblemId);
    m_PledgeEmblemMap.erase(key);
}

bool CPledgeEmblemIDMapDB::Add(const int worldId, const int emblemId, SharedUtilsWideChar* const downloadUrl) {
    // Current L2Server 0x0098c78c inserts only when the world/emblem key is
    // absent. An existing URL is left unchanged.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    static_assert(sizeof(SharedUtilsWideChar) == sizeof(char16_t));
    const std::u16string url(reinterpret_cast<const char16_t*>(downloadUrl));
    const CPledgeEmblemKey key(worldId, emblemId);
    if (m_PledgeEmblemMap.find(key) != m_PledgeEmblemMap.end()) {
        return false;
    }
    m_PledgeEmblemMap.emplace(key, url);
    return true;
}
