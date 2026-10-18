#include "TradeItem.h"

#include <algorithm>

CCommissionItem::CCommissionItem(
    const std::uint32_t itemId,
    const std::uint32_t itemClassId,
    const std::int64_t amount,
    const std::int32_t enchant,
    const std::int32_t damaged,
    const std::uint16_t blessed,
    const std::int32_t ident,
    const std::uint16_t wished,
    const std::int32_t variationOption1,
    const std::int32_t variationOption2,
    const std::int32_t intensiveItemClassId,
    const std::int32_t duration,
    const std::int16_t attributeAttackType,
    const std::int16_t attributeAttackValue,
    const std::int16_t* const attributeDefendValues,
    const std::int32_t shapeShiftingClassId,
    const std::map<int, EnsoulOptionData*> ensoulOptions[3],
    const std::int16_t locked,
    const std::int16_t useCount
) :
    m_nItemID(itemId),
    m_nItemClassID(itemClassId),
    m_nAmount(amount),
    m_nEnchant(enchant),
    m_nDamaged(damaged),
    m_bBless(blessed),
    m_padding1a{},
    m_nIdent(ident),
    m_nWished(wished),
    m_padding22{},
    m_nVariationOpt1(variationOption1),
    m_nVariationOpt2(variationOption2),
    m_nIntensiveItemClassID(intensiveItemClassId),
    m_nDuration(duration),
    m_nBaseAttributeAttackType(attributeAttackType),
    m_nBaseAttributeAttackValue(attributeAttackValue),
    m_nBaseAttributeDefendValue{},
    m_nShapeShiftingClassId(shapeShiftingClassId),
    mEnsoulOptionMap(),
    m_hLocked(locked),
    m_hUseCount(useCount),
    m_targetTailPadding{} {
    std::copy_n(attributeDefendValues, 6, m_nBaseAttributeDefendValue);
    mEnsoulOptionMap[1] = ensoulOptions[1];
    mEnsoulOptionMap[2] = ensoulOptions[2];
}

CCommissionItem::CCommissionItem(const CCommissionItem& other) :
    CCommissionItem(
        other.m_nItemID,
        other.m_nItemClassID,
        other.m_nAmount,
        other.m_nEnchant,
        other.m_nDamaged,
        other.m_bBless,
        other.m_nIdent,
        other.m_nWished,
        other.m_nVariationOpt1,
        other.m_nVariationOpt2,
        other.m_nIntensiveItemClassID,
        other.m_nDuration,
        other.m_nBaseAttributeAttackType,
        other.m_nBaseAttributeAttackValue,
        other.m_nBaseAttributeDefendValue,
        other.m_nShapeShiftingClassId,
        other.mEnsoulOptionMap,
        other.m_hLocked,
        other.m_hUseCount
    ) {}

CCommissionItem::~CCommissionItem() = default;
