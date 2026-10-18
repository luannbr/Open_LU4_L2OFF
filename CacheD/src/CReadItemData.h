#pragma once

#include <cstddef>
#include <cstdint>

// Both enum definitions remain owned once by Shared/SharedFactory.h. Fixed
// underlying types let this CacheD header depend only on their public ABI,
// without importing SharedFactory's process-private L2NPC dependencies.
enum ItemType : std::int32_t;
enum CrystalType : std::int32_t;

// Shipping owner: CacheD/CReadItemData.{h,cpp}. The original C1 source
// keeps the first three member names. Current Cached.exe
// 0x00523be8 fixes the complete 0x420-byte layout and constructor order.
// char16_t is intentional: the target's wchar_t is a two-byte UTF-16 code
// unit, whereas wchar_t is four bytes on the supported Unix hosts.
class CReadItemData {
public:
    CReadItemData(
        int itemType,
        wchar_t* itemName,
        int defaultPrice,
        wchar_t* consumeType,
        int canMove,
        bool isPremium,
        bool isPeriod,
        bool isDuration,
        bool isQuestItem,
        ItemType type,
        CrystalType crystalType
    );

    int m_itemType; // +0x000
    char16_t m_itemName[256]; // +0x004
    std::int64_t m_defaultPrice; // +0x208
    char16_t m_consumeType[256]; // +0x210
    int m_canMove; // +0x410
    bool m_isPremium; // +0x414
    bool m_isPeriod; // +0x415
    bool m_isDuration; // +0x416
    bool m_isQuestItem; // +0x417
    ItemType m_type; // +0x418
    CrystalType m_crystalType; // +0x41c
};
