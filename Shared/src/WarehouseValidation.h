#pragma once

#include "Shared/src/ItemConsumeType.h"

#include <cstdint>

namespace warehouse {

constexpr int kMaxWithdrawalItems = 200;
constexpr std::int64_t kMaxStoredAmount = 999999999999LL;

constexpr bool IsValidWithdrawalAmount(ItemConsumeType consumeType, std::int64_t amount) noexcept {
    switch (consumeType) {
        case ICT_NORMAL:
        case ICT_CHARGE:
            return amount == 1;
        case ICT_STACKABLE:
            return amount >= 1 && amount <= 1000000;
        case ICT_ASSET:
            return amount >= 1 && amount <= kMaxStoredAmount;
        default:
            return false;
    }
}

} // namespace warehouse
