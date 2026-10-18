#include "Inspector.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

CInspectorDB g_InspectorDB;

CInspector* CInspectorDB::GetInspector(int x, int y) {
    L2_GUARD_FUNCTION();
    const std::uint32_t upper = (static_cast<std::uint32_t>(x) * 2U + 0xa0000U) & 0xffff0000U;
    const std::uint32_t lower = static_cast<std::uint32_t>((runtime::AddInt32Wrapping(y, 0x40000) >> 15) + 10);
    const std::uint32_t keyBits = (upper + 0xa0000U) | lower;
    int key = 0;
    static_assert(sizeof(key) == sizeof(keyBits));
    std::memcpy(&key, &keyBits, sizeof(key));
    if (m_vec.find(key) != m_vec.end()) {
        const int count = static_cast<int>(m_vec[key].size());
        if (count > 0) {
            return m_vec[key][static_cast<std::size_t>(std::rand() % count)];
        }
    }
    return nullptr;
}
