#include "CouponKey.h"
#include "Shared/src/MemorySub.h"

#include "L2Core/src/inc/BaseConverter.h"

#include <string>

namespace {


CBaseConverter g_Converter;


} // namespace

CCouponKey::CCouponKey(
    const std::uint32_t first, const std::uint32_t second, const std::uint32_t third, const std::uint32_t fourth
) :
    m_Number{first, second, third, fourth} {}

CCouponKey::CCouponKey(const CCouponKey& other) : MemoryObject(other), m_Number(other.m_Number) {}

L2_DEFINE_OBJECT_RELEASE(CCouponKey)

bool CCouponKey::operator<(const CCouponKey& other) const {
    for (std::size_t index = 0; index < m_Number.size(); ++index) {
        if (m_Number[index] < other.m_Number[index]) {
            return true;
        }
        if (other.m_Number[index] < m_Number[index]) {
            return false;
        }
    }
    return false;
}

std::uint32_t CCouponKey::Num(const std::uint32_t index) const {
    return index < m_Number.size() ? m_Number[index] : UINT32_MAX;
}

CCouponKey CCouponKey::Reversed(const char16_t* const value, const std::uint32_t ordinal) {
    const std::u16string_view coupon(value);
    if (coupon.size() == 20) {
        std::uint32_t numbers[4]{};
        const std::size_t offsets[4] = {0, 2, 8, 14};
        const std::size_t lengths[4] = {2, 6, 6, 6};
        for (std::size_t index = 0; index < 4; ++index) {
            const std::u16string part(coupon.substr(offsets[index], lengths[index]));
            g_Converter.ConvertToInt(36, part, &numbers[index]);
        }
        if (numbers[0] == (((numbers[3] ^ numbers[2] ^ numbers[1]) & 0x3fcU) + (ordinal & 3U))) {
            return CCouponKey(numbers[0], numbers[1], numbers[2], numbers[3]);
        }
    }
    static const CCouponKey invalidKey(0, 0, 0, 0);
    return invalidKey;
}

bool CCouponKeyCmp::operator()(const CCouponKey* const left, const CCouponKey* const right) const { return *left < *right; }

CCouponKeyMgr::CCouponKeyMgr() = default;

CCouponKeyMgr::~CCouponKeyMgr() { Clear(); }

void CCouponKeyMgr::Clear() {
    for (CCouponKey* const key : m_CouponDB) {
        L2_RELEASE(key, ObjectRefType::Anonymous);
    }
    m_CouponDB.clear();
}

bool CCouponKeyMgr::Find(const CCouponKey* const key) const {
    if (key->Num(0) == 0 && key->Num(1) == 0 && key->Num(2) == 0 && key->Num(3) == 0) {
        return false;
    }
    return m_CouponDB.find(const_cast<CCouponKey*>(key)) != m_CouponDB.end();
}
