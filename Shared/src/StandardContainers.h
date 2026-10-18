#pragma once

#include <array>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Shared/src/PriorityQueue.h"

namespace runtime {
// Preserve the previous xvector bounds check at migrated indexing sites.
template <typename Vector>
decltype(auto) CheckedVectorAt(Vector&& values, std::size_t index) {
    if (index >= values.size()) {
        throw std::out_of_range("invalid vector<T> subscript");
    }
    return values[index];
}

template <typename Char, typename Traits, typename Allocator>
std::basic_string_view<Char, Traits> StringView(const std::basic_string<Char, Traits, Allocator>& value) noexcept {
    return {value.data(), value.size()};
}

// Legacy name maps compare null-terminated text, including embedded NULs.
template <typename String>
struct CStringLess {
private:
    using character_type = typename String::value_type;

    static int Compare(const character_type* left, const character_type* right) noexcept {
        for (;;) {
            const character_type leftValue = *left++;
            const character_type rightValue = *right++;
            if (leftValue != rightValue || leftValue == character_type{}) {
                return leftValue < rightValue ? -1 : rightValue < leftValue ? 1 : 0;
            }
        }
    }

public:
    using is_transparent = void;

    bool operator()(const character_type* left, const String& right) const noexcept {
        return Compare(left, right.c_str()) < 0;
    }

    bool operator()(const String& left, const character_type* right) const noexcept {
        return Compare(left.c_str(), right) < 0;
    }

    bool operator()(const String& left, const String& right) const noexcept {
        return Compare(left.c_str(), right.c_str()) < 0;
    }
};


} // namespace runtime
