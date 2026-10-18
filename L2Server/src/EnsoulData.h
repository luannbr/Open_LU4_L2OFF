#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

enum CrystalType : std::int32_t;
class CSkillInfo;

namespace EnsoulItem {

// enum EnsoulOptionType : int {
//     EOT_INVALID = -1,
//     EOT_BASE = 0,
//     EOT_PHY1 = 1,
//     EOT_PHY2 = 2,
//     EOT_PHY3 = 3,
//     EOT_PHY4 = 4,
//     EOT_PHY5 = 5,
//     EOT_PHY6 = 6,
//     EOT_PHY7 = 7,
//     EOT_PHY8 = 8,
//     EOT_PHY9 = 9,
//     EOT_MAG1 = 10,
//     EOT_MAG2 = 11,
//     EOT_MAG3 = 12,
//     EOT_MAG4 = 13,
//     EOT_MAG5 = 14,
//     EOT_MAG6 = 15,
//     EOT_MAG7 = 16,
//     EOT_MAG8 = 17,
//     EOT_MAG9 = 18,
//     EOT_PPHY1 = 19,
//     EOT_PPHY2 = 20,
//     EOT_PPHY3 = 21,
//     EOT_PPHY4 = 22,
//     EOT_PPHY5 = 23,
//     EOT_PPHY6 = 24,
//     EOT_PPHY7 = 25,
//     EOT_PPHY8 = 26,
//     EOT_PPHY9 = 27,
//     EOT_MMAG1 = 28,
//     EOT_MMAG2 = 29,
//     EOT_MMAG3 = 30,
//     EOT_MMAG4 = 31,
//     EOT_MMAG5 = 32,
//     EOT_MMAG6 = 33,
//     EOT_MMAG7 = 34,
//     EOT_MMAG8 = 35,
//     EOT_MMAG9 = 36,
//     EOT_PM1 = 37,
//     EOT_PM2 = 38,
//     EOT_PM3 = 39,
//     EOT_PM4 = 40,
//     EOT_PM5 = 41,
//     EOT_PM6 = 42,
//     EOT_PM7 = 43,
//     EOT_PM8 = 44,
//     EOT_PM9 = 45,
//     EOT_TRA1 = 46,
//     EOT_TRA2 = 47,
//     EOT_TRA3 = 48,
//     EOT_TRA4 = 49,
//     EOT_TRA5 = 50,
//     EOT_TRA6 = 51,
//     EOT_TRA7 = 52,
//     EOT_TRA8 = 53,
//     EOT_TRA9 = 54,
//     EOT_ATT1 = 55,
//     EOT_ATT2 = 56,
//     EOT_ATT3 = 57,
//     EOT_ATT4 = 58,
//     EOT_ATT5 = 59,
//     EOT_ATT6 = 60,
//     EOT_ATT7 = 61,
//     EOT_ATT8 = 62,
//     EOT_ATT9 = 63,
//     EOT_ETC1 = 64,
//     EOT_ETC2 = 65,
//     EOT_ETC3 = 66,
//     EOT_ETC4 = 67,
//     EOT_ETC5 = 68,
//     EOT_ETC6 = 69,
//     EOT_ETC7 = 70,
//     EOT_ETC8 = 71,
//     EOT_ETC9 = 72,
//     EOT_SIGEL_DEFENSE = 73,
//     EOT_SIGEL_HEALTH = 74,
//     EOT_TIR_MIGHT = 75,
//     EOT_TIR_HASTE = 76,
//     EOT_OTHEL_FOCUS = 77,
//     EOT_OTHEL_CRTDMG = 78,
//     EOT_YR_WINDWALK = 79,
//     EOT_YR_GUIDANCE = 80,
//     EOT_FEOH_MAGICPOWER = 81,
//     EOT_FEOH_EMPOWER = 82,
//     EOT_IS_MPREGEN = 83,
//     EOT_IS_MPUP = 84,
//     EOT_WYNN_ACUMEN = 85,
//     EOT_WYNN_LIGHT = 86,
//     EOT_EOLH_HPREGEN = 87,
//     EOT_EOLH_HEAL = 88,
//     EOT_TIR_CRTSTUN = 89,
//     EOT_OTHEL_MIGHTMORTAL = 90,
//     EOT_YR_CRTDMG = 91,
//     EOT_IS_ACUMEN = 92,
//     EOT_IS_WINDWALK = 93,
//     EOT_WYNN_MPUP = 94,
//     EOT_EOLH_MPREGEN = 95,
//     EOT_ANAKIM_POWER = 96,
//     EOT_LILITH_POWER = 97,
//     EOT_MAX = 98,
// }

// enum EnsoulItemSlotType : int {
//     EIST_INVALID = 0,
//     EIST_NORMAL = 1,
//     EIST_BM = 2,
//     EIST_MAX = 3,
// }

} // namespace EnsoulItem

