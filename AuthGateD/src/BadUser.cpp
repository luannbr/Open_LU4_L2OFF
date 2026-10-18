#include "BadUser.h"

#include <cstring>
#include <new>

// AuthGateD.exe 0x00401000, BadUser.cpp.
CBadUser::CBadUser() = default;

// AuthGateD.exe 0x00401080, BadUser.cpp.
CBadUser::~CBadUser() {
    ReleaseTable();
}

// AuthGateD.exe 0x00401020, BadUser.cpp.
std::uint32_t CBadUser::hash(const char* userId) {
    std::uint32_t value = 0;
    for (const char* cursor = userId; *cursor; ++cursor) {
        std::int32_t character =
            static_cast<std::int8_t>(*cursor);
        if (character >= 0x60) {
            character -= 0x28;
        }
        value = (value >> 28U) + value * 8U +
            static_cast<std::uint32_t>(character);
    }
    return (value & 0x3fffffffU) % kBucketCount;
}

void CBadUser::ReleaseTable() {
    if (!mpData) {
        return;
    }

    // The shipping process released only the VirtualAlloc bucket slab. Its
    // nodes lived until process exit. The portable owner may be re-created, so
    // it releases those otherwise unreachable allocations as well. This does
    // not change any live table result.
    for (std::size_t bucket = 0; bucket < kBucketCount; ++bucket) {
        Node* node = mpData[bucket];
        while (node) {
            Node* const next = node->next;
            delete node;
            node = next;
        }
    }
    delete[] mpData;
    mpData = nullptr;
}

// AuthGateD.exe 0x004010A0, BadUser.cpp.
bool CBadUser::Init(std::uint16_t maxPenaltySeconds) {
    mMaxPenaltySeconds = maxPenaltySeconds;
    ReleaseTable();

    // VirtualAlloc supplied a zero-filled 100000-entry pointer table in the
    // x86 target. Pointer width is native in the portable build.
    mpData = new (std::nothrow) Node*[kBucketCount]{};
    return mpData != nullptr;
}

// AuthGateD.exe 0x004010F0, BadUser.cpp.
std::uint32_t CBadUser::Add(const char* userId) {
    const std::uint32_t bucket = hash(userId);
    Node** const head = &mpData[bucket];
    Node* previous = nullptr;
    Node* node = *head;

    while (node && std::strcmp(node->userId, userId) != 0) {
        previous = node;
        node = node->next;
    }

    if (!node) {
        node = new Node;
        // The target copies exactly bytes [0, 15). Account names reaching this
        // owner are constrained to fit, so their NUL is included in that span.
        std::memcpy(node->userId, userId, 15);
        node->cntBadConn = 0;
        node->next = nullptr;
        if (previous) {
            previous->next = node;
        } else {
            *head = node;
        }
    }

    const std::uint32_t count = ++node->cntBadConn;
    const std::uint32_t penalty = count + (count * 2U) / 10U;
    if (penalty < mMaxPenaltySeconds) {
        return penalty & 0xffffU;
    }
    return mMaxPenaltySeconds;
}
