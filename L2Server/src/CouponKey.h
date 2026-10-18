#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CCouponKey : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CCouponKey)

    CCouponKey(std::uint32_t first, std::uint32_t second, std::uint32_t third, std::uint32_t fourth);
    CCouponKey(const CCouponKey& other);
    ~CCouponKey() override = default;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool operator<(const CCouponKey& other) const;
    std::uint32_t Num(std::uint32_t index) const;

    static CCouponKey Reversed(const char16_t* value, std::uint32_t ordinal);

private:
    std::array<std::uint32_t, 4> m_Number;
};

struct CCouponKeyCmp {
    bool operator()(const CCouponKey* left, const CCouponKey* right) const;
};

class CCouponKeyMgr {
public:
    CCouponKeyMgr();
    ~CCouponKeyMgr();

    void Clear();
    bool Find(const CCouponKey* key) const;

private:
    std::set<CCouponKey*, CCouponKeyCmp> m_CouponDB;
};