class EnsoulTryData {
public:
    EnsoulTryData(EnsoulItem::EnsoulItemSlotType slotType, int slotIndex, int stoneItemID, int optionID) :
        m_slotType(slotType), m_nSlotIndex(slotIndex), m_nStoneItemID(stoneItemID), m_nOptionID(optionID) {}

    EnsoulItem::EnsoulItemSlotType m_slotType; // +0x00
    int m_nSlotIndex; // +0x04
    int m_nStoneItemID; // +0x08
    int m_nOptionID; // +0x0c
};

class EnsoulFeeItemData {
public:
    EnsoulFeeItemData(int feeItemType, int feeItemCount) : m_nFeeItemType(feeItemType), m_nFeeItemCount(feeItemCount) {}

    int m_nFeeItemType; // +0x00
    int m_nFeeItemCount; // +0x04
};

class EnsoulOptionData {
public:
    EnsoulOptionData() : m_nId(0), m_optionType(EnsoulItem::EOT_INVALID), m_nExtractionStoneClassId(0) {}

    void SetEnsoulOptionSkills(std::vector<std::int64_t, std::allocator<std::int64_t>>& skills);
    static EnsoulItem::EnsoulOptionType GetEnsoulOptionType(const wchar_t* name);
    void SetExtractionStoneClassId(int classId);
    int GetExtractionStoneClassId() const;
    bool CanExtraction() const;

    int m_nId; // +0x00
    std::vector<const CSkillInfo*,
        std::allocator<const CSkillInfo*>> m_vSkillList; // +0x08
    EnsoulItem::EnsoulOptionType m_optionType; // +0x28
    int m_nExtractionStoneClassId; // +0x2c
};

class EnsoulFeeData {
public:
    using FeeItemVector = std::vector<EnsoulFeeItemData*, std::allocator<EnsoulFeeItemData*>>;

    void SetEnsoulFeeNormal(FeeItemVector& fees);
    void SetEnsoulFeeBM(FeeItemVector& fees);
    void SetEnsoulReFeeNormal(FeeItemVector& fees);
    void SetEnsoulReFeeBM(FeeItemVector& fees);
    void SetExtractionFeeNormal(FeeItemVector& fees);
    void SetExtractionFeeBM(FeeItemVector& fees);

    CrystalType m_crystalType; // +0x00
    FeeItemVector m_vFeeNormal; // +0x08
    FeeItemVector m_vFeeBM; // +0x28
    FeeItemVector m_vReFeeNormal; // +0x48
    FeeItemVector m_vReFeeBM; // +0x68
    FeeItemVector m_vExtractionNormal; // +0x88
    FeeItemVector m_vExtractionBM; // +0xa8
};

class EnsoulStoneData {
public:
    using OptionIdVector = std::vector<std::int64_t, std::allocator<std::int64_t>>;

    void SetEnsoulOptions(OptionIdVector& options);

    bool CheckType(EnsoulItem::EnsoulItemSlotType slotType) { return m_slotType == slotType; }

    bool CheckOption(std::int64_t optionId);

    std::int64_t m_nItemType; // +0x00
    EnsoulItem::EnsoulItemSlotType m_slotType; // +0x08
    OptionIdVector m_vOptionIdVector; // +0x10
};

using EnsoulOptionDataMap = std::map<int, EnsoulOptionData*>;
using EnsoulStoneDataMap = std::map<std::int64_t, EnsoulStoneData*>;
using EnsoulFeeDataMap = std::map<CrystalType, EnsoulFeeData*>;

class EnsoulManager {
public:
    void Load(bool onlyPrecompiledHeader);
    void Uniputs(char16_t* text);

    std::FILE* PCHFile; // +0x00
};

extern EnsoulManager g_EnsoulManager;
extern EnsoulOptionDataMap g_mEnsoulOptionData;
extern EnsoulStoneDataMap g_mEnsoulStoneData;
extern EnsoulFeeDataMap g_EnsoulFeeData;

static_assert(std::is_trivially_copyable_v<EnsoulTryData>);
static_assert(std::is_trivially_destructible_v<EnsoulTryData>);
static_assert(!std::is_default_constructible_v<EnsoulTryData>);
static_assert(std::is_constructible_v<EnsoulTryData, EnsoulItem::EnsoulItemSlotType, int, int, int>);
