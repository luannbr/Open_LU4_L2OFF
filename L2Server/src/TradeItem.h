#pragma once

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class EnsoulOptionData;

// Original TradeItem.h carrier used by the commission-trade cache replies.
// Current L2Server 0x00af7b8c fixes the complete 0xb0-byte layout. The
// constructor copies only ensoul slots one and two.
class CCommissionItem {
public:
    CCommissionItem(
        std::uint32_t itemId,
        std::uint32_t itemClassId,
        std::int64_t amount,
        std::int32_t enchant,
        std::int32_t damaged,
        std::uint16_t blessed,
        std::int32_t ident,
        std::uint16_t wished,
        std::int32_t variationOption1,
        std::int32_t variationOption2,
        std::int32_t intensiveItemClassId,
        std::int32_t duration,
        std::int16_t attributeAttackType,
        std::int16_t attributeAttackValue,
        const std::int16_t* attributeDefendValues,
        std::int32_t shapeShiftingClassId,
        const std::map<int, EnsoulOptionData*> ensoulOptions[3],
        std::int16_t locked,
        std::int16_t useCount
    );
    CCommissionItem(const CCommissionItem& other);
    ~CCommissionItem();

    CCommissionItem& operator=(const CCommissionItem&) = delete;

    std::uint32_t m_nItemID; // +0x00
    std::uint32_t m_nItemClassID; // +0x04
    std::int64_t m_nAmount; // +0x08
    std::int32_t m_nEnchant; // +0x10
    std::int32_t m_nDamaged; // +0x14
    std::uint16_t m_bBless; // +0x18
    std::array<std::byte, 0x02> m_padding1a; // +0x1a
    std::int32_t m_nIdent; // +0x1c
    std::uint16_t m_nWished; // +0x20
    std::array<std::byte, 0x02> m_padding22; // +0x22
    std::int32_t m_nVariationOpt1; // +0x24
    std::int32_t m_nVariationOpt2; // +0x28
    std::int32_t m_nIntensiveItemClassID; // +0x2c
    std::int32_t m_nDuration; // +0x30
    std::int16_t m_nBaseAttributeAttackType; // +0x34
    std::int16_t m_nBaseAttributeAttackValue; // +0x36
    std::int16_t m_nBaseAttributeDefendValue[6]; // +0x38
    std::int32_t m_nShapeShiftingClassId; // +0x44
    std::map<int, EnsoulOptionData*> mEnsoulOptionMap[3]; // +0x48
    std::int16_t m_hLocked; // +0xa8
    std::int16_t m_hUseCount; // +0xaa
    std::array<std::byte, 0x04> m_targetTailPadding; // +0xac
};
