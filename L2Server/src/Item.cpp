#include "Item.h"
#include "BuilderCmdHandler.h"
#include "CWorld.h"
#include "CommissionTrade.h"
#include "DB.h"
#include "EnchantOption.h"
#include "InstantZone.h"
#include "Inventory.h"
#include "Object.h"
#include "ObjectDB.h"
#include "ParserConvertData.h"
#include "Pet.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Config.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "SharedContainer.h"
#include "Skill.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"
#include "VariationDB.h"
#include "XmlLoader.h"


#include "Shared/src/Exception.h"


#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

constexpr const char* kItemSource = "Item.cpp";
constexpr const wchar_t* kItemRestrictPath = L"..\\Script\\Itemrestrict.xml";
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

constexpr int kAttributeEnchantProbabilityWeapon[6][4]{
    {50, 30, 10, 5},
    {50, 40, 20, 10},
    {50, 50, 30, 15},
    {50, 60, 40, 20},
    {50, 60, 40, 20},
    {50, 60, 40, 20},
};

constexpr int kAttributeEnchantProbabilityArmor[6][4]{
    {60, 50, 30, 10},
    {80, 70, 50, 25},
    {80, 80, 60, 40},
    {100, 80, 60, 40},
    {100, 100, 80, 50},
    {100, 100, 80, 50},
};

constexpr short kAttributeWeaponMaximum[9][2]{
    {5, 25},
    {10, 75},
    {15, 150},
    {5, 175},
    {10, 225},
    {15, 300},
    {5, 325},
    {10, 375},
    {15, 450},
};

constexpr short kAttributeArmorMaximum[9][2]{
    {2, 12},
    {3, 30},
    {5, 60},
    {2, 72},
    {3, 90},
    {5, 120},
    {2, 132},
    {3, 150},
    {5, 180},
};

void ResetAttributeEnchantState(User& user) {
    user.SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
    runtime::raw_atomic::ExchangeI32(&user.m_lWaitVariation, 0);
    user.GetSocket()->Send("chd", 0xfe, 0x62, 2);
    user.nAttributeCount = 0;
    user.nAttributeFailCount = 0;
}

void WriteAttributeEnchantFailureLog(User& user, CItem& targetItem, CItem& enchantItem, std::int64_t failedCount) {
    char16_t attribute[256]{};
    char16_t ensoul[256]{};
    char16_t option[256]{};
    targetItem.m_itemBaseAttribute.GetLogString(attribute);
    targetItem.MakeEnsoulLogInfo(ensoul);
    FormatUtf16To(
        option, std::size(option), u"%d:%d:%d", targetItem.m_itemOption.intensiveItemClassID,
        targetItem.m_itemOption.itemOptionKey.variationOption1, targetItem.m_itemOption.itemOptionKey.variationOption2
    );

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x3d7, &user, nullptr, attribute, ensoul, option, targetItem.m_data->m_nClassID, enchantItem.m_data->m_nClassID,
        targetItem.m_data->GetEnchanted(), failedCount, targetItem.m_data->GetShapeShiftedClassId(), 0, 0, 0, 0, 0,
        targetItem.GetDBID()
    );
}

// Current L2Server .data 0x015c69e20 is one flat
// [pet-collar][VALIDATE][ItemTypeForEnchantAdd][eight old grades] table.
// GetEnchantValue performs the same flat index arithmetic: the
// CRT_EVENT value (11) therefore crosses the nominal eight-grade row instead
// of being clamped. Keeping a flat carrier keeps that current behavior.
constexpr std::size_t kEnchantValidatePetCount = 2;
constexpr std::size_t kEnchantValidateTypeCount = 16;
constexpr std::size_t kEnchantValidateItemTypeCount = ITE_MAX;
constexpr std::size_t kEnchantValidateGradeCount = 8;
using EnchantValidateTable = std::array<
    double,
    kEnchantValidatePetCount * kEnchantValidateTypeCount * kEnchantValidateItemTypeCount * kEnchantValidateGradeCount>;

constexpr std::size_t EnchantValidateIndex(int petCollar, int validateType, int itemType, int crystal) noexcept {
    return (
        ((static_cast<std::size_t>(petCollar) * kEnchantValidateTypeCount + static_cast<std::size_t>(validateType)) *
                kEnchantValidateItemTypeCount +
            static_cast<std::size_t>(itemType)) *
            kEnchantValidateGradeCount +
        static_cast<std::size_t>(crystal)
    );
}

const EnchantValidateTable kCurrentEnchantValidateValues = [] {
    EnchantValidateTable values{};
    const std::array<double, 8> ones{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

    const auto setRow = [&values](int pet, int validate, int itemType, const std::array<double, 8>& row) {
        std::copy(
            row.begin(), row.end(),
            values.begin() + static_cast<std::ptrdiff_t>(EnchantValidateIndex(pet, validate, itemType, 0))
        );
    };
    const auto setConstantRow = [&values](int pet, int validate, int itemType, double value) {
        std::fill_n(
            values.begin() + static_cast<std::ptrdiff_t>(EnchantValidateIndex(pet, validate, itemType, 0)),
            kEnchantValidateGradeCount, value
        );
    };

    for (int itemType = 0; itemType < ITE_MAX; ++itemType) {
        setRow(0, VALIDATE::PDEFEND, itemType, ones);
        setRow(0, VALIDATE::MDEFEND, itemType, ones);
        setRow(0, VALIDATE::MATTACK, itemType, {2.0, 2.0, 3.0, 3.0, 3.0, 4.0, 4.0, 4.0});
    }
    setRow(0, VALIDATE::PDEFEND, ITE_ONEHAND, {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 5.0, 1.0});
    setRow(0, VALIDATE::PDEFEND, ITE_TWOHAND, {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 6.0, 1.0});
    setRow(0, VALIDATE::MDEFEND, ITE_BOW, {1.0, 1.0, 1.0, 1.0, 8.0, 1.0, 1.0, 1.0});
    setRow(0, VALIDATE::PATTACK, ITE_NORMAL, {2.0, 2.0, 3.0, 3.0, 4.0, 5.0, 5.0, 5.0});
    setRow(0, VALIDATE::PATTACK, ITE_ONEHAND, {2.0, 2.0, 3.0, 3.0, 4.0, 5.0, 5.0, 5.0});
    setRow(0, VALIDATE::PATTACK, ITE_TWOHAND, {2.0, 2.0, 4.0, 4.0, 5.0, 6.0, 6.0, 6.0});
    setRow(0, VALIDATE::PATTACK, ITE_BOW, {4.0, 4.0, 6.0, 6.0, 8.0, 10.0, 10.0, 10.0});
    setRow(0, VALIDATE::PATTACK, ITE_CROSSBOW, {3.0, 3.0, 5.0, 5.0, 6.0, 7.0, 7.0, 7.0});
    setRow(0, VALIDATE::SHIELDDEFENSE, ITE_NORMAL, ones);

    // The pet-collar half initially duplicates the ordinary half, including
    // its zero rows. Current then overwrites the physical P/M-attack rows.
    constexpr std::size_t halfSize = kEnchantValidateTypeCount * kEnchantValidateItemTypeCount * kEnchantValidateGradeCount;
    std::copy_n(values.begin(), halfSize, values.begin() + halfSize);
    setConstantRow(1, VALIDATE::PATTACK, ITE_NORMAL, 4.0);
    setConstantRow(1, VALIDATE::PATTACK, ITE_ONEHAND, 4.0);
    setConstantRow(1, VALIDATE::PATTACK, ITE_TWOHAND, 5.0);
    setConstantRow(1, VALIDATE::PATTACK, ITE_BOW, 8.0);
    setConstantRow(1, VALIDATE::PATTACK, ITE_CROSSBOW, 5.0);
    for (int itemType = 0; itemType < ITE_MAX; ++itemType) {
        setConstantRow(1, VALIDATE::MATTACK, itemType, 3.0);
    }
    return values;
}();

double CurrentEnchantValidateValue(
    bool petCollar, VALIDATE::Enum validateType, ItemTypeForEnchantAdd itemType, int crystal
) noexcept {
    return kCurrentEnchantValidateValues[EnchantValidateIndex(
        petCollar ? 1 : 0, static_cast<int>(validateType), static_cast<int>(itemType), crystal
    )];
}

bool EqualsAsciiCaseInsensitive(std::u16string_view left, std::u16string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        char16_t leftUnit = left[index];
        char16_t rightUnit = right[index];
        if (leftUnit >= u'A' && leftUnit <= u'Z') {
            leftUnit = static_cast<char16_t>(leftUnit + (u'a' - u'A'));
        }
        if (rightUnit >= u'A' && rightUnit <= u'Z') {
            rightUnit = static_cast<char16_t>(rightUnit + (u'a' - u'A'));
        }
        if (leftUnit != rightUnit) {
            return false;
        }
    }
    return true;
}

int ItemRestrictItemId(std::u16string_view value) {
    const std::string narrow = ::Utf16ToUtf8(value);
    const std::int64_t parsed = static_cast<std::int64_t>(std::strtoll(narrow.c_str(), nullptr, 10));
    const std::uint32_t lowBits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(parsed));
    std::int32_t itemId = 0;
    std::memcpy(&itemId, &lowBits, sizeof(itemId));
    return itemId;
}

std::int64_t ItemReleaseDelay(const CItem* item) noexcept {
    const std::uint32_t currentTick = runtime::GetTickCount();
    const double elapsed = static_cast<double>(currentTick) -
        (static_cast<double>(item->m_const->m_nDropPeriod) + static_cast<double>(item->m_nDroppedTick));
    if (elapsed > 0.0) {
        return 5000;
    }
    return runtime::Win64CvttDoubleToInt64(5000.0 - elapsed);
}

std::int32_t ItemClassId(const CItem* item) noexcept { return item->m_data->m_nClassID; }


void CopyItemHtml(char16_t (&destination)[260], const wchar_t* source) {
    const std::u16string target = ::WideToUtf16(source ? std::wstring_view(source) : std::wstring_view{});
    const std::size_t count = std::min<std::size_t>(target.size(), 259);
    std::copy_n(target.data(), count, destination);
    destination[count] = u'\0';
    std::fill(destination + count + 1, destination + 260, u'\0');
}

std::uint32_t MultiplyCurrentInt32(int value, std::uint32_t multiplier) noexcept {
    return static_cast<std::uint32_t>(value) * multiplier;
}

bool IsNone(const wchar_t* value) { return std::wcscmp(value, L"none") == 0; }

void ResolveFixedItemSkill(CItem& item, const CSkillKey& key, void*& destination) {
    if (key.m_nSkillId <= 0) {
        return;
    }
    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, nullptr);
    destination = const_cast<CSkillInfo*>(skill);
    if (skill == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Skill in %s is not valid. "
        //     L"id = %d. class = [%d] Skill = [%d, %d]",
        //     L"..\\Script\\Itemdata.txt",
        //     static_cast<int>(item.Id()),
        //     item.m_data->m_nClassID,
        //     key.m_nSkillId,
        //     static_cast<int>(key.m_nLevel))
        // L2_LOG_ERROR(Log,
        //     L"Skill in %s is not valid. "
        //     L"id = %d. class = [%d] Skill = [%d, %d]",
        //     L"..\\Script\\Itemdata.txt",
        //     static_cast<int>(item.Id()),
        //     item.m_data->m_nClassID,
        //     key.m_nSkillId,
        //     static_cast<int>(key.m_nLevel))
        L2_LOG_ERROR(
            Log, "Itemdata.txt: item object %u (class %d) references missing skill %d level %d; skill pointer remains null.",
            static_cast<unsigned>(item.Id()), item.m_data->m_nClassID, key.m_nSkillId, static_cast<int>(key.m_nLevel)
        );
        return;
    }
    if (item.m_const->m_nDuration > 0) {
        const_cast<CSkillInfo*>(skill)->PushRelatedItem(item.m_data->m_nClassID);
    }
}

bool ResolveItemSkillList(
    CItem& item,
    const std::vector<CSkillKey>& staged,
    std::vector<const CSkillInfo*>& destination,
    // const wchar_t* invalidFormat) {
    const char* skillListName
) {
    bool result = true;
    destination.clear();
    for (const CSkillKey& key : staged) {
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, nullptr);
        if (skill == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     invalidFormat,
            //     L"..\\Script\\Itemdata.txt")
            // L2_LOG_ERROR(Log,
            //     invalidFormat,
            //     L"..\\Script\\Itemdata.txt")
            L2_LOG_ERROR(
                Log, "Itemdata.txt: item object %u (class %d), %s list: skill %d level %d is missing; skipping this entry.",
                static_cast<unsigned>(item.Id()), item.m_data->m_nClassID, skillListName, key.m_nSkillId,
                static_cast<int>(key.m_nLevel)
            );
            result = false;
            continue;
        }
        destination.push_back(skill);
        if (item.m_const->m_nDuration > 0) {
            const_cast<CSkillInfo*>(skill)->PushRelatedItem(item.m_data->m_nClassID);
        }
    }
    return result;
}

template <std::size_t Capacity>
const char16_t* HostWideToTargetUtf16(const wchar_t* source, std::array<char16_t, Capacity>& destination) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return destination.data();
    }
    std::size_t output = 0;
    for (std::size_t input = 0; source[input] != L'\0' && output + 1 < destination.size(); ++input) {
        const std::uint32_t value = static_cast<std::uint32_t>(source[input]);
        if (value <= 0xffffU) {
            destination[output++] = static_cast<char16_t>(value);
            continue;
        }
        if (value <= 0x10ffffU && output + 2 < destination.size()) {
            const std::uint32_t adjusted = value - 0x10000U;
            destination[output++] = static_cast<char16_t>(0xd800U + (adjusted >> 10U));
            destination[output++] = static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU));
        }
    }
    return destination.data();
}

} // namespace

EtcManiUpdateUserItemBaseAttribute::EtcManiUpdateUserItemBaseAttribute(
    int userSid, int userDBID, int targetItemDBID, int intensiveItemClassId, int enchantType, BaseAttribute attribute
) :
    m_nUserSid(static_cast<unsigned int>(userSid)),
    m_nUserDBID(userDBID),
    m_nTargetItemDBID(targetItemDBID),
    m_nIntensiveItemClassId(intensiveItemClassId),
    m_nEnchantType(enchantType),
    m_Attribute(attribute) {
    // Current L2Server 0x007d0d5c, original Item.cpp owner.
}

bool CItem::ChangeAttributeItem(CItem* targetItem, User* user) {
    // Current L2Server 0x007d1ea8.
    L2_GUARD_FUNCTION();
    db.RequestChangeAttribute(this, targetItem, user, targetItem->m_eChangeAttribute);
    return true;
}

bool CItem::ShapeShiftItem(
    User* user, CItem* targetItem, CItem* shapeShiftItem, bool extract, std::int64_t amount, int period
) {
    // Current L2Server 0x007d4048.
    L2_GUARD_FUNCTION();
    if (user == nullptr || targetItem == nullptr) {
        return false;
    }
    if (shapeShiftItem == nullptr) {
        return false;
    }
    if (user->IsBuilder() && period != -1 && g_nTestShapeShiftPeriodForBuilder != -1) {
        period = g_nTestShapeShiftPeriodForBuilder;
    }
    db.RequestShapeShiftItem(user, this, targetItem, shapeShiftItem, extract, amount, period);
    return true;
}

bool CItem::ShapeShiftItem(User* user, CItem* targetItem, int shapeShiftClassId, std::int64_t amount, int period) {
    // Current L2Server 0x007d4174.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (targetItem == nullptr) {
        return false;
    }
    if (user->IsBuilder() && period != -1 && g_nTestShapeShiftPeriodForBuilder != -1) {
        period = g_nTestShapeShiftPeriodForBuilder;
    }
    db.RequestShapeShiftItem(user, this, targetItem, shapeShiftClassId, amount, period);
    return true;
}

bool CItem::RestoreShapeShiftItem(User* user, CItem* targetItem) {
    // Current L2Server 0x007d428c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || targetItem == nullptr) {
        return false;
    }
    db.RequestRestoreShapeShiftItem(user, this, targetItem);
    return true;
}

bool CItem::IsShapeShiftableTargetItem(User* user, CItem* shapeShiftItem, CItem* extraItem) const {
    // Current L2Server 0x007d77b0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || shapeShiftItem == nullptr) {
        return false;
    }
    if (shapeShiftItem->m_data->GetLocked() != 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }
    if (!m_const->m_bShapeShiftableTarget) {
        user->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_MODIFIED_OR_RESTORED);
        return false;
    }

    const int etcItemType = shapeShiftItem->m_const->m_EtcitemType;
    const bool weaponShapeItem = (etcItemType >= EIT_SHAPE_SHIFTING_WP && etcItemType <= EIT_SHAPE_SHIFTING_WP_FIXED) ||
        etcItemType == EIT_RESTORE_SHAPE_SHIFTING_WP;
    if (weaponShapeItem && !IsWeapon()) {
        user->SendSystemMessage(SystemMessageId::WEAPONS_ONLY);
        return false;
    }

    const bool armorShapeItem = (etcItemType >= EIT_SHAPE_SHIFTING_AM && etcItemType <= EIT_SHAPE_SHIFTING_AM_FIXED) ||
        etcItemType == EIT_RESTORE_SHAPE_SHIFTING_AM;
    if (armorShapeItem &&
        (!IsArmor() || m_data->m_Slot == 0x10000 || m_data->m_Slot == 0x40000 || m_data->m_Slot == 0x80000)) {
        user->SendSystemMessage(SystemMessageId::ARMOR_ONLY);
        return false;
    }

    const bool hairShapeItem =
        (etcItemType >= EIT_SHAPE_SHIFTING_HAIRACC && etcItemType <= EIT_SHAPE_SHIFTING_HAIRACC_FIXED) ||
        etcItemType == EIT_RESTORE_SHAPE_SHIFTING_HAIRACC;
    if (hairShapeItem &&
        (!IsArmor() || (m_data->m_Slot != 0x10000 && m_data->m_Slot != 0x40000 && m_data->m_Slot != 0x80000))) {
        user->SendSystemMessage(SystemMessageId::HAIR_ACCESSORIES_ONLY);
        return false;
    }

    if (m_const->m_Crystal == CRT_INVALID) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (!hairShapeItem && m_const->m_Crystal == CRT_NONE) {
        user->SendSystemMessage(SystemMessageId::CANNOT_MODIFY_OR_RESTORE_NO_GRADE_ITEM);
        return false;
    }

    if (etcItemType >= EIT_RESTORE_SHAPE_SHIFTING_WP && etcItemType <= EIT_RESTORE_SHAPE_SHIFTING_ALLITEM &&
        m_data->GetShapeShiftedClassId() == 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }

    if (extraItem == nullptr) {
        return true;
    }
    if (extraItem->m_data->GetLocked() != 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }
    if (extraItem->IsWeapon()) {
        if (m_const->m_AttackType != extraItem->m_const->m_AttackType) {
            user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
            return false;
        }
        if (m_const->m_bIsMagicWeapon != extraItem->m_const->m_bIsMagicWeapon) {
            user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
            return false;
        }
    }
    if (extraItem->IsArmor() && extraItem->m_data->m_Slot != 0x20000 &&
        m_const->m_ArmorType != extraItem->m_const->m_ArmorType) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (m_data->m_Slot != extraItem->m_data->m_Slot) {
        if (extraItem->m_data->m_Slot != 0x20000 && extraItem->m_data->m_Slot != 0x8000) {
            user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
            return false;
        }
        if (m_data->m_Slot != 0x400) {
            user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
            return false;
        }
    }
    if (m_const->m_Crystal < extraItem->m_const->m_Crystal) {
        user->SendSystemMessage(SystemMessageId::CANNOT_EXTRACT_APPEARANCE_FROM_HIGHER_GRADE_ITEM);
        return false;
    }

    const int extraClassId = extraItem->GetClassId();
    if (GetClassId() == extraClassId) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }
    return true;
}

bool CItem::IsShapeShiftableExtractionItem(User* user, CItem* targetItem) {
    // Current L2Server 0x007d7c50.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (!m_const->m_bShapeShiftableExtarction) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }

    const std::int64_t extractionSlot = m_data->m_Slot;
    const std::int64_t targetSlot = targetItem->m_data->m_Slot;
    if (extractionSlot != targetSlot && ((extractionSlot != 0x20000 && extractionSlot != 0x8000) || targetSlot != 0x400)) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }

    if (targetItem->IsWeapon() &&
        (m_const->m_AttackType != targetItem->m_const->m_AttackType ||
            m_const->m_bIsMagicWeapon != targetItem->m_const->m_bIsMagicWeapon)) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (targetItem->IsArmor() && extractionSlot != 0x20000 && m_const->m_ArmorType != targetItem->m_const->m_ArmorType) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (m_data->GetShapeShiftedClassId() > 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_EXTRACT_APPEARANCE_FROM_MODIFIED_ITEM);
        return false;
    }
    if (GetClassId() == targetItem->GetClassId() || m_const->m_Crystal == CRT_INVALID) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (m_const->m_Crystal > targetItem->m_const->m_Crystal) {
        user->SendSystemMessage(SystemMessageId::CANNOT_EXTRACT_APPEARANCE_FROM_HIGHER_GRADE_ITEM);
        return false;
    }
    if (!user->IsEquipedItem(targetItem->m_data->index) || user->CanEquip(this)) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::CANNOT_APPLY_UNEQUIPPABLE_APPEARANCE_TO_EQUIPPED_ITEM);
    return false;
}

ShapeShiftAutoMatchingType::type CItem::GetShapeShiftMatchingType() {
    // Current L2Server 0x007d4360. The target maps each
    // armor legs slot to the matching gloves matching type.
    // m_Slot contains a bitmask. Use the existing ST2SBT
    // table and SlotType names, preserving exact equality and matching results.
    L2_GUARD_FUNCTION();
    using namespace ShapeShiftAutoMatchingType;

    if (IsWeapon()) {
        const AttackType attackType = m_const->m_AttackType;
        const std::int64_t slot = m_data->m_Slot;
        switch (attackType) {
            case AT_RAPIER:
                // if (slot != 0x80) {
                if (slot != ST2SBT[ST_RHAND]) {
                    return SSAMT_MAX;
                }
                return m_const->m_bIsMagicWeapon ? SSAMT_ONE_HAND_MAGIC_SWORD : SSAMT_ONE_HAND_SWORD;
            case AT_SWORD:
                return SSAMT_ONE_HAND_SWORD;
            case AT_BUSTER:
                return SSAMT_ONE_HAND_MAGIC_SWORD;
            case AT_DAGGER:
                // return slot == 0x80
                return slot == ST2SBT[ST_RHAND] ? SSAMT_DAGGER : SSAMT_MAX;
            case AT_TWOHANDSWORD:
                return SSAMT_TWO_HAND_SWORD;
            case AT_ANCIENTSWORD:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_TWO_HAND_SWORD : SSAMT_MAX;
            case AT_DUALFIST:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_DUAL_FIST : SSAMT_MAX;
            case AT_POLE:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_POLE : SSAMT_MAX;
            case AT_BOW:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_BOW : SSAMT_MAX;
            case AT_CROSSBOW:
            case AT_TWOHANDCROSSBOW:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_CROSSBOW : SSAMT_MAX;
            case AT_BLUNT:
                return SSAMT_ONE_HAND_BLUNT;
            case AT_TWOHANDBLUNT:
                return SSAMT_TWO_HAND_BLUNT;
            case AT_STAFF:
                return SSAMT_ONE_HAND_MAGIC_BLUNT;
            case AT_TWOHANDSTAFF:
                return SSAMT_TWO_HAND_MAGIC_BLUNT;
            case AT_DUAL:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_DUAL_SWORD : SSAMT_MAX;
            case AT_DUALDAGGER:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_DUAL_DAGGER : SSAMT_MAX;
            case AT_DUALBLUNT:
                // return slot == 0x4000
                return slot == ST2SBT[ST_RLHAND] ? SSAMT_DUAL_BLUNT : SSAMT_MAX;
            default:
                return SSAMT_MAX;
        }
    }

    if (!IsArmor()) {
        return SSAMT_MAX;
    }

    const ArmorType armorType = m_const->m_ArmorType;
    const std::int64_t slot = m_data->m_Slot;
    switch (armorType) {
        case AMT_NONE:
            // return slot == 0x100 ? SSAMT_SHIELD : SSAMT_MAX
            return slot == ST2SBT[ST_LHAND] ? SSAMT_SHIELD : SSAMT_MAX;
        case AMT_SIGIL:
            // return slot == 0x100 ? SSAMT_SIGIL : SSAMT_MAX
            return slot == ST2SBT[ST_LHAND] ? SSAMT_SIGIL : SSAMT_MAX;
        case AMT_HEAVY:
            switch (slot) {
                // case 0x400:
                case ST2SBT[ST_CHEST]:
                    return SSAMT_HEAVY_CHEST;
                // case 0x200:
                case ST2SBT[ST_GLOVES]:
                // case 0x800:
                case ST2SBT[ST_LEGS]:
                    return SSAMT_HEAVY_GLOVES;
                // case 0x1000:
                case ST2SBT[ST_FEET]:
                    return SSAMT_HEAVY_FEET;
                // case 0x8000:
                case ST2SBT[ST_ONEPIECE]:
                    return SSAMT_HEAVY_ONEPIECE;
                // case 0x20000:
                case ST2SBT[ST_ALLDRESS]:
                    return SSAMT_HEAVY_ALLDRESS;
                default:
                    return SSAMT_MAX;
            }
        case AMT_LIGHT:
            switch (slot) {
                // case 0x400:
                case ST2SBT[ST_CHEST]:
                    return SSAMT_LIGHT_CHEST;
                // case 0x200:
                case ST2SBT[ST_GLOVES]:
                // case 0x800:
                case ST2SBT[ST_LEGS]:
                    return SSAMT_LIGHT_GLOVES;
                // case 0x1000:
                case ST2SBT[ST_FEET]:
                    return SSAMT_LIGHT_FEET;
                // case 0x8000:
                case ST2SBT[ST_ONEPIECE]:
                    return SSAMT_LIGHT_ONEPIECE;
                // case 0x20000:
                case ST2SBT[ST_ALLDRESS]:
                    return SSAMT_LIGHT_ALLDRESS;
                default:
                    return SSAMT_MAX;
            }
        case AMT_MAGIC:
            switch (slot) {
                // case 0x400:
                case ST2SBT[ST_CHEST]:
                    return SSAMT_MAGIC_CHEST;
                // case 0x200:
                case ST2SBT[ST_GLOVES]:
                // case 0x800:
                case ST2SBT[ST_LEGS]:
                    return SSAMT_MAGIC_GLOVES;
                // case 0x1000:
                case ST2SBT[ST_FEET]:
                    return SSAMT_MAGIC_FEET;
                // case 0x8000:
                case ST2SBT[ST_ONEPIECE]:
                    return SSAMT_MAGIC_ONEPIECE;
                // case 0x20000:
                case ST2SBT[ST_ALLDRESS]:
                    return SSAMT_MAGIC_ALLDRESS;
                default:
                    return SSAMT_MAX;
            }
        default:
            return SSAMT_MAX;
    }
}

bool CItem::hUnLockedItem(CItem* targetItem, User* user) {
    // Current L2Server 0x007d66b4.
    L2_GUARD_FUNCTION();
    if (targetItem == nullptr || user == nullptr) {
        return false;
    }

    const bool locked = targetItem->m_data->GetLocked() != 0;
    if (locked) {
        db.RequestUnLockedItem(this, targetItem, user);
        return true;
    }

    user->SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
    protocol::S_EX_LOCKED_RESULT result{};
    result.lock = 1;
    user->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1f8), result);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

bool CItem::hLockedItem(CItem* targetItem, User* user) {
    // Current L2Server 0x007d9178.
    L2_GUARD_FUNCTION();
    if (targetItem == nullptr || user == nullptr) {
        return false;
    }

    if (targetItem->m_data->GetLocked() == 0 && targetItem->m_const->m_bhLockedAble) {
        db.RequestLockedItem(this, targetItem, user);
        return true;
    }

    user->SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
    protocol::S_EX_LOCKED_RESULT result{};
    result.lock = 0;
    user->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1f8), result);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

std::map<int, CItemTempData> g_ItemTempDataDB;
std::map<int, CSetItemTempData> g_SetItemTempDataDB;
std::map<int, std::vector<TempCapsuledItem*>*> g_CapsuledItemTempDataDB;
CItemSmartIdManager g_ItemSmartIdManager;
CConvertItemDB g_ConvertItemDB;
CItemConstInfoDB g_ItemConstInfoDB;
char16_t g_ItemHtml[100000][260]{};

const int CrystalClassByCrystalType[CRT_MAX] = {
    0,
    1458,
    1459,
    1460,
    1461,
    1462,
    1462,
    1462,
    17371,
    17371,
    17371,
    0,
};

const double ArmorEnchantFailTable[20] = {
    0.0,
    0.0,
    0.0,
    0.3333333333333333,
    0.6666666666666666,
    0.75,
    0.8,
    0.8333333333333334,
    0.8571428571428571,
    0.875,
    0.8888888888888888,
    0.9,
    0.9090909090909091,
    0.9166666666666666,
    0.9230769230769231,
    0.9285714285714286,
    0.9333333333333333,
    0.9375,
    0.9411764705882353,
    0.9444444444444444,
};

const double OnePieceEnchantFailTable[20] = {
    0.0,
    0.0,
    0.0,
    0.0,
    0.3333333333333333,
    0.6666666666666666,
    0.75,
    0.8,
    0.8333333333333334,
    0.8571428571428571,
    0.875,
    0.8888888888888888,
    0.9,
    0.9090909090909091,
    0.9166666666666666,
    0.9230769230769231,
    0.9285714285714286,
    0.9333333333333333,
    0.9375,
    0.9411764705882353,
};

const int ArrayCrystalCountByEnchantWeapon[CRT_MAX] = {
    0,
    90,
    45,
    67,
    145,
    250,
    250,
    250,
    500,
    500,
    500,
    0,
};

const int ArrayCrystalCountByEnchantArmor[CRT_MAX] = {
    0,
    11,
    6,
    11,
    20,
    25,
    25,
    25,
    30,
    30,
    30,
    0,
};

CCombinationItem::CCombinationItem() :
    m_nSlotOne(0),
    m_nSlotOneAmount(0),
    m_nSlotTwo(0),
    m_nSlotTwoAmount(0),
    m_nProb(0),
    m_nSuccessResultItem(0),
    m_nSuccessResultItemAmount(0),
    m_nFailResultItem(0),
    m_nFailResultItemAmount(0) {
    L2_GUARD_FUNCTION();
}

CCombinationItem::~CCombinationItem() {
    L2_GUARD_FUNCTION();
    m_nSlotOne = 0;
    m_nSlotOneAmount = 0;
    m_nSlotTwo = 0;
    m_nSlotTwoAmount = 0;
    m_nProb = 0;
    m_nSuccessResultItem = 0;
    m_nSuccessResultItemAmount = 0;
    m_nFailResultItem = 0;
    m_nFailResultItemAmount = 0;
}

void CCombinationItem::SetOne(std::int32_t itemClassId, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    m_nSlotOne = itemClassId;
    m_nSlotOneAmount = amount;
}

void CCombinationItem::SetTwo(std::int32_t itemClassId, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    m_nSlotTwo = itemClassId;
    m_nSlotTwoAmount = amount;
}

void CCombinationItem::SetProb(std::int32_t probability) {
    L2_GUARD_FUNCTION();
    m_nProb = probability;
}

void CCombinationItem::SetResult(
    std::int32_t successItemClassId, std::int64_t successAmount, std::int32_t failItemClassId, std::int64_t failAmount
) {
    L2_GUARD_FUNCTION();
    m_nSuccessResultItem = successItemClassId;
    m_nSuccessResultItemAmount = successAmount;
    m_nFailResultItem = failItemClassId;
    m_nFailResultItemAmount = failAmount;
}

bool CCombinationItem::IsValidItemAmount(std::int32_t itemClassId, std::int64_t amount) {
    L2_GUARD_FUNCTION();

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object != nullptr && object->IsItem()) {
        CItem* const item = object->CastItem();
        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) || amount == 1) {
            return true;
        }
    }
    return false;
}

bool CCombinationItem::IsValidCombinationItem() {
    L2_GUARD_FUNCTION();

    return m_nSlotOneAmount == 1 && IsValidItemAmount(m_nSlotTwo, m_nSlotTwoAmount) && m_nProb > 0 && m_nProb < 101 &&
        IsValidItemAmount(m_nSuccessResultItem, m_nSuccessResultItemAmount) &&
        IsValidItemAmount(m_nFailResultItem, m_nFailResultItemAmount);
}

CConvertItemDB::CConvertItemDB() {}

CConvertItemDB::~CConvertItemDB() = default;

int CConvertItemDB::GetConvertItem(int inputItemId) {
    L2_GUARD_FUNCTION();

    const auto position = m_mapConvertItem.find(inputItemId);
    return position == m_mapConvertItem.end() ? 0 : position->second;
}

bool CConvertItemDB::AddItem(int inputItemId, int outputItemId) {
    L2_GUARD_FUNCTION();

    if (m_mapConvertItem.find(inputItemId) != m_mapConvertItem.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"duplicated input item[%d] (output[%d])",
        //     inputItemId,
        //     outputItemId)
        // L2_LOG_ERROR(Log,
        //     L"duplicated input item[%d] (output[%d])",
        //     inputItemId,
        //     outputItemId)
        L2_LOG_ERROR(
            Log, "Duplicate conversion for input item %d to output item %d ignored; first mapping retained.", inputItemId,
            outputItemId
        );
        return false;
    }
    m_mapConvertItem.emplace(inputItemId, outputItemId);
    return true;
}

void CConvertItemDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\convertdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/convertdata.txt";
#endif

    CSecureStream input(kFileName, false);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Item.cpp",
        //     0x18e3,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Item.cpp",
        //     0x18e3,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read convertdata.txt; item conversion loading stops.");
        return;
    }

    CParserForConvertData parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForConvertData::create failed",
        //     L"Item.cpp",
        //     0x18ec)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForConvertData::create failed",
        //     L"Item.cpp",
        //     0x18ec)
        L2_LOG_ERROR(Log, "Cannot initialize convertdata.txt parser; item conversion loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Convert Data file failed after line[%d]",
        //     L"Item.cpp",
        //     0x18f8,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Convert Data file failed after line[%d]",
        //     L"Item.cpp",
        //     0x18f8,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse convertdata.txt at script line %lld; mappings registered before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CItemConstInfo::GetUseConditionList(std::vector<int>& conditions, UseConditionType type) {
    L2_GUARD_FUNCTION();

    for (CItemUseCondition* const condition : m_vItemUseCondition) {
        if (condition->GetConditionType() == type) {
            condition->GetConditionList(conditions);
            return;
        }
    }
}

bool CItemConstInfo::CanUse(CCreature* creature, CItem* item) {
    L2_GUARD_FUNCTION();

    for (CItemUseCondition* const condition : m_vItemUseCondition) {
        if (condition == nullptr || !condition->CanUse(creature, item)) {
            return false;
        }
    }
    return true;
}

bool CItemConstInfo::CanEquip(CCreature* creature, CItem* item, CheckEquipConditionItemType::Enum itemType) {
    // Current L2Server 0x007da6cc.
    L2_GUARD_FUNCTION();
    for (CItemEquipCondition* const condition : m_vItemEquipCondition) {
        if (condition == nullptr || !condition->CanEquip(creature, item, itemType)) {
            return false;
        }
    }
    return true;
}

void CItemConstInfo::GetNewSetItemIDList(std::vector<int>& result) const {
    // Current L2Server 0x007dfb44.
    L2_GUARD_FUNCTION();
    for (const int setItemId : m_vNewSetId) {
        result.push_back(setItemId);
    }
}

void CItemConstInfo::GetShapeShiftedSetItemIDList(std::vector<int>& result) const {
    // Current L2Server 0x007dfc28.
    L2_GUARD_FUNCTION();
    for (const int setItemId : m_vShapeShiftedSetId) {
        result.push_back(setItemId);
    }
}

bool CItemConstInfo::HasEquipConditionType(const EquipConditionType type) {
    L2_GUARD_FUNCTION();
    for (CItemEquipCondition* const condition : m_vItemEquipCondition) {
        if (condition->GetConditionType() == type) {
            return true;
        }
    }
    return false;
}

void CItemConstInfo::GetCreateItemRandom(std::vector<CreateItemRandomStruct>& items) {
    L2_GUARD_FUNCTION();

    for (const CreateItemRandomStruct& item : m_vCreateItemList) {
        items.push_back(item);
    }
}

void CItemConstInfo::GetCreateItem(std::vector<CreateItemStruct>& items) {
    L2_GUARD_FUNCTION();

    for (const CreateItemStruct& item : m_vCreateItemList2) {
        items.push_back(item);
    }
}

int CItemConstInfo::GetMaxUnpackableCapsuledItemCount() {
    // Current L2Server 0x007d8370.
    L2_GUARD_FUNCTION();
    int count = 0;
    for (const CapsuledItem* const capsule : m_vCapsuledItemList) {
        if (capsule == nullptr) {
            continue;
        }
        CObject* const object = g_ObjectDB.GetObjectW(capsule->nItemClassId, false);
        if (object != nullptr && object->IsItem() && !object->IsArtifact()) {
            ++count;
        }
    }
    return count;
}

int CItemConstInfo::GetMaxUnpackableCapsuledArtifactCount() {
    // Current L2Server 0x007d8478.
    L2_GUARD_FUNCTION();
    int count = 0;
    for (const CapsuledItem* const capsule : m_vCapsuledItemList) {
        if (capsule == nullptr) {
            continue;
        }
        CObject* const object = g_ObjectDB.GetObjectW(capsule->nItemClassId, false);
        if (object != nullptr && object->IsItem() && object->IsArtifact()) {
            ++count;
        }
    }
    return count;
}

void CItemConstInfo::GetUnpackableCapsuledItemList(std::vector<CapsuledItem*>& items) {
    // Current L2Server 0x007e0a84.
    L2_GUARD_FUNCTION();
    for (CapsuledItem* const capsule : m_vCapsuledItemList) {
        if (capsule != nullptr && genrand64_real1() * 100.0 <= capsule->nProb) {
            items.push_back(capsule);
        }
    }
}

std::vector<CreateItemStructInUseCount>& CItemConstInfo::GetCreateItemsInUseCount(const std::int16_t useCount) {
    // Current L2Server 0x007e42c8.
    L2_GUARD_FUNCTION();
    const auto found = m_mFixedCreateItemsInUseCount.find(useCount);
    return found == m_mFixedCreateItemsInUseCount.end() ? m_vCreateItemsInUseCount : found->second;
}

CItemConstInfo* CItemConstInfoDB::GetItemConstInfo(int itemClassId) {
    L2_GUARD_FUNCTION();

    const auto found = m_mItemConstInfo.find(itemClassId);
    return found == m_mItemConstInfo.end() ? nullptr : found->second;
}

void CItemConstInfo::AddFixedCreateItemsInUseCount(std::int16_t fixedNumber, CreateItemStructInUseCount item) {
    L2_GUARD_FUNCTION();

    auto found = m_mFixedCreateItemsInUseCount.find(fixedNumber);
    if (found == m_mFixedCreateItemsInUseCount.end()) {
        std::vector<CreateItemStructInUseCount> items;
        items.push_back(item);
        auto inserted = m_mFixedCreateItemsInUseCount.emplace(fixedNumber, std::vector<CreateItemStructInUseCount>{});
        inserted.first->second = items;
        return;
    }
    found->second.push_back(item);
}

bool CItemConstInfo::HasItemPropertyList(const VALIDATE::Enum type) {
    // Current L2Server 0x007da7b0.
    L2_GUARD_FUNCTION();
    return std::find(m_vItemProperty.begin(), m_vItemProperty.end(), type) != m_vItemProperty.end();
}

void CItemConstInfo::SetItemProperty(const VALIDATE::Enum type) {
    L2_GUARD_FUNCTION();
    m_vItemProperty.push_back(type);
}

void CItemConstInfo::SetCrystalItem(const int itemId, const int crystalCount, const double probability) {
    L2_GUARD_FUNCTION();
    m_CrystalItemList.push_back(CrystalItemStruct{itemId, static_cast<std::int64_t>(crystalCount), probability});
}

void CItemConstInfo::GetCrystalItemList(std::vector<CrystalItemStruct>& items) {
    // Current L2Server 0x007df67c appends without clearing the destination.
    L2_GUARD_FUNCTION();
    for (const CrystalItemStruct& item : m_CrystalItemList) {
        items.push_back(item);
    }
}

void CItemConstInfo::SetCreateItemRandom(
    const int itemId, const std::int64_t count, const double probability, const int enchant
) {
    L2_GUARD_FUNCTION();
    m_vCreateItemList.push_back(CreateItemRandomStruct{itemId, count, probability, enchant});
}

void CItemConstInfo::SetCreateItem(const int itemId, const std::int64_t count, const int enchant) {
    L2_GUARD_FUNCTION();
    m_vCreateItemList2.push_back(CreateItemStruct(itemId, count, enchant));
}

void CItemConstInfo::SetCreateItemRandomGroup(const std::vector<std::vector<CreateItemRandomStruct>>& groups) {
    L2_GUARD_FUNCTION();
    for (const auto& group : groups) {
        m_vCreateItemGroupList.push_back(group);
    }
}

void CItemConstInfoDB::AddItemConstInfo(int itemClassId, CItemConstInfo* itemConstInfo) {
    L2_GUARD_FUNCTION();

    if (itemConstInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] invalid item const info added to DB[%d]",
        //     "Item.cpp",
        //     0x1a9b,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] invalid item const info added to DB[%d]",
        //     "Item.cpp",
        //     0x1a9b,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Cannot register constant data for item class %d: data pointer is null.", itemClassId);
        return;
    }

    if (m_mItemConstInfo.find(itemClassId) == m_mItemConstInfo.end()) {
        m_mItemConstInfo.emplace(itemClassId, itemConstInfo);
    }
}

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CItem)

CItem::CItem(bool createFail) :
    m_data(nullptr),
    m_const(nullptr),
    m_lock(nullptr),
    m_nTraceCharId(0),
    m_nTraceNPCClassId(0),
    m_bTraceIsBoss(false),
    padding_79{},
    m_Useable(1),
    m_eChangeAttribute(ATTRT_NONE),
    m_nDroppedTick(0),
    m_bDroppedByAI(false),
    padding_85{},
    padding_8c{},
    m_itemOption(ItemOptionKey{}),
    padding_142{},
    m_pEnchantOption(nullptr),
    m_bRealItem(false),
    padding_151{},
    m_ItemOwnerId(0),
    m_durationCommitTick(0),
    padding_1bc{},
    m_mEnsoulInfo{} {
    L2_GUARD_FUNCTION();
    if (createFail) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"[%S][%d]Item Create fail.",
        //     "Item.cpp",
        //     0xb5)
        // L2_LOG(Log,
        //     LOG_LOCAL,
        //     L"[%S][%d]Item Create fail.",
        //     "Item.cpp",
        //     0xb5)
        L2_LOG_ERROR_WITH_LOCAL_COPY(Log, "Item creation failed; requesting a crash report.");
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Item Create fail.",
        //     "Item.cpp",
        //     0xb6)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Item Create fail.",
        //     "Item.cpp",
        //     0xb6)
        // Noise: the preceding ERROR_WITH_LOCAL_COPY already keeps both error and mandatory local destinations.
        ForceCrash(CrashInfo::ITEM_CREATION_IS_FAILED);
        std::abort();
    }

    CItemFactory& factory = CItemFactory::Inst();
    m_data = factory.AddPrototype(this);
    m_const = factory.GetConstant(m_data->const_index);
}

CItem::CItem(const CItem& other) :
    CWorldObject(other),
    m_data(nullptr),
    m_const(nullptr),
    m_lock(nullptr),
    m_nTraceCharId(0),
    m_nTraceNPCClassId(0),
    m_bTraceIsBoss(false),
    padding_79{},
    m_Useable(1),
    m_eChangeAttribute(ATTRT_NONE),
    m_nDroppedTick(0),
    m_bDroppedByAI(false),
    padding_85{},
    padding_8c{},
    m_itemOption(other.m_itemOption),
    m_itemBaseAttribute(other.m_itemBaseAttribute),
    padding_142{},
    m_pEnchantOption(other.m_pEnchantOption),
    m_bRealItem(true),
    padding_151{},
    m_ItemOwnerId(0),
    m_ItemSkillList(other.m_ItemSkillList),
    m_ItemEquipSkillList(other.m_ItemEquipSkillList),
    m_ItemUnequipSkillList(other.m_ItemUnequipSkillList),
    m_durationCommitTick(0),
    padding_1bc{},
    m_mEnsoulInfo{} {
    L2_GUARD_FUNCTION();

    CItemFactory& factory = CItemFactory::Inst();
    m_data = factory.Add(this);
    m_data->CopyFrom(other.m_data);
    m_const = factory.GetConstant(m_data->const_index);
    m_nClassId = m_data->m_nClassID;

    for (int slot = 1; slot < 3; ++slot) {
        const auto& source = other.m_mEnsoulInfo[slot];
        if (!source.empty() && source.size() <= static_cast<std::size_t>(m_const->m_ensoulSlotCount[slot])) {
            m_mEnsoulInfo[slot] = source;
        }
    }
}

CItem::~CItem() {
    L2_GUARD_FUNCTION();
    if (m_bRealItem) {
        CItemFactory::Inst().Remove(this);
        g_ServerContext.Item.fetch_sub(1, std::memory_order_seq_cst);
    }
}

L2_DEFINE_OBJECT_RELEASE(CItem)

void CItem::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    if (id == -1) {
        if (RefCount() > 1) {
            ReportOrt(kItemSource, 0x528);
        }
        return;
    }
    if (!m_bDeleting && m_nDeleteCount == 0 && (m_ContributeDataID.Sid() != 0 || m_bDroppedByAI)) {
        CWorld::Inst().PickAndDeleteItem(this);
    }
}

CObject* CItem::Copy() {
    L2_GUARD_FUNCTION();
    return new CItem(*this);
}

CItem* CItem::CopyFakeItem() {
    L2_GUARD_FUNCTION();
    CItem* const item = new CItem(*this);
    item->OnCreateFakeItem();
    return item;
}

bool CItem::IsItem() const { return true; }

CItem* CItem::CastItem() { return this; }

CEtcItem* CItem::CastEtcItem() { return nullptr; }

std::int64_t CItem::GetPreviewPrice() const {
    // Current L2Server 0x007d34dc.
    // use CrystalType names. Preview prices remain unchanged.
    // switch (static_cast<int>(m_const->GetCrystalType())) {
    switch (m_const->GetCrystalType()) {
        // case 0:
        case CRT_NONE:
            // return 10
            return PREVIEW_CRT_NONE;
        // case 1:
        case CRT_D:
            // return 50
            return PREVIEW_CRT_D;
        // case 2:
        case CRT_C:
            // return 100
            return PREVIEW_CRT_C;
        // case 3:
        case CRT_B:
            // return 200
            return PREVIEW_CRT_B;
        // case 4:
        case CRT_A:
            // return 500
            return PREVIEW_CRT_A;
        // case 5:
        case CRT_S:
        // case 6:
        case CRT_S80:
        // case 7:
        case CRT_S84:
            // return 1000
            return PREVIEW_CRT_S;
        // case 8:
        case CRT_R:
        // case 9:
        case CRT_R95:
        // case 10:
        case CRT_R99:
            // return 2000
            return PREVIEW_CRT_R;
        default:
            return 0;
    }
}

bool CItem::IsPetCollarItem() { return false; }

bool CItem::IsEnchantableItem() const {
    // Current L2Server 0x007df0f0.
    L2_GUARD_FUNCTION();
    const int enchantType = m_const->m_nItemEnchantType;
    if (enchantType == 0 || (m_const->m_nDuration > 0 && enchantType == 1)) {
        return false;
    }

    const int maxEnchantLevel = CEnchantOptionDB::Inst().GetMaxEnchantLevel(GetClassId());
    return maxEnchantLevel < 1 || m_data->GetEnchanted() < maxEnchantLevel;
}

bool CItem::IsInVehicle() { return CContainerDB::IsVehicleContainerId(m_data->container_index); }

bool CItem::IsInAirShip() { return CContainerDB::IsAirShipContainerId(m_data->container_index); }

void CItem::SetDBID(unsigned int dbId) {
    nDBID = dbId;
    m_data->m_nDBID = static_cast<std::int32_t>(dbId);
}

FVector CItem::GetPos() const { return m_data->GetPos(); }

FAngle CItem::GetDir() const { return m_data->GetDir(); }

int CItem::GetIndex() const { return m_data->index; }

void CItem::DecideDistance(CMoveToTargetShareData* data, CSmartID) {
    data->m_nDistance = 20;
    data->m_nAcceptableDistance = 30;
}

const char16_t* CItem::GetName() const {
    L2_GUARD_FUNCTION();
    return g_ObjectDB.GetName(m_data->m_nClassID);
}

void CItem::OnCreate() {
    L2_GUARD_FUNCTION();
    m_nId = g_ItemSmartIdManager.RegisterObject(
        // 8,
        SCLS_ITEM, static_cast<std::uint32_t>(m_data->index), this
    );
    CItemFactory::Inst().PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = static_cast<std::int32_t>(m_nId);
    g_ServerContext.Item.fetch_add(1, std::memory_order_seq_cst);
}

void CItem::OnCreateFakeItem() {
    L2_GUARD_FUNCTION();
    m_nId = g_ItemSmartIdManager.RegisterObject(
        // 6,
        SCLS_FAKE_ITEM, static_cast<std::uint32_t>(m_data->index), this
    );
    CItemFactory::Inst().PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = static_cast<std::int32_t>(m_nId);
    g_ServerContext.Item.fetch_add(1, std::memory_order_seq_cst);
}

void CItem::Delete() {
    L2_GUARD_FUNCTION();
    const std::int32_t count = runtime::raw_atomic::IncrementI32(&m_nDeleteCount);
    if (count > 1) {
        const wchar_t* const caller = runtime::process::CurrentGuardCaller();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid item::delete (%s)",
        //     L"Item.cpp",
        //     0x176,
        //     caller ? caller : L"callStackError")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid item::delete (%s)",
        //     L"Item.cpp",
        //     0x176,
        //     caller ? caller : L"callStackError")
        L2_LOG_ERROR(
            Log, "Item object %u received delete request %d from %s; duplicate deletion rejected and stack reported.",
            static_cast<unsigned>(m_nId), count, caller ? WideToUtf8(caller).c_str() : "<unknown>"
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    if (CInstantZone* const zone = GetInstantZone()) {
        zone->LeaveZone(this);
    }
    m_bDeleting = true;
    m_pEnchantOption = nullptr;
    const wchar_t* const caller = runtime::process::CurrentGuardCaller();
    static_cast<void>(new CItemReleaseVerifier(this, caller ? caller : L"callStackError"));
    SetDelete();
    g_ItemSmartIdManager.DeleteObject(m_nId);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

bool CItem::IsTradable(User* user, bool flag) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || !IsTrade()) {
        return false;
    }
    if (IsPetCollarItem()) {
        if (user->m_nPetItemCount > 0) {
            return false;
        }
        CPet* const pet = user->GetPet();
        if (pet != nullptr && GetDBID() == pet->GetDBID()) {
            return false;
        }
    }
    if (IsQuestItem()) {
        return false;
    }
    if ((m_itemOption.itemOptionKey.variationOption1 >= 1 && m_itemOption.itemOptionKey.variationOption2 >= 1) ||
        m_const->m_nDuration >= 1 || m_const->m_period >= 1) {
        return false;
    }
    if (!flag && user->IsEquipedItem(m_data->index)) {
        return false;
    }
    if (IsPeriodicShapeShiftedItem(user)) {
        return false;
    }
    return m_data->GetLocked() == 0;
}

bool CItem::IsNPCTradable(User* user, bool flag) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || !IsNPCTrade()) {
        return false;
    }
    if (IsPetCollarItem()) {
        if (user->m_nPetItemCount > 0) {
            return false;
        }
        CPet* const pet = user->GetPet();
        if (pet != nullptr && GetDBID() == pet->GetDBID()) {
            return false;
        }
    }
    if (IsQuestItem()) {
        return false;
    }
    if ((m_itemOption.itemOptionKey.variationOption1 >= 1 && m_itemOption.itemOptionKey.variationOption2 >= 1) ||
        m_const->m_nDuration >= 1 || m_const->m_period >= 1) {
        return false;
    }
    if (!flag && user->IsEquipedItem(m_data->index)) {
        return false;
    }
    if (m_data->GetLocked() != 0) {
        return false;
    }
    return !IsPeriodicShapeShiftedItem(user);
}

bool CItem::CanGetByPet(User* user) {
    // Current L2Server.exe 0x007d9b18, original Item.cpp owner.
    L2_GUARD_FUNCTION();
    if (!IsTrade()) {
        return false;
    }

    const std::int32_t etcItemType = m_const->m_EtcitemType;
    if (etcItemType == EIT_PET_COLLAR) {
        return false;
    }
    if (etcItemType == EIT_ARROW) {
        return false;
    }
    if (etcItemType == EIT_BOLT) {
        return false;
    }
    if (etcItemType == EIT_LURE) {
        return false;
    }

    const ItemDefaultActionEnum defaultAction = m_const->m_enumDefaultAction;
    if (defaultAction == ITEM_ACTION_SOULSHOT) {
        return false;
    }
    if (defaultAction == ITEM_ACTION_SPIRITSHOT) {
        return false;
    }
    if (defaultAction == ITEM_ACTION_BLESS_SPIRITSHOT) {
        return false;
    }
    if (defaultAction == ITEM_ACTION_CAPSULE) {
        return false;
    }
    if (defaultAction == ITEM_ACTION_FISHINGSHOT) {
        return false;
    }

    if (IsArtifact()) {
        return false;
    }
    return !IsPeriodicShapeShiftedItem(user);
}

bool CItem::PostLoad() {
    L2_GUARD_FUNCTION();
    const auto found = g_ItemTempDataDB.find(static_cast<int>(m_nId));
    if (found == g_ItemTempDataDB.end()) {
        return true;
    }

    const CItemTempData& staged = found->second;
    ResolveFixedItemSkill(*this, staged.m_ItemSkill, m_const->m_pSkill);
    ResolveFixedItemSkill(*this, staged.m_CriticalSkill, m_const->m_pCriticalSkill);
    ResolveFixedItemSkill(*this, staged.m_AttackSkill, m_const->m_pAttackSkill);
    ResolveFixedItemSkill(*this, staged.m_MagicSkill, m_const->m_pMagicSkill);
    ResolveFixedItemSkill(*this, staged.m_EnchantedFourSkill, m_const->m_pEnchantedFourSkill);

    // bool result = ResolveItemSkillList(
    //     *this,
    //     staged.m_ItemSkillList,
    //     m_ItemSkillList,
    //     L"Skill in %s is not valid.")
    bool result = ResolveItemSkillList(*this, staged.m_ItemSkillList, m_ItemSkillList, "item skill");
    // result = ResolveItemSkillList(
    //              *this,
    //              staged.m_ItemUnequipSkillList,
    //              m_ItemUnequipSkillList,
    //              L"Unequip Skill in %s is not valid.") &&
    //     result.
    result = ResolveItemSkillList(*this, staged.m_ItemUnequipSkillList, m_ItemUnequipSkillList, "unequip skill") && result;
    // result = ResolveItemSkillList(
    //              *this,
    //              staged.m_ItemEquipSkillList,
    //              m_ItemEquipSkillList,
    //              L"equip Skill in %s is not valid.") &&
    //     result.
    result = ResolveItemSkillList(*this, staged.m_ItemEquipSkillList, m_ItemEquipSkillList, "equip skill") && result;
    return result;
}

bool CItem::CheckItemUseCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();

    if (m_data->m_Type == IT_ETCITEM) {
        CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(m_data->m_nClassID);
        if (itemConstInfo != nullptr) {
            return itemConstInfo->CanUse(creature, this);
        }
    }
    return true;
}

bool CItem::CanUseWithCrystalType(const CrystalType crystalType) {
    // Current L2Server 0x007d354c, original Item.cpp owner.
    if (m_const->m_enumDefaultAction == ITEM_ACTION_SKILL_REDUCE) {
        const CrystalType itemCrystal = m_const->GetCrystalType();
        if (itemCrystal != CRT_MAX && crystalType != static_cast<CrystalType>(BaseCrystal[static_cast<int>(itemCrystal)])) {
            return false;
        }
    }
    return true;
}

bool CItem::CheckItemEquipCondition(CCreature* creature, int shapeShiftClassId) {
    // Current L2Server 0x007dc524.
    L2_GUARD_FUNCTION();

    CItemConstInfo* itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(m_data->m_nClassID);
    if (itemConstInfo != nullptr && !itemConstInfo->CanEquip(creature, this, CheckEquipConditionItemType::CECIT_NORMAL)) {
        return false;
    }

    if (shapeShiftClassId == 0) {
        return true;
    }
    itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(shapeShiftClassId);
    return itemConstInfo == nullptr ||
        itemConstInfo->CanEquip(creature, this, CheckEquipConditionItemType::CECIT_SHAPESHIFT);
}

std::map<int, CNewSetItemTempData> g_NewSetItemTempDataDB;
std::map<int, CShapeShiftedSetItemTempData> g_ShapeShiftedSetItemTempDataDB;

CNewSetItem::CNewSetItem() :
    m_nNewSetItemId(0),
    m_nEnchantNum(0),
    m_nMaxPartsNum(0),
    m_pSkill(nullptr),
    m_pAdditionalSkill(nullptr),
    m_pAdditional3Skill(nullptr) {
    L2_GUARD_FUNCTION();
}

CNewSetItem::~CNewSetItem() {
    L2_GUARD_FUNCTION();
    m_nNewSetItemId = 0;
    m_vAdditionalList.clear();
    m_pSkill = nullptr;
    m_pAdditionalSkill = nullptr;
    m_pAdditional3Skill = nullptr;
    m_nEnchantNum = 0;
    m_nMaxPartsNum = 0;
}

void CNewSetItem::Set(int setItemId) {
    L2_GUARD_FUNCTION();
    m_nNewSetItemId = static_cast<std::uint32_t>(setItemId);
}

int CNewSetItem::GetSlotNewSetItemId(int slot) const {
    L2_GUARD_FUNCTION();

    const auto position = m_mapSlotToSetItemId.find(static_cast<SlotType>(slot));
    if (position == m_mapSlotToSetItemId.end()) {
        return 0;
    }
    return position->second.front();
}

void CNewSetItem::PushNewSetItemToUser(User& user, int enchanted) {
    // Current L2Server 0x00a440d0.
    L2_GUARD_FUNCTION();
    for (int slot = 0; slot < 0x3f; ++slot) {
        if (GetSlotNewSetItemId(slot) == 0) {
            continue;
        }
        InventoryParams parameters;
        parameters.nItemClassID = GetSlotNewSetItemId(slot);
        parameters.nCount = 1;
        parameters.nEnchanted = enchanted;
        static_cast<void>(user.AddItemToInventory(parameters));
    }
}

void CNewSetItem::DeleteAllSkills(CCreature* creature) {
    // Current L2Server 0x00a43668.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }
    if (m_pSkill != nullptr) {
        creature->DeleteAcquireSkill(m_pSkill->m_nSkillId);
    }
    if (m_pAdditionalSkill != nullptr) {
        creature->DeleteAcquireSkill(m_pAdditionalSkill->m_nSkillId);
    }
    for (const auto& entry : m_pEnchantSetSkillMap) {
        if (entry.second != nullptr) {
            creature->DeleteAcquireSkill(entry.second->m_nSkillId);
        }
    }
    // Current calls the additional-skill deletion twice.
    if (m_pAdditionalSkill != nullptr) {
        creature->DeleteAcquireSkill(m_pAdditionalSkill->m_nSkillId);
    }
    if (m_pAdditional3Skill != nullptr) {
        creature->DeleteAcquireSkill(m_pAdditional3Skill->m_nSkillId);
    }
}

CSkillInfo* CNewSetItem::FindSetItemEnchantSkill(int enchant) const {
    // Current L2Server 0x00a438b0.
    L2_GUARD_FUNCTION();
    if (enchant <= 5) {
        return nullptr;
    }
    const int key = enchant < 11 ? enchant : 10;
    const auto position = m_pEnchantSetSkillMap.find(key);
    return position != m_pEnchantSetSkillMap.end() ? position->second : nullptr;
}

void CNewSetItem::SetSlotItemToSetID(std::vector<int>* itemClassIds) {
    L2_GUARD_FUNCTION();

    const std::vector<int> copiedIds(*itemClassIds);
    for (const int itemClassId : copiedIds) {
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr) {
            continue;
        }

        CItem* const item = static_cast<CItem*>(object);
        CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(item->m_data->m_nClassID);
        if (itemConstInfo != nullptr) {
            itemConstInfo->m_vNewSetId.push_back(static_cast<int>(m_nNewSetItemId));
        }
    }
}

void CNewSetItem::SetAdditionalSlotItemToSetID(CItem* item) {
    L2_GUARD_FUNCTION();

    if (item == nullptr) {
        return;
    }
    CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(item->m_data->m_nClassID);
    if (itemConstInfo != nullptr) {
        itemConstInfo->m_vNewSetId.push_back(static_cast<int>(m_nNewSetItemId));
    }
}

void CNewSetItem::SetAdditionalItemId(std::vector<int>* itemClassIds) {
    L2_GUARD_FUNCTION();

    const std::vector<int> copiedIds(*itemClassIds);
    m_vAdditionalList = copiedIds;
    for (const int itemClassId : copiedIds) {
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object != nullptr) {
            SetAdditionalSlotItemToSetID(static_cast<CItem*>(object));
        }
    }
}

void CNewSetItem::SetSlotTypeItemId(NewSetItemSlotMap value) {
    L2_GUARD_FUNCTION();
    m_mapSlotToSetItemId = value;
}

void CNewSetItem::SetSkill(char16_t* skillName) {
    L2_GUARD_FUNCTION();
    if (std::u16string_view(skillName) != u"none") {
        g_NewSetItemTempDataDB[m_nNewSetItemId].m_wsSetSkillName.assign(std::u16string_view(skillName));
    }
}

void CNewSetItem::SetAdditionalSkill(char16_t* skillName) {
    L2_GUARD_FUNCTION();
    if (std::u16string_view(skillName) != u"none") {
        g_NewSetItemTempDataDB[m_nNewSetItemId].m_wsAdditionalSkillName.assign(std::u16string_view(skillName));
    }
}

void CNewSetItem::AddEnchantSkill(int enchant, char16_t* skillName) {
    L2_GUARD_FUNCTION();
    if (std::u16string_view(skillName) != u"none") {
        std::u16string storedName;
        storedName.assign(std::u16string_view(skillName));
        g_NewSetItemTempDataDB[m_nNewSetItemId].m_mapEnchantSkillName.emplace(enchant, storedName);
    }
}

void CNewSetItem::SetAdditional3Skill(char16_t* skillName) {
    L2_GUARD_FUNCTION();
    if (std::u16string_view(skillName) != u"none") {
        g_NewSetItemTempDataDB[m_nNewSetItemId].m_wsAdditional3SkillName.assign(std::u16string_view(skillName));
    }
}

bool CNewSetItem::PostLoad() {
    L2_GUARD_FUNCTION();

    const auto position = g_NewSetItemTempDataDB.find(m_nNewSetItemId);
    if (position == g_NewSetItemTempDataDB.end()) {
        return true;
    }

    CNewSetItemTempData& staged = g_NewSetItemTempDataDB[m_nNewSetItemId];
    if (!staged.m_wsSetSkillName.empty()) {
        const std::wstring skillName = Utf16ToWide(staged.m_wsSetSkillName);
        m_pSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    }
    if (!staged.m_wsAdditionalSkillName.empty()) {
        const std::wstring skillName = Utf16ToWide(staged.m_wsAdditionalSkillName);
        m_pAdditionalSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    }
    for (const auto& entry : staged.m_mapEnchantSkillName) {
        if (!entry.second.empty()) {
            const std::wstring skillName = Utf16ToWide(entry.second);
            CSkillInfo* const skill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
            m_pEnchantSetSkillMap.emplace(entry.first, skill);
        }
    }
    if (!staged.m_wsAdditional3SkillName.empty()) {
        // Current 0x00a45ff4 checks the fourth staged string but resolves the
        // second one. Preserve that shipped behavior literally.
        const std::wstring skillName = Utf16ToWide(staged.m_wsAdditionalSkillName);
        m_pAdditional3Skill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    }
    return true;
}

CShapeShiftedSetItem::CShapeShiftedSetItem() : m_nShapeShiftedSetItemId(0), m_pSkill(nullptr) {
    L2_GUARD_FUNCTION();
}

CShapeShiftedSetItem::~CShapeShiftedSetItem() {
    L2_GUARD_FUNCTION();
    m_nShapeShiftedSetItemId = 0;
    m_pSkill = nullptr;
}

bool CShapeShiftedSetItem::PostLoad() {
    L2_GUARD_FUNCTION();

    const auto position = g_ShapeShiftedSetItemTempDataDB.find(static_cast<int>(m_nShapeShiftedSetItemId));
    if (position == g_ShapeShiftedSetItemTempDataDB.end()) {
        return true;
    }

    CShapeShiftedSetItemTempData& staged = g_ShapeShiftedSetItemTempDataDB[static_cast<int>(m_nShapeShiftedSetItemId)];
    if (!staged.m_wsSetSkillName.empty()) {
        const std::wstring skillName = Utf16ToWide(staged.m_wsSetSkillName);
        m_pSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    }
    return true;
}

void CShapeShiftedSetItem::Set(int shapeShiftedSetItemId) {
    L2_GUARD_FUNCTION();
    m_nShapeShiftedSetItemId = static_cast<std::uint32_t>(shapeShiftedSetItemId);
}

void CShapeShiftedSetItem::SetSlotTypeItemId(ShapeShiftedSetItemSlotMap value) {
    L2_GUARD_FUNCTION();
    m_mapSlotToSetItemId = value;
}

void CShapeShiftedSetItem::SetSlotItemToSetID(std::vector<int>* itemClassIds) {
    L2_GUARD_FUNCTION();

    const std::vector<int> copiedIds(*itemClassIds);
    for (const int itemClassId : copiedIds) {
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr) {
            continue;
        }

        CItem* const item = static_cast<CItem*>(object);
        CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(item->m_data->m_nClassID);
        if (itemConstInfo != nullptr) {
            itemConstInfo->m_vShapeShiftedSetId.push_back(static_cast<int>(m_nShapeShiftedSetItemId));
        }
    }
}

void CShapeShiftedSetItem::SetSkill(char16_t* skillName) {
    L2_GUARD_FUNCTION();
    if (std::u16string_view(skillName) != u"none") {
        g_ShapeShiftedSetItemTempDataDB[static_cast<int>(m_nShapeShiftedSetItemId)].m_wsSetSkillName.assign(
            std::u16string_view(skillName)
        );
    }
}

void CItem::ActivateEquipSkillList(CCreature* creature, CWorldObject* target) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr) {
        return;
    }

    for (const CSkillInfo* const storedSkill : m_ItemEquipSkillList) {
        CSkillInfo* const skill = const_cast<CSkillInfo*>(storedSkill);
        skill->m_nCapsuleReduceItemID = m_data->m_nClassID;

        const int result = skill->CheckOperateCond(creature, target, CSkillInfo::OCT_BOTH);
        if (result == -2) {
            skill->ActivateSkill(creature, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        } else if (creature->IsPC()) {
            CUserSocket* const socket = creature->CastUser()->GetSocket();
            if (result == -1) {
                socket->Send(
                    "chccdd", 0x62, SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET, 1, 4, skill->m_nSkillId,
                    (static_cast<int>(skill->m_nSubLevel) << 16) | static_cast<std::uint16_t>(skill->m_nLevel)
                );
            } else {
                socket->SendSystemMessage(static_cast<unsigned int>(result));
            }
        }
    }
}

void CItem::ActivateMultiSkillList(CCreature* const creature, CWorldObject* const target) {
    // Current L2Server 0x007d930c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr) {
        return;
    }

    for (const CSkillInfo* const storedSkill : m_ItemSkillList) {
        CSkillInfo* const skill = const_cast<CSkillInfo*>(storedSkill);
        if (skill == nullptr) {
            continue;
        }
        skill->m_nCapsuleReduceItemID = m_data->m_nClassID;
        const int result = skill->CheckOperateCond(creature, target, CSkillInfo::OCT_BOTH);
        if (result == -2) {
            skill->ActivateSkill(creature, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        } else if (creature->IsPC() && result != -1) {
            creature->CastUser()->GetSocket()->SendSystemMessage(static_cast<unsigned int>(result));
        }
    }
}

void CItem::ActivateUnequipSkillList(CCreature* creature, CWorldObject* target) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr) {
        return;
    }

    for (const CSkillInfo* const storedSkill : m_ItemUnequipSkillList) {
        CSkillInfo* const skill = const_cast<CSkillInfo*>(storedSkill);
        skill->m_nCapsuleReduceItemID = m_data->m_nClassID;

        const int result = skill->CheckOperateCond(creature, target, CSkillInfo::OCT_BOTH);
        if (result == -2) {
            skill->ActivateSkill(creature, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        } else if (creature->IsPC()) {
            CUserSocket* const socket = creature->CastUser()->GetSocket();
            if (result == -1) {
                socket->Send(
                    "chccdd", 0x62, SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET, 1, 4, skill->m_nSkillId,
                    (static_cast<int>(skill->m_nSubLevel) << 16) | static_cast<std::uint16_t>(skill->m_nLevel)
                );
            } else {
                socket->SendSystemMessage(static_cast<unsigned int>(result));
            }
        }
    }
}
double g_dEnchantWeaponExBonus[CRT_MAX][AT_MAX][2][VALIDATE::EXTRA_MAX]{};
int g_nEnchantedArmorBonus[CRT_R99 + 1][20]{};
int g_nOnepieceFactor = 0;

void CItem::SetValidateParam(VALIDATE::Enum type, double value) {
    L2_GUARD_FUNCTION();
    m_const->m_dValidate[static_cast<int>(type)] = value;
}

double CItem::GetValidateParam(VALIDATE::Enum type) {
    L2_GUARD_FUNCTION();
    return m_const->m_dValidate[static_cast<int>(type)];
}

void CItem::ShowEquipItemInfo(User* user) {
    // Current L2Server 0x007d397c, original owner Item.cpp.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 512> message{};
    FormatUtf16To(
        message.data(), message.size(),
        u"Equip Item Info PDefend[%d], MDefend[%d], "
        u"PAttack[%d], MAttack[%d], PAttackSpeed[%d], "
        u"MAttackSpeed[%d], PSkillSpeed[%d], PHit[%d], "
        u"MHit[%d], PCritical[%d], MCritical[%d], "
        u"Speed[%d], ShieldDefense[%d], "
        u"ShieldDefenseRate[%d], PAvoid[%d]",
        static_cast<int>(GetValidateParam(VALIDATE::PDEFEND)), static_cast<int>(GetValidateParam(VALIDATE::MDEFEND)),
        static_cast<int>(GetValidateParam(VALIDATE::PATTACK)), static_cast<int>(GetValidateParam(VALIDATE::MATTACK)),
        static_cast<int>(GetValidateParam(VALIDATE::PATTACKSPEED)),
        static_cast<int>(GetValidateParam(VALIDATE::MATTACKSPEED)),
        static_cast<int>(GetValidateParam(VALIDATE::PSKILLSPEED)), static_cast<int>(GetValidateParam(VALIDATE::PHIT)),
        static_cast<int>(GetValidateParam(VALIDATE::MHIT)), static_cast<int>(GetValidateParam(VALIDATE::PCRITICAL)),
        static_cast<int>(GetValidateParam(VALIDATE::MCRITICAL)), static_cast<int>(GetValidateParam(VALIDATE::SPEED)),
        static_cast<int>(GetValidateParam(VALIDATE::SHIELDDEFENSE)),
        static_cast<int>(GetValidateParam(VALIDATE::SHIELDDEFENSERATE)), static_cast<int>(GetValidateParam(VALIDATE::PAVOID))
    );
    user->GetSocket()->SendSystemMessage(u"SYS", message.data());
}

void CItem::ShowItemInfo(User& user) {
    // Current L2Server 0x007d3c14.
    L2_GUARD_FUNCTION();

    const FVector itemPosition = GetPos();
    const FVector userPosition = user.GetPos();
    const double deltaX = itemPosition.x - userPosition.x;
    const double deltaY = itemPosition.y - userPosition.y;
    const double distanceSquared = deltaX * deltaX + deltaY * deltaY;
    const bool canSee = CWorld::Inst().GetGeo()->CanSee(this, &user, true);

    char16_t buffer[1024]{};
    FormatUtf16To(
        buffer, std::size(buffer),
        u"item info. pos[%d, %d, %d] active[%d] dist_sq[%d], "
        u"CanSee[%d] AttackRange[%d], AttackSpeed[%d]",
        static_cast<int>(runtime::Win64CvttDoubleToInt64(itemPosition.x)),
        static_cast<int>(runtime::Win64CvttDoubleToInt64(itemPosition.y)),
        static_cast<int>(runtime::Win64CvttDoubleToInt64(itemPosition.z)), m_data->m_bIsActive ? 1 : 0,
        static_cast<int>(runtime::Win64CvttDoubleToInt64(distanceSquared)), canSee ? 1 : 0, m_const->m_nAttackRange,
        static_cast<int>(runtime::Win64CvttDoubleToInt64(m_const->m_dValidate[VALIDATE::PATTACKSPEED]))
    );

    user.GetSocket()->SendSystemMessage(u"SYS", buffer);
}

void CItem::SetDefaultAction(ItemDefaultActionEnum action) {
    L2_GUARD_FUNCTION();
    m_const->m_enumDefaultAction = action;
}

void CItem::SetRecipeId(int recipeId) {
    L2_GUARD_FUNCTION();
    m_const->m_nRecipeId = recipeId;
}

ItemTypeForEnchantAdd CItem::GetITEFromType() {
    L2_GUARD_FUNCTION();
    switch (m_const->m_AttackType) {
        case AT_TWOHANDSWORD:
        case AT_TWOHANDBLUNT:
        case AT_TWOHANDSTAFF:
        case AT_DUAL:
        case AT_DUALFIST:
        case AT_DUALDAGGER:
        case AT_DUALBLUNT:
            return ITE_TWOHAND;
        case AT_BOW:
            return ITE_BOW;
        case AT_CROSSBOW:
        case AT_TWOHANDCROSSBOW:
            return ITE_CROSSBOW;
        case AT_ANCIENTSWORD:
            return (static_cast<std::uint64_t>(m_data->m_Slot) & (std::uint64_t{1} << 14)) != 0 ? ITE_TWOHAND : ITE_ONEHAND;
        case AT_SWORD:
        case AT_BUSTER:
        case AT_BLUNT:
        case AT_STAFF:
        case AT_DAGGER:
        case AT_POLE:
        case AT_ETC:
        case AT_RAPIER:
            return ITE_ONEHAND;
        default:
            return ITE_NORMAL;
    }
}

double CItem::GetEnchantValue(
    const VALIDATE::Enum type, const ItemTypeForEnchantAdd itemType, const int crystal, const int enchanted
) {
    // Current L2Server 0x007d2080. In particular, levels above three use
    // crystal zero for PDEF but keep the supplied crystal for every other
    // branch. This is not a normalization typo.
    L2_GUARD_FUNCTION();

    const bool petCollar = IsPetCollarItem();
    double result = 0.0;
    for (int level = 1; level <= enchanted; ++level) {
        if (level < 4) {
            bool applies = false;
            switch (type) {
                case VALIDATE::PDEFEND:
                case VALIDATE::SHIELDDEFENSE:
                    applies = IsArmor();
                    break;
                case VALIDATE::MDEFEND:
                    applies = IsAccessary() && !IsArtifact();
                    break;
                case VALIDATE::PATTACK:
                case VALIDATE::MATTACK:
                    applies = IsWeapon();
                    break;
                default:
                    break;
            }
            if (applies) {
                result += CurrentEnchantValidateValue(petCollar, type, itemType, crystal);
            }
            continue;
        }

        switch (type) {
            case VALIDATE::PDEFEND:
                if (IsArmor() && (m_const->m_ArmorType == AMT_SIGIL || m_data->m_Slot != 0x100)) {
                    result += 3.0 * CurrentEnchantValidateValue(petCollar, type, itemType, CRT_NONE);
                }
                break;
            case VALIDATE::MDEFEND:
                if (IsAccessary() && !IsArtifact()) {
                    result += 3.0 * CurrentEnchantValidateValue(petCollar, type, itemType, crystal);
                }
                break;
            case VALIDATE::PATTACK:
            case VALIDATE::MATTACK:
                if (IsWeapon()) {
                    result += 2.0 * CurrentEnchantValidateValue(petCollar, type, itemType, crystal);
                }
                break;
            case VALIDATE::SHIELDDEFENSE:
                if (IsArmor() && m_data->m_Slot == 0x100 && m_const->m_ArmorType != AMT_SIGIL) {
                    result += CurrentEnchantValidateValue(petCollar, type, ITE_NORMAL, crystal);
                }
                break;
            default:
                break;
        }
    }
    return result;
}

double CItem::GetEnchantValueRGrade(
    const VALIDATE::Enum type, const ItemTypeForEnchantAdd itemType, const int crystal, const int enchanted
) {
    // Current L2Server 0x007d67ec. The crystal parameter is present in the
    // current ABI but is unused by the R-grade formula.
    L2_GUARD_FUNCTION();
    static_cast<void>(crystal);

    double result = 0.0;
    for (int level = 1; level <= enchanted; ++level) {
        if (IsWeapon()) {
            const int tier = (std::min(level, 15) - 1) / 3 + 1;
            if (type == VALIDATE::PATTACK) {
                double multiplier = 0.0;
                if (IsAttribution()) {
                    switch (itemType) {
                        case ITE_NORMAL:
                        case ITE_ONEHAND:
                            multiplier = 9.0;
                            break;
                        case ITE_TWOHAND:
                        case ITE_CROSSBOW:
                            multiplier = 11.0;
                            break;
                        case ITE_BOW:
                            multiplier = 18.0;
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (itemType) {
                        case ITE_NORMAL:
                        case ITE_ONEHAND:
                            multiplier = 6.0;
                            break;
                        case ITE_TWOHAND:
                        case ITE_CROSSBOW:
                            multiplier = 7.0;
                            break;
                        case ITE_BOW:
                            multiplier = 12.0;
                            break;
                        default:
                            break;
                    }
                }
                result += static_cast<double>(tier) * multiplier;
            } else if (type == VALIDATE::MATTACK) {
                result += static_cast<double>(tier) * (IsAttribution() ? 8.0 : 5.0);
            }
            continue;
        }

        const int tier = (std::min(level, 9) - 1) / 3 + 1;
        if (!IsArmor() || m_data->m_Slot == 0x100) {
            if (IsArmor() && m_data->m_Slot == 0x100) {
                const bool applies =
                    m_const->m_ArmorType == AMT_SIGIL ? type == VALIDATE::PDEFEND : type == VALIDATE::SHIELDDEFENSE;
                if (applies) {
                    result += static_cast<double>(tier) * (IsAttribution() ? 3.0 : 2.0);
                }
            } else if (IsAccessary() && !IsArtifact() && type == VALIDATE::MDEFEND) {
                result += static_cast<double>(tier) * (IsAttribution() ? 3.0 : 2.0);
            }
        } else if (type == VALIDATE::PDEFEND) {
            result += static_cast<double>(tier) * (IsAttribution() ? 3.0 : 2.0);
        }
    }
    return result;
}

double CItem::GetPropertyValue(const VALIDATE::Enum type, const int enchanted) {
    // Current L2Server 0x007d6c48.
    L2_GUARD_FUNCTION();

    double result = 0.0;
    for (int level = 1; level <= enchanted; ++level) {
        const int tier = (std::min(level, 9) - 1) / 3 + 1;
        double multiplier = 0.0;
        switch (type) {
            case VALIDATE::PATTACK:
                multiplier = IsAttribution() ? 3.0 : 2.0;
                break;
            case VALIDATE::MATTACK:
                multiplier = IsAttribution() ? 2.0 : 1.4;
                break;
            case VALIDATE::PHIT:
            case VALIDATE::MHIT:
            case VALIDATE::PAVOID:
            case VALIDATE::MAVOID:
                multiplier = IsAttribution() ? 0.3 : 0.2;
                break;
            case VALIDATE::PCRITICAL:
            case VALIDATE::MCRITICAL:
                multiplier = IsAttribution() ? 0.5 : 0.34;
                break;
            case VALIDATE::SPEED:
                multiplier = IsAttribution() ? 1.0 : 0.6;
                break;
            default:
                break;
        }
        result += static_cast<double>(tier) * multiplier;
    }
    return result;
}

bool CItem::IsItemProperty(const VALIDATE::Enum type) {
    // Current L2Server 0x007dc264.
    L2_GUARD_FUNCTION();
    CItemConstInfo* const info = g_ItemConstInfoDB.GetItemConstInfo(GetClassId());
    return info != nullptr && info->HasItemPropertyList(type);
}

double CItem::EnchantAddValue(const VALIDATE::Enum type) {
    // Current L2Server 0x007dc32c.
    L2_GUARD_FUNCTION();

    const int enchanted = m_data->GetEnchanted();
    const int crystal = static_cast<int>(m_const->GetCrystalType());
    const ItemTypeForEnchantAdd itemType = GetITEFromType();
    if (crystal < 0) {
        return 0.0;
    }

    double result = crystal < CRT_R || crystal == CRT_EVENT ? GetEnchantValue(type, itemType, crystal, enchanted)
                                                            : GetEnchantValueRGrade(type, itemType, crystal, enchanted);
    if (IsItemProperty(type)) {
        result += GetPropertyValue(type, enchanted);
    }
    return result;
}

double CItem::EnchantAddExtraValue(VALIDATE::ExtraEnum type) {
    L2_GUARD_FUNCTION();

    const int crystal = static_cast<int>(m_const->GetCrystalType());
    const int extraType = static_cast<int>(type);
    if (static_cast<unsigned int>(crystal) >= static_cast<unsigned int>(CRT_MAX) ||
        static_cast<unsigned int>(extraType) >= static_cast<unsigned int>(VALIDATE::EXTRA_MAX)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] index may have problem",
        //     L"Item.cpp",
        //     0xb61)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] index may have problem",
        //     L"Item.cpp",
        //     0xb61)
        L2_LOG_ERROR(
            Log, "Cannot calculate enchantment bonus: crystal type %d (limit %d), extra type %d (limit %d); returning 0.",
            crystal, static_cast<int>(CRT_MAX), extraType, static_cast<int>(VALIDATE::EXTRA_MAX)
        );
        return 0.0;
    }

    const int enchanted = std::min(m_data->GetEnchanted(), 30);
    return static_cast<double>(enchanted) *
        g_dEnchantWeaponExBonus[crystal][static_cast<int>(m_const->m_AttackType)][m_const->m_bIsMagicWeapon ? 1 : 0]
                               [extraType];
}

double CItem::GetEnchantedArmorHPBonus() {
    L2_GUARD_FUNCTION();

    const int enchanted = m_data->GetEnchanted();
    if (!IsArmor() || enchanted == 0) {
        return 0.0;
    }

    std::uint32_t enchantIndexBits = static_cast<std::uint32_t>(enchanted) - 1U;
    int enchantIndex = 0;
    static_assert(sizeof(enchantIndex) == sizeof(enchantIndexBits));
    std::memcpy(&enchantIndex, &enchantIndexBits, sizeof(enchantIndex));
    enchantIndex = std::min(enchantIndex, 19);

    const int crystal = std::min(static_cast<int>(m_const->GetCrystalType()), static_cast<int>(CRT_R99));
    double bonus = static_cast<double>(g_nEnchantedArmorBonus[crystal][enchantIndex]);

    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & m_data->m_Slot) == 0) {
        ++slot;
    }
    if (slot == ST_ONEPIECE) {
        bonus = bonus * static_cast<double>(g_nOnepieceFactor) / 200.0;
    }
    return bonus;
}

std::int64_t CItem::GetBaseAttributeCancelCommision() {
    L2_GUARD_FUNCTION();
    // use the existing CrystalType enumerators without
    // changing the commission values or branch behavior.
    // switch (static_cast<int>(m_const->GetCrystalType())) {
    switch (m_const->GetCrystalType()) {
        // case 5:
        case CRT_S:
            return IsWeapon() ? 50000 : 40000;
        // case 6:
        case CRT_S80:
            return IsWeapon() ? 100000 : 80000;
        // case 7:
        case CRT_S84:
            return IsWeapon() ? 200000 : 160000;
        // case 8:
        case CRT_R:
            return IsWeapon() ? 400000 : 320000;
        // case 9:
        case CRT_R95:
            return IsWeapon() ? 800000 : 640000;
        // case 10:
        case CRT_R99:
            return IsWeapon() ? 3200000 : 2560000;
        default:
            return 0;
    }
}

void CItem::SetAttribute(const BaseAttribute& attribute) {
    L2_GUARD_FUNCTION();
    m_itemBaseAttribute = attribute;
}

BaseAttribute CItem::GetTotalAttribute() const {
    L2_GUARD_FUNCTION();
    return m_const->m_BaseAttribute + m_itemBaseAttribute;
}

void CItem::SetVariation(const ItemOptionKey optionKey, const std::uint32_t intensiveItemClassId) {
    // Current L2Server 0x007db9f4.
    L2_GUARD_FUNCTION();
    m_itemOption = g_VariationDB.GetItemOptionOnLoad(optionKey);
    m_itemOption.intensiveItemClassID = static_cast<int>(intensiveItemClassId);
    m_data->m_bIsVariation = optionKey.variationOption1 > 0 && optionKey.variationOption2 > 0;
}

void CItem::ApplyStatBonus(CStats& stats) {
    L2_GUARD_FUNCTION();

    stats += m_itemOption.baseValue;
    if (m_pEnchantOption != nullptr) {
        stats += m_pEnchantOption->GetStatBonus();
    }
    stats.GuaranteeMax(1);
}

int CItem::GetDuration() {
    // Current L2Server.exe 0x007d70c4 tests CItemConst +0xf0
    // (m_nDuration), then reads CSharedItemData +0x88. m_period is the
    // adjacent periodic-item contract and is not consulted here.
    return m_const->m_nDuration > 0 ? m_data->m_nDuration : -1;
}

std::int64_t CItem::GetCrystalizeCount(int divisor) const {
    // Current L2Server 0x007d744c.
    L2_GUARD_FUNCTION();

    const int crystal = static_cast<int>(m_const->GetCrystalType());
    std::int64_t crystalCount = 0;
    if (IsWeapon()) {
        crystalCount = m_const->m_nCrystalCount / divisor;
        const int enchanted = m_data->GetEnchanted();
        for (int level = 0; level < enchanted; ++level) {
            const int addition = ArrayCrystalCountByEnchantWeapon[crystal];
            crystalCount += level < 3 ? addition : addition * 2;
        }
    } else if (IsArmor() || IsAccessary()) {
        crystalCount = m_const->m_nCrystalCount / divisor;
        const int enchanted = m_data->GetEnchanted();
        for (int level = 0; level < enchanted; ++level) {
            const int addition = ArrayCrystalCountByEnchantArmor[crystal];
            const bool onePieceFourthLevel = level == 3 && ((static_cast<std::uint64_t>(m_data->m_Slot) >> 15U) & 1U) != 0;
            crystalCount += level < 3 || onePieceFourthLevel ? addition : addition * 3;
        }
    } else {
        // The current non-equipment branch literally ignores `divisor`.
        crystalCount = m_const->m_nCrystalCount / 2;
    }

    if (crystalCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CrystalCount is zero ItemClassID[%d] "
        //     L"CrystalType[%d], Divisor[%d]",
        //     m_data->m_nClassID,
        //     crystal,
        //     divisor)
        // L2_LOG_ERROR(Log,
        //     L"CrystalCount is zero ItemClassID[%d] "
        //     L"CrystalType[%d], Divisor[%d]",
        //     m_data->m_nClassID,
        //     crystal,
        //     divisor)
        L2_LOG_ERROR(
            Log,
            "Item class %d has crystal count %lld below 1 (crystal type %d, divisor %d); returning the calculated count "
            "unchanged.",
            m_data->m_nClassID, static_cast<long long>(crystalCount), crystal, divisor
        );
        ReportCallStack(LOG_ERROR, false);
    }
    return crystalCount;
}

bool CItem::CanCrystalized() const {
    // Current L2Server 0x007d2bac.
    const int crystal = static_cast<int>(m_const->GetCrystalType());
    return crystal > 0 && crystal < 11 && m_const->m_nCrystalCount > 0;
}

bool CItem::Crystallize(User* const user, CPet* const pet) {
    // Current L2Server 0x007e0400.
    L2_GUARD_FUNCTION();
    const double dice = ::genrand64_real1();

    std::array<char, 0x5000> packet{};
    int packetLength = ::Assemble(
        packet.data(), static_cast<int>(packet.size()), "dQ",
        CrystalClassByCrystalType[static_cast<int>(m_const->GetCrystalType())], GetCrystalizeCount(1)
    );
    int itemCount = packetLength > 0 ? 1 : 0;
    if (packetLength < 1) {
        packetLength = 0;
    }

    CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(GetClassId());
    if (itemInfo != nullptr) {
        std::vector<CrystalItemStruct> crystalItems;
        itemInfo->GetCrystalItemList(crystalItems);
        for (const CrystalItemStruct& crystalItem : crystalItems) {
            if (dice * 100.0 + 0.0 <= crystalItem.Prob) {
                const int assembled = ::Assemble(
                    packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "dQ", crystalItem.ItemID,
                    crystalItem.CrystalCount
                );
                if (assembled > 0) {
                    packetLength += assembled;
                    ++itemCount;
                }
            }
        }
    }

    if (pet == nullptr) {
        db.RequestCrystallizeItem(this, user, itemCount, packetLength, packet.data());
    } else {
        db.RequestCrystallizePetItem(this, user, pet, itemCount, packetLength, packet.data());
    }
    return true;
}

bool CItem::EnchantItem(CItem* targetItem, User* user, SkillFxChangeType type, double value, int enchantLevel) {
    // Current L2Server 0x007de8ec.
    L2_GUARD_FUNCTION();
    if (targetItem == nullptr || user == nullptr) {
        return false;
    }

    const int weaponEnchantRate = user->m_SkillMod.m_nWeaponEnchantRate;
    const int armorEnchantRate = user->m_SkillMod.m_nArmorEnchantRate;
    const int maxEnchantIncreaseRate = user->m_SkillMod.m_nMaxEnchantIncreaseRate;
    const int enchantOption = static_cast<int>(user->m_SkillMod.m_nEnchantOption);
    const SkillFxChangeType enchantType = user->m_SkillMod.m_EnchantType;
    const int currentEnchant = targetItem->m_data->GetEnchanted();
    const std::uint64_t targetSlot = static_cast<std::uint64_t>(targetItem->m_data->m_Slot);
    const bool lucky = user->EventByLuckyStat(false);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);

    int addedEnchantLevel = 0;
    int decreaseEnchantLevel = -1;
    double addedProbability = 0.0;
    double successProbability = 30.0;
    bool success = false;

    if (lucky) {
        success = true;
        if (probabilityItem != nullptr) {
            addedEnchantLevel = user->m_EnchantBonusOptions[2];
        }
    } else {
        if (probabilityItem != nullptr) {
            addedProbability = static_cast<double>(user->m_EnchantBonusOptions[0]);
            decreaseEnchantLevel = user->m_EnchantBonusOptions[1];
            addedEnchantLevel = user->m_EnchantBonusOptions[2];
        }

        CEnchantOptionDB& optionDb = CEnchantOptionDB::Inst();
        if (optionDb.GetMaxEnchantLevel(targetItem->GetClassId()) > 0) {
            successProbability =
                static_cast<double>(optionDb.GetEnchantSuccessRate(targetItem->GetClassId(), currentEnchant));
            success = ::genrand64_real1() * 100.0 <= successProbability;
        } else if (currentEnchant < 3) {
            successProbability = 100.0;
            success = true;
        } else if (!targetItem->IsWeapon()) {
            if (currentEnchant < 20) {
                const double failRate = ((targetSlot >> 15U) & 1U) == 0 ? ArmorEnchantFailTable[currentEnchant]
                                                                        : OnePieceEnchantFailTable[currentEnchant];
                successProbability = 100.0 - failRate * 100.0;
            } else {
                successProbability = 0.0;
            }
            if (currentEnchant > 14) {
                successProbability *= 0.5;
            }
            successProbability += addedProbability;
            if (type == SFCT_PER) {
                successProbability *= value / 100.0 + 1.0;
            } else if (type == SFCT_DIFF) {
                successProbability += value * 100.0;
            }
            if (enchantOption == 6 && (enchantType == SFCT_PER || enchantType == SFCT_VEL)) {
                successProbability += std::min(
                    successProbability * (static_cast<double>(armorEnchantRate) / 100.0),
                    static_cast<double>(maxEnchantIncreaseRate)
                );
            }
            success = currentEnchant < 20 && ::genrand64_real1() * 100.0 <= successProbability;
        } else {
            const CrystalType crystal = targetItem->m_const->GetCrystalType();
            const bool magicWeapon = crystal >= CRT_C && targetItem->m_const->m_bIsMagicWeapon;
            const bool rGrade = crystal >= CRT_R && crystal <= CRT_R99;

            if (!magicWeapon) {
                if (rGrade) {
                    const int rateDivisor = (std::min(currentEnchant, 15) + 3) / 3;
                    successProbability = 100.0 / static_cast<double>(rateDivisor) + addedProbability;
                } else {
                    successProbability = currentEnchant > 14 ? 35.0 : 70.0;
                    successProbability += addedProbability;
                    if (enchantOption == 6 && (enchantType == SFCT_PER || enchantType == SFCT_DIFF)) {
                        successProbability += std::min(
                            successProbability * (static_cast<double>(weaponEnchantRate) / 100.0),
                            static_cast<double>(maxEnchantIncreaseRate)
                        );
                    }
                }
                if (type == SFCT_PER) {
                    successProbability *= value / 100.0 + 1.0;
                } else if (type == SFCT_DIFF) {
                    successProbability += value * 100.0;
                }
            } else {
                if (rGrade) {
                    const int rateDivisor = (std::min(currentEnchant, 15) + 3) / 3;
                    successProbability = (100.0 / static_cast<double>(rateDivisor)) / 1.4285;
                } else {
                    successProbability = currentEnchant > 14 ? 24.5 : 49.0;
                }
                successProbability += addedProbability;
                if (type == SFCT_PER) {
                    successProbability *= value / 100.0 + 1.0;
                } else if (type == SFCT_DIFF) {
                    successProbability += value * 100.0;
                }
                if (enchantOption == 6 && (enchantType == SFCT_PER || enchantType == SFCT_DIFF)) {
                    successProbability += std::min(
                        successProbability * (static_cast<double>(weaponEnchantRate) / 100.0),
                        static_cast<double>(maxEnchantIncreaseRate)
                    );
                }
            }
            success = ::genrand64_real1() * 100.0 <= successProbability;
        }
    }

    const int packetProbability = runtime::Win64CvttDoubleToInt32(successProbability);
    if (g_L2ServerType == L2ServerType::DEV && user->IsBuilder()) {
        char16_t message[160]{};
        FormatUtf16To(message, 100, u"EnchantScrollItem [Prob:%d])", packetProbability);
        user->GetSocket()->SendSystemMessage(u"SYS", message);
    }

    if (success) {
        int newEnchant = currentEnchant + addedEnchantLevel + enchantLevel;
        if (targetItem->IsArmor() && newEnchant > 20) {
            newEnchant = 20;
        }
        db.RequestEnchantItem(this, probabilityItem, targetItem, newEnchant, user, packetProbability, lucky);
        return true;
    }

    if (decreaseEnchantLevel == 0) {
        db.RequestEnchantItem(this, probabilityItem, targetItem, 0, user, packetProbability, lucky);
        return true;
    }
    if (decreaseEnchantLevel > 0) {
        db.RequestEnchantItem(
            this, probabilityItem, targetItem, std::max(currentEnchant - decreaseEnchantLevel, 0), user, packetProbability,
            lucky
        );
        return true;
    }

    const EtcItemType scrollType = static_cast<EtcItemType>(m_const->m_EtcitemType);
    if (scrollType == EIT_BLESS_ENCHT_WP || scrollType == EIT_BLESS_ENCHT_AM || scrollType == EIT_BLESS_ENCHT_AG) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::BLESSED_ENCHANT_FAILED_ENCHANT_VALUE_RESET);
        db.RequestEnchantItem(this, probabilityItem, targetItem, 0, user, packetProbability, lucky);
        return true;
    }
    if (scrollType == EIT_ANCIENT_CRYSTAL_ENCHANT_WP || scrollType == EIT_ANCIENT_CRYSTAL_ENCHANT_AM ||
        scrollType == EIT_ANCIENT_CRYSTAL_ENCHANT_AG) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ENCHANT_FAILED_VALUE_PRESERVED);
        db.RequestEnchantItem(this, probabilityItem, targetItem, currentEnchant, user, packetProbability, lucky);
        return true;
    }

    const CrystalType crystal = targetItem->m_const->GetCrystalType();
    int crystalCount = 0;
    if (crystal >= CRT_D && crystal <= CRT_R99 && targetItem->m_const->m_nCrystalCount > 0) {
        crystalCount = static_cast<int>(targetItem->GetCrystalizeCount(2));
    }
    db.RequestEnchantItemFail(
        this, probabilityItem, targetItem, user, CrystalClassByCrystalType[static_cast<int>(crystal)], crystalCount,
        packetProbability
    );
    return true;
}

bool CItem::EnchantAttributeItemArmor(
    CItem* targetItem, User* user, AttributeTypeEnum attributeType, int grade, int additionalProbability, int additionalValue
) {
    // Current L2Server 0x007e0ccc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (targetItem == nullptr || !targetItem->IsArmor()) {
        ResetAttributeEnchantState(*user);
        return false;
    }

    const CrystalType crystal = targetItem->m_const->GetCrystalType();
    const std::int64_t attemptCount = std::min(user->nAttributeCount, m_data->m_nCount);
    std::int64_t failedCount = 0;
    std::int64_t successCount = 0;
    BaseAttribute newAttribute(targetItem->m_itemBaseAttribute);

    for (std::int16_t attempt = 0; attempt < attemptCount; ++attempt) {
        const int roll = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 101.0);
        int probability =
            additionalProbability + kAttributeEnchantProbabilityArmor[static_cast<int>(crystal) - CRT_S][grade / 3 - 1];
        if (m_const->m_GeneralItemType == 0x1a) {
            probability /= 2;
        }

        if (g_L2ServerType == L2ServerType::DEV && user->IsBuilder()) {
            char16_t message[100]{};
            FormatUtf16To(message, std::size(message), u"EnchantAttributeArmor [Prob:%d])", probability);
            user->GetSocket()->SendSystemMessage(u"SYS", message);
        }

        if (probability < roll) {
            ++failedCount;
            continue;
        }

        const short increment = static_cast<short>(additionalValue < 7 ? 6 : additionalValue);
        short& value = newAttribute.m_nDefendValue[attributeType];
        value = static_cast<short>(value + increment);
        const short maximum = kAttributeArmorMaximum[grade - 1][1];
        if (value > maximum) {
            value = maximum;
        }
        ++successCount;
        if (value == maximum) {
            break;
        }
    }

    if (successCount == 0) {
        ResetAttributeEnchantState(*user);
        user->DeleteItemInInventory(m_data->m_nClassID, failedCount, nullptr, false, false);
        user->SendSystemMessage(SystemMessageId::ATTRIBUTE_ENCHANT_FAILED);
        user->GetSocket()->Send("chdhh", 0xfe, 0x62, 1, 0, static_cast<int>(failedCount));
        WriteAttributeEnchantFailureLog(*user, *targetItem, *this, failedCount);
        return true;
    }

    ItemManip request(0, m_data->m_nClassID, -(failedCount + successCount), 0, 0, 0, true);
    EtcManiUpdateUserItemBaseAttribute update(
        user->m_nId, user->GetDBID(), targetItem->GetDBID(), m_data->m_nClassID, attributeType, newAttribute
    );
    CPacketHelperEx<20480> packet;
    update.Assemble(packet);

    L2_GUARD(user->m_ManipulateQueueLock);
    User::etcMsg message = user->SaveEtcMsg(
        false, false, static_cast<int>(successCount), static_cast<int>(failedCount), 0, std::u16string(), std::u16string(), 0
    );
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_ATTRIBUTE_ITEM, 0, 1, &request, nullptr, 1, packet, false, ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        user->nAttributeCount = successCount;
        user->nAttributeFailCount = failedCount;
        if (!user->_HandleItemManipulation(queue)) {
            ResetAttributeEnchantState(*user);
            return false;
        }
        return true;
    }
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        ResetAttributeEnchantState(*user);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"Item.cpp",
        //     0x79a)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"Item.cpp",
        //     0x79a)
        // Noise: duplicate armor attribute request is already rejected with PREVIOUS_REQUEST_NOT_COMPLETED. No server error.
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"Item.cpp",
        //     0x79f)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"Item.cpp",
        //     0x79f)
        // Noise: normal armor attribute request queued successfully. Enqueueing is not an error or proof of completion.
        return true;
    }

    user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    ResetAttributeEnchantState(*user);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"Item.cpp",
    //     0x7aa)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"Item.cpp",
    //     0x7aa)
    L2_LOG_ERROR(
        Log, "Cannot queue armor attribute enchantment for character %u: item queue returned %d; request rejected.",
        static_cast<unsigned>(user->GetDBID()), static_cast<int>(result)
    );
    return false;
}

bool CItem::EnchantAttributeItemWeapon(
    CItem* targetItem, User* user, AttributeTypeEnum attributeType, int grade, int additionalProbability, int additionalValue
) {
    // Current L2Server 0x007e18cc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (targetItem == nullptr || !targetItem->IsWeapon()) {
        ResetAttributeEnchantState(*user);
        return false;
    }

    const CrystalType crystal = targetItem->m_const->GetCrystalType();
    const std::int64_t attemptCount = std::min(user->nAttributeCount, m_data->m_nCount);
    std::int64_t failedCount = 0;
    std::int64_t successCount = 0;
    BaseAttribute newAttribute(targetItem->m_itemBaseAttribute);

    for (std::int16_t attempt = 0; attempt < attemptCount; ++attempt) {
        const int roll = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 101.0);
        int probability =
            additionalProbability + kAttributeEnchantProbabilityWeapon[static_cast<int>(crystal) - CRT_S][grade / 3 - 1];
        if (m_const->m_GeneralItemType == 0x1a) {
            probability /= 2;
        }

        if (g_L2ServerType == L2ServerType::DEV && user->IsBuilder()) {
            char16_t message[100]{};
            FormatUtf16To(message, std::size(message), u"EnchantAttributeWeapon [Prob:%d])", probability);
            user->GetSocket()->SendSystemMessage(u"SYS", message);
        }

        if (probability < roll) {
            ++failedCount;
            continue;
        }

        const int currentType = newAttribute.m_nAttackType;
        if (currentType != ATTRT_NONE && currentType != attributeType) {
            user->SendSystemMessage(SystemMessageId::ATTRIBUTE_ENCHANT_ITEM_USE_CANCELLED);
            ResetAttributeEnchantState(*user);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] target item have another "
            //     L"attribute[%d], but try [%d]",
            //     L"Item.cpp",
            //     0x845,
            //     currentType,
            //     static_cast<int>(attributeType))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] target item have another "
            //     L"attribute[%d], but try [%d]",
            //     L"Item.cpp",
            //     0x845,
            //     currentType,
            //     static_cast<int>(attributeType))
            L2_LOG_WARN(
                Log, "Weapon attribute enchantment cancelled for character %u: item %u has attribute %d, requested %d.",
                static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(targetItem->GetDBID()), currentType,
                static_cast<int>(attributeType)
            );
            return false;
        }

        short increment = 0;
        if (newAttribute.m_nAttackValue == 0) {
            increment = static_cast<short>(additionalValue < 21 ? 20 : additionalValue);
        } else {
            increment = static_cast<short>(additionalValue < 6 ? 5 : additionalValue);
        }
        newAttribute.m_nAttackValue = static_cast<short>(newAttribute.m_nAttackValue + increment);
        const short maximum = kAttributeWeaponMaximum[grade - 1][1];
        if (newAttribute.m_nAttackValue > maximum) {
            newAttribute.m_nAttackValue = maximum;
        }
        ++successCount;
        newAttribute.m_nAttackType = static_cast<short>(attributeType);
        if (newAttribute.m_nAttackValue == maximum) {
            break;
        }
    }

    if (successCount == 0) {
        ResetAttributeEnchantState(*user);
        user->DeleteItemInInventory(m_data->m_nClassID, failedCount, nullptr, false, false);
        user->SendSystemMessage(SystemMessageId::ATTRIBUTE_ENCHANT_FAILED);
        user->GetSocket()->Send("chdhh", 0xfe, 0x62, 1, 0, static_cast<int>(failedCount));
        WriteAttributeEnchantFailureLog(*user, *targetItem, *this, failedCount);
        user->GetSocket()->Send("chdhh", 0xfe, 0x62, 1, 0, static_cast<int>(failedCount));
        return true;
    }

    ItemManip request(0, m_data->m_nClassID, -(failedCount + successCount), 0, 0, 0, true);
    EtcManiUpdateUserItemBaseAttribute update(
        user->m_nId, user->GetDBID(), targetItem->GetDBID(), m_data->m_nClassID, attributeType, newAttribute
    );
    CPacketHelperEx<20480> packet;
    update.Assemble(packet);

    L2_GUARD(user->m_ManipulateQueueLock);
    User::etcMsg message = user->SaveEtcMsg(
        false, false, static_cast<int>(successCount), static_cast<int>(failedCount), 0, std::u16string(), std::u16string(), 0
    );
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_ATTRIBUTE_ITEM, 0, 1, &request, nullptr, 1, packet, false, ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        user->nAttributeCount = successCount;
        user->nAttributeFailCount = failedCount;
        if (!user->_HandleItemManipulation(queue)) {
            ResetAttributeEnchantState(*user);
            return false;
        }
        return true;
    }
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        ResetAttributeEnchantState(*user);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"Item.cpp",
        //     0x896)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"Item.cpp",
        //     0x896)
        // Noise: duplicate weapon attribute request is already rejected with PREVIOUS_REQUEST_NOT_COMPLETED. No server
        // error.
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"Item.cpp",
        //     0x89b)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"Item.cpp",
        //     0x89b)
        // Noise: normal weapon attribute request queued successfully. Enqueueing is not an error or proof of completion.
        return true;
    }

    user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    ResetAttributeEnchantState(*user);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"Item.cpp",
    //     0x8a7)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"Item.cpp",
    //     0x8a7)
    L2_LOG_ERROR(
        Log, "Cannot queue weapon attribute enchantment for character %u: item queue returned %d; request rejected.",
        static_cast<unsigned>(user->GetDBID()), static_cast<int>(result)
    );
    return false;
}

bool CItem::CompareOptionData(
    const std::map<int, EnsoulOptionData*> first[3], const std::map<int, EnsoulOptionData*> second[3]
) {
    // Current L2Server 0x007db19c compares only the two usable ensoul slots,
    // preserving both slot indices and option IDs.
    L2_GUARD_FUNCTION();
    for (int slot = 1; slot <= 2; ++slot) {
        if (first[slot].size() != second[slot].size()) {
            return false;
        }
        for (const auto& entry : first[slot]) {
            const auto found = second[slot].find(entry.first);
            if (found == second[slot].end()) {
                return false;
            }
            const EnsoulOptionData* const left = entry.second;
            const EnsoulOptionData* const right = found->second;
            if (left == nullptr || right == nullptr || left->m_nId != right->m_nId) {
                return false;
            }
        }
    }
    return true;
}

bool CItem::SimulateWithInventory(CInventory& inventory, std::pair<int, std::int64_t>& result, const std::int64_t count) {
    L2_GUARD_FUNCTION();

    const std::int64_t current = inventory.GetItemAmount(GetClassId(), 0, true);
    if (current + count < 0 || count == 0) {
        return false;
    }

    if (CSharedItemData::IsStackable(m_data->m_ItemConsumeType)) {
        if (current + count == 0) {
            result.first = -1;
        } else {
            result.first = current == 0 ? 1 : 0;
        }
    } else {
        result.first = static_cast<int>(count);
    }

    const int weight = m_data->m_ItemConsumeType == ICT_ASSET ? 0 : m_const->m_nWeight;
    result.second = static_cast<std::int64_t>(weight) * count;
    return true;
}

void CItem::SetAgathionEnergy(double energy) {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    const double maximum = m_const->m_dAgathionMaxEnergy;
    const double lowerCandidate = maximum <= energy ? maximum : energy;
    double result = 0.0;
    if (0.0 < lowerCandidate || lowerCandidate == 0.0) {
        result = energy < maximum ? energy : maximum;
    }
    m_data->m_dAgathionEnergy = result;
}

double CItem::GetAgathionEnergy() { return m_data->m_dAgathionEnergy; }

bool CItem::AddAmount(std::int64_t amount) {
    if (m_data == nullptr) {
        return false;
    }

    const std::uint64_t updatedBits = static_cast<std::uint64_t>(m_data->m_nCount) + static_cast<std::uint64_t>(amount);
    if (updatedBits > 999999999999ULL) {
        return false;
    }

    std::int64_t updated = 0;
    std::memcpy(&updated, &updatedBits, sizeof(updated));
    m_data->m_nCount = updated;
    return true;
}

bool CItem::IsAddableAmount(std::int64_t amount) {
    if (m_data == nullptr) {
        return false;
    }
    const std::uint64_t updated = static_cast<std::uint64_t>(m_data->m_nCount) + static_cast<std::uint64_t>(amount);
    return updated < 1000000000000ULL;
}

bool CItem::SetAmount(std::int64_t amount) {
    if (m_data != nullptr && static_cast<std::uint64_t>(amount) < 1000000000000ULL) {
        m_data->m_nCount = amount;
        return true;
    }
    return false;
}

void CItem::SetDuration(const int duration) {
    // Current L2Server 0x007d24f4.
    runtime::raw_atomic::ExchangeI32(&m_data->m_nDuration, duration);
}

void CItem::DecreaseDurationOneUnit() {
    // Current L2Server 0x007d2500.
    runtime::raw_atomic::DecrementI32(&m_data->m_nDuration);
}

void CItem::SetDurationCommitTick(const int tick) {
    // Current L2Server 0x007d2524. The target rounds every non-multiple
    // upward to the next fifteen-second boundary before the atomic store.
    const int quotient = tick / 15;
    const int rounded = (quotient + (tick != quotient * 15 ? 1 : 0)) * 15;
    runtime::raw_atomic::ExchangeI32(&m_durationCommitTick, rounded);
}

bool CItem::IsTickToExpire(const int tick) const {
    // Current L2Server.exe 0x007d2558.
    if (tick == m_durationCommitTick) {
        return false;
    }
    const std::uint32_t deltaBits = static_cast<std::uint32_t>(tick) - static_cast<std::uint32_t>(m_durationCommitTick);
    std::int32_t delta = 0;
    std::memcpy(&delta, &deltaBits, sizeof(delta));
    return delta % 90 == 0;
}

void CItem::SetInitialDuration() { runtime::raw_atomic::ExchangeI32(&m_data->m_nDuration, m_const->m_nDuration); }

void CItem::SetEnchanted(const int enchanted) {
    // Current L2Server 0x007df204.
    L2_GUARD_FUNCTION();
    m_data->SetEnchanted(enchanted);
    CEnchantOptionDB::Inst().SetEnchantOption(*this);
}

bool CItem::IsDuration() { return m_const->m_nDuration > 0; }

void CItem::SendItemSpawn(CUserSocket* socket) {
    // Current L2Server.exe 0x007db460.
    L2_GUARD_FUNCTION();

    const std::uint32_t itemId = Id();
    const std::int32_t classId = m_nClassId;
    const FVector position = GetPos();
    const std::uint16_t heading = static_cast<std::uint16_t>(GetDir().Yaw);

    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"don't call without socket! [%S][%d]",
        //     "Item.cpp",
        //     0x49e)
        // L2_LOG_ERROR(Log,
        //     L"don't call without socket! [%S][%d]",
        //     "Item.cpp",
        //     0x49e)
        L2_LOG_ERROR(
            Log, "Cannot send spawn packet for item object %u (class %d): socket is missing.", static_cast<unsigned>(itemId),
            classId
        );
        return;
    }

    const bool stackable = CSharedItemData::IsStackable(m_data->m_ItemConsumeType);
    const bool hasVariation =
        m_itemOption.itemOptionKey.variationOption1 > 0 && m_itemOption.itemOptionKey.variationOption2 > 0;
    const std::int32_t direction = IsEtcItem() && m_const->m_EtcitemType == 8 ? static_cast<std::int32_t>(heading) : 0;

    socket->Send(
        "cddddddQdccc", 5, itemId, classId, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), stackable,
        m_data->m_nCount, direction, static_cast<char>(m_data->GetEnchanted()), hasVariation,
        static_cast<char>(GetEnsoulCount())
    );
}

void CItem::SendItemDropped(CUserSocket* socket, CCreature* dropper) {
    L2_GUARD_FUNCTION();
    if (dropper == nullptr) {
        return;
    }

    const std::uint32_t dropperId = dropper->Id();
    const std::uint32_t itemId = Id();
    const std::int32_t classId = m_nClassId;
    const std::int32_t x = runtime::Win64CvttDoubleToInt32(GetPos().x);
    const std::int32_t y = runtime::Win64CvttDoubleToInt32(GetPos().y);
    const std::int32_t z = runtime::Win64CvttDoubleToInt32(GetPos().z);
    const std::uint16_t heading = static_cast<std::uint16_t>(GetDir().Yaw);

    if (socket == nullptr) {
        return;
    }

    const bool stackable = CSharedItemData::IsStackable(m_data->m_ItemConsumeType);
    const char enchanted = static_cast<char>(m_data->GetEnchanted());
    const bool hasVariation =
        m_itemOption.itemOptionKey.variationOption1 > 0 && m_itemOption.itemOptionKey.variationOption2 > 0;
    const int ensoulCount = GetEnsoulCount();

    if (IsEtcItem() && m_const->m_EtcitemType == 8) {
        socket->Send(
            "cddddddcQcdccc", 0x16, dropperId, itemId, classId, x, y, z, stackable, m_data->m_nCount, 1, heading, enchanted,
            hasVariation, ensoulCount
        );
        return;
    }

    socket->Send(
        "cddddddcQcccc", 0x16, dropperId, itemId, classId, x, y, z, stackable, m_data->m_nCount, 0, enchanted, hasVariation,
        ensoulCount
    );
}

bool CItem::AddDropPeriodTimer() {
    L2_GUARD_FUNCTION();
    return m_const->m_nDropPeriod < 1 || AddTimer(m_const->m_nDropPeriod, 0);
}

const unsigned char* CItem::DisassembleVariation(const unsigned char* packet, int& firstOption, int& secondOption) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dd", &firstOption, &secondOption);
}

int CItem::AssembleAttribute(const BaseAttribute& attribute, IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hhhhhhhh", static_cast<int>(attribute.m_nAttackType), static_cast<int>(attribute.m_nAttackValue),
        static_cast<int>(attribute.m_nDefendValue[0]), static_cast<int>(attribute.m_nDefendValue[1]),
        static_cast<int>(attribute.m_nDefendValue[2]), static_cast<int>(attribute.m_nDefendValue[3]),
        static_cast<int>(attribute.m_nDefendValue[4]), static_cast<int>(attribute.m_nDefendValue[5])
    );
}

const unsigned char* CItem::DisassembleAttribute(const unsigned char* packet, BaseAttribute& attribute) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "hhhhhhhh", &attribute.m_nAttackType, &attribute.m_nAttackValue, &attribute.m_nDefendValue[0],
        &attribute.m_nDefendValue[1], &attribute.m_nDefendValue[2], &attribute.m_nDefendValue[3],
        &attribute.m_nDefendValue[4], &attribute.m_nDefendValue[5]
    );
}

const unsigned char* CItem::DisassembleShapeShift(const unsigned char* packet, int& classId) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "d", &classId);
}

int CItem::AssembleEnsoul(IPacketHelper& packet) {
    // Current L2Server.exe 0x007daf4c. Only lanes NORMAL and BM are compiled.
    L2_GUARD_FUNCTION();
    int assembled = 0;
    for (int slotType = EnsoulItem::EIST_NORMAL; slotType < EnsoulItem::EIST_MAX; ++slotType) {
        const auto& options = m_mEnsoulInfo[slotType];
        assembled += packet.Assemble("c", static_cast<int>(options.size()));
        for (int slotIndex = 1; static_cast<std::size_t>(slotIndex) <= options.size(); ++slotIndex) {
            const auto found = options.find(slotIndex);
            const int optionId = found == options.end() ? 0 : found->second->m_nId;
            assembled += packet.Assemble("d", optionId);
        }
    }
    return assembled;
}

const unsigned char* CItem::DisassembleEnsoul(const unsigned char* packet, std::map<int, EnsoulOptionData*> options[3]) {
    // Current L2Server 0x007dcc08. Only the NORMAL and BM lanes are
    // serialized. Lane zero is skipped.
    L2_GUARD_FUNCTION();
    for (int slotType = EnsoulItem::EIST_NORMAL; slotType < EnsoulItem::EIST_MAX; ++slotType) {
        char optionCount = 0;
        packet = ::Disassemble(packet, "c", &optionCount);
        for (int slotIndex = 1; slotIndex <= optionCount; ++slotIndex) {
            int optionId = 0;
            packet = ::Disassemble(packet, "d", &optionId);
            const auto found = g_mEnsoulOptionData.find(optionId);
            if (found != g_mEnsoulOptionData.end()) {
                options[slotType].emplace(slotIndex, found->second);
            }
        }
    }
    return packet;
}

void CItem::SetEnsoulOptionData(std::map<int, EnsoulOptionData*> options[3], const bool clear) {
    // Current L2Server.exe 0x007dcd84.
    L2_GUARD_FUNCTION();
    for (int slotType = EnsoulItem::EIST_NORMAL; slotType < EnsoulItem::EIST_MAX; ++slotType) {
        const auto& source = options[slotType];
        if (source.empty()) {
            continue;
        }

        const std::uint64_t capacity =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(m_const->m_ensoulSlotCount[slotType]));
        if (capacity < source.size()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Too Many Ensoul Options ItemID[%d] "
            //     L"EnsoulOptionSlot[%d] OptionNum[%d]",
            //     "Item.cpp",
            //     0x1be3,
            //     GetDBID(),
            //     slotType,
            //     static_cast<int>(source.size()))
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Too Many Ensoul Options ItemID[%d] "
            //     L"EnsoulOptionSlot[%d] OptionNum[%d]",
            //     "Item.cpp",
            //     0x1be3,
            //     GetDBID(),
            //     slotType,
            //     static_cast<int>(source.size()))
            L2_LOG_ERROR(
                Log,
                "Item %u has %zu ensoul options for slot type %d, exceeding capacity %llu; stopping updates after earlier "
                "slots.",
                static_cast<unsigned>(GetDBID()), source.size(), slotType, static_cast<unsigned long long>(capacity)
            );
            break;
        }

        auto& destination = m_mEnsoulInfo[slotType];
        if (clear) {
            destination.clear();
        }
        for (const auto& option : source) {
            destination.erase(option.first);
            destination.emplace(option.first, option.second);
        }
    }
}

bool CItem::IsCombinableItem(User* user) {
    // Current L2Server 0x007dab24, original owner Item.cpp.
    L2_GUARD_FUNCTION();
    if (user->IsEquipedItem(m_data->index)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_EQUIPPED_ITEM);
        return false;
    }
    if (IsAsset()) {
        user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }

    if (IsArmor() || IsAccessary()) {
        int slot = 0;
        while (slot < ST_MAX && (ST2SBT[slot] & m_data->m_Slot) == 0) {
            ++slot;
        }
        const bool combinableSlot = slot <= ST_LFINGER || slot == ST_BACK || slot == ST_HAIR ||
            (slot >= ST_HAIR2 && slot <= ST_LBRACELET) || slot == ST_DECO1 || (slot >= ST_WAIST && slot <= ST_JEWEL1);
        if (!combinableSlot) {
            user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
            return false;
        }
        if (m_itemBaseAttribute.IsEnchanted(ATTRT_NONE) || m_data->GetEnchanted() > 0 ||
            (m_itemOption.itemOptionKey.variationOption1 > 0 && m_itemOption.itemOptionKey.variationOption2 > 0) ||
            IsEnsoulItem()) {
            user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
            return false;
        }
    }
    if (!IsWeapon()) {
        return true;
    }

    if (m_itemBaseAttribute.IsEnchanted(ATTRT_NONE) || m_data->GetEnchanted() > 0 ||
        (m_itemOption.itemOptionKey.variationOption1 > 0 && m_itemOption.itemOptionKey.variationOption2 > 0) ||
        IsEnsoulItem()) {
        user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }
    return true;
}

bool CItem::IsPeriodicShapeShiftedItem(User* user) const {
    L2_GUARD_FUNCTION();
    return user != nullptr && m_data->GetShapeShiftedClassId() > 0 &&
        user->GetPeriodLeftForShapeShiftedItem(static_cast<int>(GetDBID())) != -9999;
}

void CItem::MakeShapeShiftAndVariationLogInfo(char16_t* buffer) {
    // Current L2Server.exe 0x007d7fdc, original owner Item.cpp.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> shapeShift{};
    const int classId = m_data->GetShapeShiftedClassId();
    if (classId == 0) {
        FormatUtf16To(shapeShift.data(), shapeShift.size(), u"not_shape_shifted");
    } else {
        FormatUtf16To(shapeShift.data(), shapeShift.size(), u"shape_shifted_(%d)", classId);
    }

    std::array<char16_t, 256> variation{};
    FormatUtf16To(
        variation.data(), variation.size(), u"%d:%d:%d", m_itemOption.intensiveItemClassID,
        m_itemOption.itemOptionKey.variationOption1, m_itemOption.itemOptionKey.variationOption2
    );
    FormatUtf16To(buffer, 256, u"%ls/%ls", shapeShift.data(), variation.data());
}

void CItem::MakeShapeShiftLogInfo(char16_t* buffer) {
    L2_GUARD_FUNCTION();
    const int classId = m_data->GetShapeShiftedClassId();
    if (classId == 0) {
        FormatUtf16To(buffer, 32, u"not_shape_shifted");
    } else {
        FormatUtf16To(buffer, 32, u"shape_shifted_(%d)", classId);
    }
}

void CItem::MakeEnsoulLogInfo(char16_t* buffer) {
    L2_GUARD_FUNCTION();
    int length = 0;
    for (int slotType = EnsoulItem::EIST_NORMAL; slotType < EnsoulItem::EIST_MAX; ++slotType) {
        const auto& options = m_mEnsoulInfo[slotType];
        for (int slotIndex = 1; static_cast<std::size_t>(slotIndex) <= options.size(); ++slotIndex) {
            const auto found = options.find(slotIndex);
            if (found == options.end()) {
                continue;
            }
            const int optionId = found->second != nullptr ? found->second->m_nId : 0;
            const int written = FormatUtf16To(
                buffer + length, static_cast<std::size_t>(std::max(1, 260 - length)), u"(%d:%d:%d)", slotType, slotIndex,
                optionId
            );
            if (written > 0) {
                length += written;
            }
        }
    }
}

void CItem::LogRelateItem(User& user, const std::int64_t amount) {
    L2_GUARD_FUNCTION();
    if (m_nTraceCharId == 0) {
        return;
    }

    const FVector position = GetPos();
    std::array<char16_t, 256> attribute{};
    std::array<char16_t, 256> ensoul{};
    std::array<char16_t, 260> shape{};
    m_itemBaseAttribute.GetLogString(attribute.data());
    MakeShapeShiftLogInfo(shape.data());
    MakeEnsoulLogInfo(ensoul.data());

    CSPointer<User> traceUser =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(m_nTraceCharId)));
    std::int64_t traceAccountId = 0;
    const char16_t* traceAccountName = nullptr;
    const char16_t* traceName = nullptr;
    std::u16string traceNameStorage;
    if (traceUser) {
        traceAccountId = traceUser->GetAccountID();
        traceAccountName = traceUser->m_sAccountName.data();
        traceName = (traceNameStorage = traceUser->GetName()).c_str();
    }

    std::u16string userNameStorage;
    const char16_t* const userName = (userNameStorage = user.GetName()).c_str();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3ac, static_cast<std::int64_t>(m_nTraceCharId), traceAccountId, static_cast<std::int64_t>(user.GetDBID()),
        static_cast<std::int64_t>(user.GetAccountID()), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), shape.data(),
        ensoul.data(), attribute.data(), 0, 0, static_cast<std::int64_t>(user.m_data->m_nClass),
        static_cast<std::int64_t>(user.m_data->m_nLevel), 0, 0, 0, static_cast<std::int64_t>(m_data->m_nClassID), amount,
        static_cast<std::int64_t>(m_data->GetEnchanted()), traceName, traceAccountName, userName, user.m_sAccountName.data(),
        static_cast<std::int64_t>(GetDBID())
    );
}

CrystalType CItem::GetCrysalType(const wchar_t* name) {
    L2_GUARD_FUNCTION();

    if (std::wcscmp(name, L"a") == 0) {
        return CRT_A;
    }
    if (std::wcscmp(name, L"b") == 0) {
        return CRT_B;
    }
    if (std::wcscmp(name, L"c") == 0) {
        return CRT_C;
    }
    if (std::wcscmp(name, L"d") == 0) {
        return CRT_D;
    }
    if (std::wcscmp(name, L"s") == 0) {
        return CRT_S;
    }
    if (std::wcscmp(name, L"s80") == 0) {
        return CRT_S80;
    }
    if (std::wcscmp(name, L"s84") == 0) {
        return CRT_S84;
    }
    if (std::wcscmp(name, L"none") == 0) {
        return CRT_NONE;
    }
    if (std::wcscmp(name, L"crystal_free") == 0) {
        return CRT_MAX;
    }
    if (std::wcscmp(name, L"event") == 0) {
        return CRT_EVENT;
    }
    if (std::wcscmp(name, L"r") == 0) {
        return CRT_R;
    }
    if (std::wcscmp(name, L"r95") == 0) {
        return CRT_R95;
    }
    if (std::wcscmp(name, L"r99") == 0) {
        return CRT_R99;
    }
    return CRT_INVALID;
}

VALIDATE::Enum CItem::GetValidateType(const wchar_t* name) {
    L2_GUARD_FUNCTION();

    struct Entry {
        const wchar_t* name;
        VALIDATE::Enum type;
    };
    static constexpr Entry entries[] = {
        {L"pdefense", VALIDATE::PDEFEND},
        {L"mdefense", VALIDATE::MDEFEND},
        {L"pattack", VALIDATE::PATTACK},
        {L"mattack", VALIDATE::MATTACK},
        {L"pattackspeed", VALIDATE::PATTACKSPEED},
        {L"mattackspeed", VALIDATE::MATTACKSPEED},
        {L"phit", VALIDATE::PHIT},
        {L"mhit", VALIDATE::MHIT},
        {L"pcritical", VALIDATE::PCRITICAL},
        {L"mcritical", VALIDATE::MCRITICAL},
        {L"speed", VALIDATE::SPEED},
        {L"shielddefense", VALIDATE::SHIELDDEFENSE},
        {L"shielddefenserate", VALIDATE::SHIELDDEFENSERATE},
        {L"pavoid", VALIDATE::PAVOID},
        {L"mavoid", VALIDATE::MAVOID},
    };
    for (const Entry& entry : entries) {
        if (std::wcscmp(name, entry.name) == 0) {
            return entry.type;
        }
    }
    // Log.Add(LOG_ERROR, L"Undefine Validate Type")
    // L2_LOG_ERROR(Log, L"Undefine Validate Type")
    L2_LOG_ERROR(
        Log, "Unknown item validation type '%s'; returning VALIDATE::MAX.", name ? WideToUtf8(name).c_str() : "<null>"
    );
    return VALIDATE::MAX;
}

Commission::Enum CItem::GetCommissionItemType(const wchar_t* name) {
    L2_GUARD_FUNCTION();

    struct Entry {
        const wchar_t* name;
        Commission::Enum type;
    };
    static constexpr Entry entries[] = {
        {L"none", Commission::None},
        {L"onehand_sword", Commission::OnehandSword},
        {L"onehand_magicsword", Commission::OnehandMagicSword},
        {L"dagger", Commission::Dagger},
        {L"rapier", Commission::Rapier},
        {L"twohand_sword", Commission::TwohandSword},
        {L"twohand_ancientsword", Commission::TwohandAncientSword},
        {L"dual_sword", Commission::DualSword},
        {L"dual_dagger", Commission::DualDagger},
        {L"onehand_blunt", Commission::OnehandBlunt},
        {L"onehand_magicblunt", Commission::OnehandMagicBlunt},
        {L"twohand_blunt", Commission::TwohandBlunt},
        {L"twohand_magicblunt", Commission::TwohandMagicBlunt},
        {L"dual_blunt", Commission::DualBlunt},
        {L"bow", Commission::Bow},
        {L"crossbow", Commission::CrossBow},
        {L"fist", Commission::Fist},
        {L"spear", Commission::Spear},
        {L"etc_weapon", Commission::EtcWeapon},
        {L"helmet", Commission::Helmet},
        {L"upbody", Commission::Upbody},
        {L"downbody", Commission::Downbody},
        {L"onepiece", Commission::Onepiece},
        {L"gloves", Commission::Gloves},
        {L"shoes", Commission::Shoes},
        {L"shield", Commission::Shield},
        {L"sigil", Commission::Sigil},
        {L"underwear", Commission::Underwear},
        {L"cloak", Commission::Cloak},
        {L"ring", Commission::Ring},
        {L"earring", Commission::Earring},
        {L"necklace", Commission::Necklace},
        {L"belt", Commission::Belt},
        {L"bracelet", Commission::Bracelet},
        {L"hair_accessary", Commission::HairAccessary},
        {L"potion", Commission::Potion},
        {L"weapon_enchant_scroll", Commission::WeaponEnchantScroll},
        {L"armor_enchant_scroll", Commission::ArmorEnchantScroll},
        {L"etc_scroll", Commission::EtcScroll},
        {L"soulshot", Commission::SoulShot},
        {L"spritshot", Commission::SpritShot},
        {L"etc_goods", Commission::EtcGoods},
        {L"pet_equipitem", Commission::PetEquipItem},
        {L"pet_goods", Commission::PetGoods},
        {L"crystal", Commission::Crystal},
        {L"recipe", Commission::Recipe},
        {L"main_material", Commission::MainMaterial},
        {L"weapon_variation_stone", Commission::WeaponVariationStone},
        {L"soul_crystal", Commission::SoulCrystal},
        {L"attribute_stone", Commission::AttributeStone},
        {L"weapon_enchant_aux_stone", Commission::WeaponEnchantAuxStone},
        {L"armor_enchant_aux_stone", Commission::ArmorEnchantAuxStone},
        {L"skill_book", Commission::SkillBook},
        {L"jemstone", Commission::Jemstone},
        {L"pouch", Commission::Pouch},
        {L"pin", Commission::Pin},
        {L"magic_rune_clip", Commission::MagicRuneClip},
        {L"magic_deco", Commission::MagicDeco},
        {L"dye", Commission::Dye},
        {L"etc_item", Commission::EtcItem},
        {L"talisman", Commission::Talisman},
        {L"agathion", Commission::Agathion},
        {L"jewel", Commission::Jewel},
        {L"artifact", Commission::Artifact},
    };
    for (const Entry& entry : entries) {
        if (std::wcscmp(name, entry.name) == 0) {
            return entry.type;
        }
    }
    return Commission::Invalid;
}

int CItem::GetOppositeAttribute(int attribute) {
    L2_GUARD_FUNCTION();
    switch (attribute) {
        case ATTRT_FIRE:
            return ATTRT_WATER;
        case ATTRT_WATER:
            return ATTRT_FIRE;
        case ATTRT_WIND:
            return ATTRT_EARTH;
        case ATTRT_EARTH:
            return ATTRT_WIND;
        case ATTRT_HOLY:
            return ATTRT_UNHOLY;
        case ATTRT_UNHOLY:
            return ATTRT_HOLY;
        default:
            // wchar_t message[96]{}
            // std::swprintf(
            //     message,
            //     sizeof(message) / sizeof(message[0]),
            //     L"Undefined attribute %d. [GetOppositeAttribute]",
            //     attribute)
            // const std::string portableMessage = ::WideToUtf8(message)
            // Log.Add(
            //     LOG_ERROR,
            //     "%s",
            //     portableMessage.c_str())
            // L2_LOG_ERROR(Log,
            //     "%s",
            //     portableMessage.c_str())
            L2_LOG_ERROR(Log, "Cannot find opposite of attribute %d; falling back to fire.", attribute);
            return ATTRT_FIRE;
    }
}

void CItem::SetShapeShiftingClassId(int classId) {
    L2_GUARD_FUNCTION();
    m_data->SetShapeShiftedClassId(classId);
}

bool CItem::CheckEnsoulOption(std::map<int, int> ensoulInfo[3]) {
    // Current L2Server 0x007dbeac, original owner Item.cpp.
    L2_GUARD_FUNCTION();
    std::array<int, static_cast<std::size_t>(EnsoulItem::EOT_MAX - 1)> optionTypeCount{};

    bool valid = true;
    for (int slotType = EnsoulItem::EIST_NORMAL; slotType < EnsoulItem::EIST_MAX && valid; ++slotType) {
        for (int slotIndex = 1; slotIndex <= m_const->m_ensoulSlotCount[slotType]; ++slotIndex) {
            EnsoulOptionData* optionData = nullptr;
            const auto requested = ensoulInfo[slotType].find(slotIndex);
            if (requested != ensoulInfo[slotType].end()) {
                const auto option = g_mEnsoulOptionData.find(requested->second);
                if (option == g_mEnsoulOptionData.end()) {
                    valid = false;
                    break;
                }
                optionData = option->second;
            } else {
                const auto current = m_mEnsoulInfo[slotType].find(slotIndex);
                if (current == m_mEnsoulInfo[slotType].end()) {
                    continue;
                }
                optionData = current->second;
            }

            const int optionId = optionData->m_nId;
            const int optionType = static_cast<int>(optionData->m_optionType);
            if (optionType < 1) {
                if (optionType == EnsoulItem::EOT_INVALID && optionId != 0) {
                    valid = false;
                    break;
                }
            } else {
                int& count = optionTypeCount[static_cast<std::size_t>(optionType - 1)];
                ++count;
                if (count > 1) {
                    valid = false;
                    break;
                }
            }
        }
    }
    return valid;
}

bool CItem::CheckEnsoulSlotIndex(EnsoulItem::EnsoulItemSlotType slotType, int slotIndex) {
    L2_GUARD_FUNCTION();
    return slotIndex <= m_const->m_ensoulSlotCount[static_cast<int>(slotType)];
}

bool CItem::IsEnsoulItem() {
    L2_GUARD_FUNCTION();
    for (int slotType = 1; slotType < 3; ++slotType) {
        for (const auto& option : m_mEnsoulInfo[slotType]) {
            if (option.second && option.second->m_nId > 0) {
                return true;
            }
        }
    }
    return false;
}

int CItem::GetEnsoulCount() {
    L2_GUARD_FUNCTION();
    int count = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        const auto& options = m_mEnsoulInfo[slotType];
        for (int slotIndex = 1; static_cast<std::size_t>(slotIndex) <= options.size(); ++slotIndex) {
            const auto option = options.find(slotIndex);
            if (option != options.end() && option->second && option->second->m_nId > 0) {
                ++count;
            }
        }
    }
    return count;
}

void CItem::GetEnsoulSkills(std::map<int, const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();
    for (int slotType = EnsoulItem::EIST_NORMAL; slotType < EnsoulItem::EIST_MAX; ++slotType) {
        for (const auto& option : m_mEnsoulInfo[slotType]) {
            for (const CSkillInfo* const skill : option.second->m_vSkillList) {
                skills[skill->m_nSkillId] = skill;
            }
        }
    }
}

std::int16_t CItem::GetItemDamage() const {
    L2_GUARD_FUNCTION();
    std::uint16_t rawDamage = 0;
    if (m_data->m_nClassID == 0x115a) {
        const std::uint32_t enchanted = static_cast<std::uint32_t>(m_data->GetEnchanted());
        rawDamage = static_cast<std::uint16_t>(enchanted >> 16);
    } else if (m_data->m_nClassID == 0x115b || m_data->m_nClassID == 0x115c) {
        rawDamage = static_cast<std::uint16_t>(m_data->m_nDamaged / 100);
    } else {
        rawDamage = static_cast<std::uint16_t>(m_data->m_nDamaged);
    }

    std::int16_t result = 0;
    std::memcpy(&result, &rawDamage, sizeof(result));
    return result;
}

bool CItem::IsSameItem(CItem& item) const {
    L2_GUARD_FUNCTION();
    return m_data->m_nClassID == item.m_data->m_nClassID && m_data->m_bBlessed == item.m_data->m_bBlessed &&
        m_data->GetEnchanted() == item.m_data->GetEnchanted() && m_data->m_nDamaged == item.m_data->m_nDamaged &&
        m_data->m_nDuration == item.m_data->m_nDuration && m_data->m_bIsVariation == item.m_data->m_bIsVariation &&
        m_itemBaseAttribute == item.m_itemBaseAttribute;
}

bool CItem::IsSellableToStore(User* user, bool flag, bool commission) {
    L2_GUARD_FUNCTION();
    const bool tradable = commission ? IsNPCTradable(user, flag) : IsTradable(user, flag);
    return tradable && m_data->m_nClassID != 0x39;
}

bool CItem::IsSellableToPrivateStore(User* user, bool flag) {
    L2_GUARD_FUNCTION();
    return IsSellableToStore(user, flag, false) && m_const->m_bIsPrivateStore;
}

bool CItem::IsSellableToNpcStore(User* user, bool flag) {
    L2_GUARD_FUNCTION();
    return IsSellableToStore(user, flag, true) && !IsPetCollarItem();
}

bool CItem::IsNPCTrade() { return m_const->m_bIsNPCTrade; }

bool CItem::IsAttribution() { return m_const->m_nAttribution > 0; }

bool CItem::IsTrade() { return m_const->m_bIsTrade; }

bool CItem::CanKeep(WareHouseClass warehouseClass) { return (m_const->m_nKeepType & static_cast<int>(warehouseClass)) > 0; }

bool CItem::IsDrop() { return m_const->m_bIsDrop; }

bool CItem::IsDestruct() { return m_const->m_bIsDestruct; }

void CItem::Set(ObjectFieldType field, std::vector<int>* values) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        const std::size_t size = values->size();
        const int numericField = static_cast<int>(field);
        bool valid = true;
        // const char* failureFormat = nullptr
        // int failureLine = 0
        const char* failureField = nullptr;
        std::size_t expectedCount = 0;

        switch (numericField) {
            case 0x0e:
                if (size == 2) {
                    m_const->m_Reduced_soulshot_prob = runtime::CheckedVectorAt((*values), 0);
                    m_const->m_Reduced_soulshot_count = runtime::CheckedVectorAt((*values), 1);
                } else {
                    valid = false;
                    // failureLine = 0x39d
                    // failureFormat =
                    //     "[%s][%d] reduced soulshot loading failed. "
                    //     "pIntVector size [%d].".
                    failureField = "reduced soulshot";
                    expectedCount = 2;
                }
                break;
            case 0x0f:
                if (size == 2) {
                    m_const->m_Reduced_spiritshot_prob = runtime::CheckedVectorAt((*values), 0);
                    m_const->m_Reduced_spiritshot_count = runtime::CheckedVectorAt((*values), 1);
                } else {
                    valid = false;
                    // failureLine = 0x3ab
                    // failureFormat =
                    //     "[%s][%d] reduced spiritshot loading failed. "
                    //     "pIntVector size [%d].".
                    failureField = "reduced spiritshot";
                    expectedCount = 2;
                }
                break;
            case 0x31:
                if (size == 4) {
                    m_const->m_DamageRange.target = runtime::CheckedVectorAt((*values), 0);
                    m_const->m_DamageRange.direction = runtime::CheckedVectorAt((*values), 1);
                    m_const->m_DamageRange.distance = runtime::CheckedVectorAt((*values), 2);
                    m_const->m_DamageRange.angle = runtime::CheckedVectorAt((*values), 3);
                } else {
                    valid = false;
                    // failureLine = 0x381
                    // failureFormat =
                    //     "[%s][%d] damage range loading failed. "
                    //     "pIntVector size [%d].".
                    failureField = "damage range";
                    expectedCount = 4;
                }
                break;
            case 0x35:
                if (size == 2) {
                    m_const->m_Reduced_mp_consume_prob = runtime::CheckedVectorAt((*values), 0);
                    m_const->m_Reduced_mp_consume = runtime::CheckedVectorAt((*values), 1);
                } else {
                    valid = false;
                    // failureLine = 0x38f
                    // failureFormat =
                    //     "[%s][%d] reduced mp consume loading failed. "
                    //     "pIntVector size [%d].".
                    failureField = "reduced MP consumption";
                    expectedCount = 2;
                }
                break;
            case 0x10f:
                if (size == 2) {
                    m_const->m_nDuration = runtime::CheckedVectorAt((*values), 0);
                } else {
                    valid = false;
                    // failureLine = 0x3b9
                    // failureFormat =
                    //     "[%s][%d] item duration loading failed. "
                    //     "pIntVector size [%d].".
                    failureField = "duration";
                    expectedCount = 2;
                }
                break;
            case 0x111:
                if (size == 2) {
                    m_const->m_BaseAttribute.m_nAttackType = static_cast<std::int16_t>(runtime::CheckedVectorAt((*values), 0));
                    m_const->m_BaseAttribute.m_nAttackValue = static_cast<std::int16_t>(runtime::CheckedVectorAt((*values), 1));
                } else {
                    valid = false;
                    // failureLine = 0x3c5
                    // failureFormat =
                    //     "[%s][%d] item base attribute attack param loading "
                    //     "failed. pIntVector size [%d]".
                    failureField = "base attack attribute";
                    expectedCount = 2;
                }
                break;
            case 0x112:
                if (size == 6) {
                    for (std::size_t index = 0; index < 6; ++index) {
                        m_const->m_BaseAttribute.m_nDefendValue[index] =
                            static_cast<std::int16_t>(runtime::CheckedVectorAt((*values), index));
                    }
                } else {
                    valid = false;
                    // failureLine = 0x3d3
                    // failureFormat =
                    //     "[%s][%d] item base attribute defend param loading "
                    //     "failed. pIntVector size [%d]".
                    failureField = "base defence attributes";
                    expectedCount = 6;
                }
                break;
            case 0x136:
                if (size == 2 && IsWeapon()) {
                    m_const->m_ensoulSlotCount[1] = runtime::CheckedVectorAt((*values), 0);
                    m_const->m_ensoulSlotCount[2] = runtime::CheckedVectorAt((*values), 1);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] item(%d) ensoul slot count loading failed. "
                    //     L"pIntVector size [%d == 2] and IsWeapon(%s == true)",
                    //     L"Item.cpp",
                    //     0x3e2,
                    //     m_data->m_nClassID,
                    //     static_cast<int>(size),
                    //     IsWeapon() ? L"true" : L"false")
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] item(%d) ensoul slot count loading failed. "
                    //     L"pIntVector size [%d == 2] and IsWeapon(%s == true)",
                    //     L"Item.cpp",
                    //     0x3e2,
                    //     m_data->m_nClassID,
                    //     static_cast<int>(size),
                    //     IsWeapon() ? L"true" : L"false")
                    L2_LOG_ERROR(
                        Log,
                        "Item class %d: ensoul slot counts require 2 values and a weapon; got %zu values, weapon=%s. Slot "
                        "counts unchanged.",
                        m_data->m_nClassID, size, IsWeapon() ? "yes" : "no"
                    );
                }
                break;
            default:
                break;
        }

        if (!valid) {
            // Log.Add(
            //     LOG_ERROR,
            //     failureFormat,
            //     "Item.cpp",
            //     failureLine,
            //     static_cast<int>(size))
            // L2_LOG_ERROR(Log,
            //     failureFormat,
            //     "Item.cpp",
            //     failureLine,
            //     static_cast<int>(size))
            L2_LOG_ERROR(
                Log, "Item class %d: %s requires %zu values, got %zu; property unchanged.", m_data->m_nClassID, failureField,
                expectedCount, size
            );
        }
    }
}

void CItem::Set(ObjectFieldType field, wchar_t* value, int probability) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        if (static_cast<int>(field) == 0x3f && !IsNone(value)) {
            if (!g_ObjectDB.m_bOnlyItemPrecompiledHeader) {
                g_ItemTempDataDB[static_cast<int>(m_nId)].m_MagicSkill = g_SkillDB.GetSkillKeyByName(value);
            }
            m_const->m_nMagicSkillProb = probability;
        }
    }
}

void CItem::Set(ObjectFieldType field, wchar_t* value) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        const int numericField = static_cast<int>(field);
        if (numericField == 1) {
            g_ObjectDB.RegisterName(this, value);
        } else if (numericField == 0xaf) {
            CopyItemHtml(g_ItemHtml[m_nClassId], value);
        } else if (numericField == 0x23 || numericField == 0x3d || numericField == 0x3e || numericField == 0x3f ||
            numericField == 0x40) {
            if (!IsNone(value) && !g_ObjectDB.m_bOnlyItemPrecompiledHeader) {
                CItemTempData& staged = g_ItemTempDataDB[static_cast<int>(m_nId)];
                const CSkillKey key = g_SkillDB.GetSkillKeyByName(value);
                switch (numericField) {
                    case 0x23:
                        staged.m_ItemSkill = key;
                        staged.m_ItemSkillList.push_back(key);
                        break;
                    case 0x3d:
                        staged.m_CriticalSkill = key;
                        break;
                    case 0x3e:
                        staged.m_AttackSkill = key;
                        break;
                    case 0x3f:
                        staged.m_MagicSkill = key;
                        break;
                    case 0x40:
                        staged.m_EnchantedFourSkill = key;
                        break;
                    default:
                        break;
                }
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Undefined ObjectFieldType[%d], "
            //     L"value(%s) on [%d].",
            //     L"Item.cpp",
            //     0x46c,
            //     numericField,
            //     value,
            //     m_data->m_nClassID)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Undefined ObjectFieldType[%d], "
            //     L"value(%s) on [%d].",
            //     L"Item.cpp",
            //     0x46c,
            //     numericField,
            //     value,
            //     m_data->m_nClassID)
            L2_LOG_ERROR(
                Log, "Item class %d: unsupported text field %d with value '%s'; value ignored.", m_data->m_nClassID,
                numericField, value ? WideToUtf8(value).c_str() : "<null>"
            );
        }
    }
}

void CItem::Set(ObjectFieldType field, std::int64_t value) {
    L2_GUARD_FUNCTION();
    const bool handled = [&] {
        L2_GUARD(*m_lock);
        if (static_cast<int>(field) == 0x12) {
            m_const->m_nDefaultPrice = value;
            return true;
        }
        return false;
    }();
    if (!handled) {
        Set(field, static_cast<double>(value));
        {
            L2_GUARD(*m_lock);
        }
    }
}

void CItem::Set(ObjectFieldType field, int value) {
    L2_GUARD_FUNCTION();
    const bool handled = [&] {
        L2_GUARD(*m_lock);
        const int numericField = static_cast<int>(field);
        bool handled = true;
        switch (numericField) {
            case 0x00:
                m_nClassId = value;
                m_data->m_nClassID = value;
                break;
            case 0x04:
                m_const->m_ArmorType = static_cast<ArmorType>(value);
                m_const->m_AdditionalArmorType = static_cast<AdditionalArmorType>(value);
                break;
            case 0x05:
                m_data->m_Type = static_cast<ItemType>(value);
                break;
            case 0x07:
                m_data->m_bBlessed = value;
                break;
            case 0x08:
                m_const->m_nWeight = value;
                break;
            case 0x09:
                m_data->m_ItemConsumeType = static_cast<ItemConsumeType>(value);
                break;
            case 0x0a:
                m_const->m_nInitialCount = value;
                break;
            case 0x0c:
                m_const->m_nSoulShotCount = value;
                break;
            case 0x0d:
                m_const->m_nSpiritShotCount = value;
                break;
            case 0x10:
                m_const->m_bImmediateEffect = value != 0;
                break;
            case 0x11:
                m_const->m_nPrice = static_cast<std::int64_t>(value);
                break;
            case 0x12:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[CItem][Set(token, int32)] goto __int64")
                // L2_LOG_ERROR(Log,
                //     L"[CItem][Set(token, int32)] goto __int64")
                L2_LOG_ERROR(
                    Log,
                    "Item class %d: default price %d passed to the 32-bit setter; value ignored. Use the 64-bit setter.",
                    m_data->m_nClassID, value
                );
                break;
            case 0x24:
                m_const->m_Material = value;
                break;
            case 0x25:
                m_const->m_nCrystalCount = value;
                break;
            case 0x38:
                m_data->m_nDamaged = value;
                break;
            case 0x26:
                m_const->m_bIsTrade = value != 0;
                break;
            case 0x27:
                m_const->m_bIsDrop = value != 0;
                break;
            case 0x28:
                m_const->m_bIsDestruct = value != 0;
                break;
            case 0x2a:
                m_const->m_nRandomDamage = value;
                break;
            case 0x2b:
                m_const->m_AttackType = static_cast<AttackType>(value);
                break;
            case 0x2e:
                m_const->m_nAttackRange = value;
                break;
            case 0x2f:
                m_const->m_nEffectiveRange = value;
                break;
            case 0x33:
                m_const->m_nReuseDelay = value;
                break;
            case 0x34:
                m_const->m_nMpConsume = value;
                break;
            case 0x3b:
                m_data->SetEnchanted(value);
                break;
            case 0x3c:
                m_const->m_sMpBonus =
                    static_cast<std::uint32_t>(value) + 0x7fffU < 0xfffeU ? static_cast<std::int16_t>(value) : 0;
                break;
            case 0x41:
                m_const->m_bForNpc = value == 1;
                break;
            case 0x42:
                m_const->m_nAttribution = value;
                break;
            case 0x43:
                m_const->m_bIsNPCTrade = value != 0;
                break;
            case 0x44:
                m_const->m_nGroupId = value;
                break;
            case 0x45:
                m_const->m_bhLockedAble = value != 0;
                break;
            case 0xaa:
                m_const->m_EtcitemType = value;
                break;
            case 0xad:
                m_const->m_cDualRate = static_cast<std::uint32_t>(value) < 101U ? static_cast<std::uint8_t>(value) : 0;
                break;
            case 0xdf:
                m_const->m_bIsMagicWeapon = value == 1;
                break;
            case 0xe2:
                m_const->m_nItemEnchantType = value;
                break;
            case 0xe6:
                m_const->m_nHennaId = value;
                break;
            case 0xfe:
                m_const->m_nManorId = value;
                break;
            case 0xff:
                m_const->m_nManorLev = value;
                break;
            case 0x100:
                m_const->m_nManorCropId = value;
                break;
            case 0x101:
                m_const->m_nManorCropIdReplace = value;
                break;
            case 0x104:
                m_const->m_bExImmediateEffect = value != 0;
                break;
            case 0x109:
                m_const->m_nDropPeriod = static_cast<std::int32_t>(MultiplyCurrentInt32(value, 60000U));
                break;
            case 0x10c:
                m_const->m_nDelayShareGroup = static_cast<std::uint32_t>(value) - 1U < 200U ? value : -1;
                break;
            case 0x10d:
                m_const->m_nKeepType = value;
                break;
            case 0x10f:
                m_const->m_nDuration = value;
                break;
            case 0x117:
                m_const->m_bCanAttrEnchant = value != 0;
                break;
            case 0x11c:
                if (value == 0) {
                    m_const->m_bNoAttackWeapon = true;
                } else if (value == 1) {
                    m_const->m_bForceEquip = true;
                } else if (value == 2) {
                    m_const->m_bOnlyUseEquipedWeaponActiveSkill = true;
                }
                break;
            case 0x11e:
                m_const->m_nUseSkillDistime = value;
                break;
            case 0x11f:
                m_const->m_nEquipReuseDelay = value;
                break;
            case 0x120:
                m_const->m_bIsPremium = value == 1;
                break;
            case 0x121:
                m_const->m_period = value;
                break;
            case 0x125:
                m_const->m_bCanOlympiadUse = value == 1;
                break;
            case 0x126:
                m_const->m_bCanNotPledgeGameUse = value == 0;
                break;
            case 0x128:
                m_const->m_bIsItemChanceAffected = value == 1;
                break;
            case 0x129:
                m_const->m_dAgathionEnergy = static_cast<double>(value);
                break;
            case 0x12a:
                m_const->m_dAgathionMaxEnergy = static_cast<double>(value);
                break;
            case 0x12e:
                m_const->m_nIsHeroEntryType = value;
                break;
            case 0x131:
                m_const->m_bHpLimitBreak = value == 1;
                break;
            case 0x0b:
            case 0x29:
            case 0x2c:
            case 0x2d:
            case 0x32:
            case 0x36:
            case 0x37:
            case 0x39:
            case 0x3a:
                break;
            default:
                handled = false;
                break;
        }

        return handled;
    }();
    if (!handled) {
        Set(field, static_cast<double>(value));
        {
            L2_GUARD(*m_lock);
        }
    }
}

void CItem::Set(ObjectFieldType field, double value) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        if (field != OBJECT_FIELD_HIT_MODIFY && field != OBJECT_FIELD_AVOIDE_MODIFY &&
            field != OBJECT_FIELD_SHIELD_DEFEND_RATE && field != OBJECT_FIELD_SHIELD_DEFEND) {
            // Log.Add(
            //     LOG_ERROR,
            //     const_cast<wchar_t*>(
            //         L"[%s][%d] Invalid Object field type [%d][%f]."),
            //     L"Item.cpp",
            //     0x348,
            //     static_cast<int>(field),
            //     value)
            // L2_LOG_ERROR(Log,
            //     const_cast<wchar_t*>(
            //         L"[%s][%d] Invalid Object field type [%d][%f]."),
            //     L"Item.cpp",
            //     0x348,
            //     static_cast<int>(field),
            //     value)
            L2_LOG_ERROR(
                Log, "Item class %d: unsupported floating-point field %d with value %.17g; value ignored.",
                m_data->m_nClassID, static_cast<int>(field), value
            );
        }
    }
}

void CItem::Set2(std::vector<std::int64_t>* values) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        for (const std::int64_t value : *values) {
            m_data->m_Slot |= value;
        }
    }
}

CItemRestrict::CItemRestrict() {
    L2_GUARD_FUNCTION();
    m_bIsOn = false;
}

CItemRestrict::~CItemRestrict() = default;

CItemRestrict* CItemRestrict::m_ItemRestrict = nullptr;

void CItemRestrict::SetState(bool state) {
    L2_GUARD_FUNCTION();
    m_bIsOn = state;
}

bool CItemRestrict::IsRestrictItem(int itemClassId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    return std::find(m_vItemVector.begin(), m_vItemVector.end(), itemClassId) != m_vItemVector.end();
}

bool CItemRestrict::CheckItemRestrict(int itemClassId) {
    L2_GUARD_FUNCTION();
    if (!m_bIsOn) {
        return false;
    }
    return IsRestrictItem(itemClassId);
}

bool CItemRestrict::LoadData() {
    L2_GUARD_FUNCTION();

    XMLElement* const root = CXmlLoader::inst().Load(kItemRestrictPath);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Fail to load %s",
        //     kItemSource,
        //     0x1ad9,
        //     "..\\Script\\Itemrestrict.xml")
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Fail to load %s",
        //     kItemSource,
        //     0x1ad9,
        //     "..\\Script\\Itemrestrict.xml")
        L2_LOG_ERROR(Log, "Cannot load Itemrestrict.xml; existing restriction list retained.");
        return false;
    }

    for (XMLElement* restrictItem : root->m_vChild) {
        if (!restrictItem || !EqualsAsciiCaseInsensitive(runtime::StringView(restrictItem->m_sName), u"RestrictItem")) {
            continue;
        }

        for (XMLElement* item : restrictItem->m_vChild) {
            if (!item || !EqualsAsciiCaseInsensitive(runtime::StringView(item->m_sName), u"Item")) {
                continue;
            }

            std::u16string& itemId = item->m_smapAttr[std::u16string(u"ItemID")];
            m_vItemVector.push_back(ItemRestrictItemId(runtime::StringView(itemId)));
        }
    }

    // Current LoadData leaves the CXmlLoader tree allocated.
    return true;
}

bool CItemRestrict::ReLoadData() {
    L2_GUARD_FUNCTION();

    XMLElement* const root = CXmlLoader::inst().Load(kItemRestrictPath);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Fail to load %s",
        //     kItemSource,
        //     0x1afb,
        //     "..\\Script\\Itemrestrict.xml")
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Fail to load %s",
        //     kItemSource,
        //     0x1afb,
        //     "..\\Script\\Itemrestrict.xml")
        L2_LOG_ERROR(Log, "Cannot reload Itemrestrict.xml; existing restriction list retained.");
        return false;
    }

    L2_GUARD(m_lock);
    m_vItemVector.clear();

    for (XMLElement* restrictItem : root->m_vChild) {
        if (!restrictItem || !EqualsAsciiCaseInsensitive(runtime::StringView(restrictItem->m_sName), u"RestrictItem")) {
            continue;
        }

        for (XMLElement* item : restrictItem->m_vChild) {
            if (!item || !EqualsAsciiCaseInsensitive(runtime::StringView(item->m_sName), u"Item")) {
                continue;
            }

            std::u16string& itemId = item->m_smapAttr[std::u16string(u"ItemID")];
            m_vItemVector.push_back(ItemRestrictItemId(runtime::StringView(itemId)));
        }
    }

    // Current ReLoadData likewise does not retire the loaded XML tree.
    return true;
}

CSetItem::CSetItem(bool createFail) :
    m_nSetItemId(0),
    m_nAdditionalItemSlot(0),
    m_p_def_inc(0.0),
    m_m_def_inc(0.0),
    m_hp_inc(0.0),
    m_mp_inc(0.0),
    m_breath_inc(0.0),
    m_magic_resist_inc(0.0),
    m_casting_speed_inc(0.0),
    m_move_speed_inc(0.0),
    m_p_def_inc_add(0.0),
    m_m_def_inc_add(0.0),
    m_hp_inc_add(0.0),
    m_mp_inc_add(0.0),
    m_breath_inc_add(0.0),
    m_magic_resist_inc_add(0.0),
    m_move_speed_inc_add(0.0),
    m_casting_speed_inc_add(0.0),
    m_p_attack_inc(0.0),
    m_m_attack_inc(0.0),
    m_hp_regen_inc(0.0),
    m_mp_regen_inc(0.0),
    m_avoid_inc(0.0),
    m_shield_def_prob_inc(0.0),
    m_weight_limit_inc(0.0),
    m_resist_poison_inc(0.0),
    m_p_def_vs_dagger_inc(0.0),
    m_p_attack_inc_add(0.0),
    m_m_attack_inc_add(0.0),
    m_hp_regen_inc_add(0.0),
    m_mp_regen_inc_add(0.0),
    m_avoid_inc_add(0.0),
    m_shield_def_prob_inc_add(0.0),
    m_weight_limit_inc_add(0.0),
    m_resist_poison_inc_add(0.0),
    m_p_def_vs_dagger_inc_add(0.0),
    m_nAdditional2(0),
    m_pSkills{} {
    L2_GUARD_FUNCTION();
    if (createFail) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]CSetItem Create fail.",
        //     kItemSource,
        //     0x164b)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]CSetItem Create fail.",
        //     kItemSource,
        //     0x164b)
        L2_LOG_ERROR(
            Log, "Item set constructor received a creation-failure flag; construction continues with default fields."
        );
    }
}

CSetItem::~CSetItem() { L2_GUARD_FUNCTION(); }

void CSetItem::Set(ObjectFieldType field, int value) {
    L2_GUARD_FUNCTION();
    if (field == OBJECT_FIELD_ITEM_ID) {
        m_nSetItemId = static_cast<std::uint32_t>(value);
    }
}

void CSetItem::SetItemSlot2(ObjectFieldType field, std::uint32_t value) {
    L2_GUARD_FUNCTION();
    if (field == OBJECT_FIELD_SETITEM_ADDITIONAL) {
        m_nAdditionalItemSlot = value;
    } else if (field == OBJECT_FIELD_SETITEM_ADDITIONAL2) {
        m_nAdditional2 = value;
    }
}

void CSetItem::SetItemSlot(ObjectFieldType field, std::vector<int>* itemClassIds) {
    L2_GUARD_FUNCTION();

    int slot;
    switch (field) {
        case OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR:
            slot = 0;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_REAR:
            slot = 1;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_LEAR:
            slot = 2;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_NECK:
            slot = 3;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_RFINGER:
            slot = 4;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_LFINGER:
            slot = 5;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_HEAD:
            slot = 6;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_RHAND:
            slot = 7;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_LHAND:
            slot = 8;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_GLOVES:
            slot = 9;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_CHEST:
            slot = 10;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_LEGS:
            slot = 11;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_FEET:
            slot = 12;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_BACK:
            slot = 13;
            break;
        case OBJECT_FIELD_SETITEM_SLOT_RLHAND:
            slot = 14;
            break;
        default:
            return;
    }
    m_mapSlotToItemId.emplace(slot, itemClassIds);
}

void CSetItem::SetSkill(ObjectFieldType field, wchar_t* skillName) {
    L2_GUARD_FUNCTION();

    std::u16string CSetItemTempData::* member = nullptr;
    switch (field) {
        case OBJECT_FIELD_SETITEM_SET_SKILL:
            member = &CSetItemTempData::m_wsSkillName;
            break;
        case OBJECT_FIELD_SETITEM_EFFECT_SKILL:
            member = &CSetItemTempData::m_wsSetSkillName;
            break;
        case OBJECT_FIELD_SETITEM_ADDITIONAL_EFFECT_SKILL:
            member = &CSetItemTempData::m_wsAddtionalSetSkillName;
            break;
        case OBJECT_FIELD_SETITEM_ADDITIONAL2_EFFECT_SKILL:
            member = &CSetItemTempData::m_wsAddtional2SetSkillName;
            break;
        default:
            return;
    }

    if (std::wcscmp(skillName, L"none") == 0) {
        return;
    }

    CSetItemTempData& staged = g_SetItemTempDataDB[static_cast<int>(m_nSetItemId)];
    std::u16string* const destination = &(staged.*member);

    const std::u16string targetName = ::WideToUtf16(std::wstring_view(skillName));
    destination->assign(targetName);
}

void CSetItem::SetEffectParam(ObjectFieldType field, int baseValue, int additionalValue) {
    L2_GUARD_FUNCTION();

    switch (field) {
        case OBJECT_FIELD_SETITEM_P_DEF_INC:
            m_p_def_inc = static_cast<double>(baseValue);
            m_p_def_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_M_DEF_INC:
            m_m_def_inc = static_cast<double>(baseValue);
            m_m_def_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_HP_INC:
            m_hp_inc = static_cast<double>(baseValue);
            m_hp_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_MP_INC:
            m_mp_inc = static_cast<double>(baseValue);
            m_mp_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_BREATHE_INC:
            m_breath_inc = static_cast<double>(baseValue);
            m_breath_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_MOVE_SPEED_INC:
            m_move_speed_inc = static_cast<double>(baseValue);
            m_move_speed_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_MR_INC:
            m_magic_resist_inc = static_cast<double>(baseValue);
            m_magic_resist_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_CAST_SPEED_INC:
            m_casting_speed_inc = static_cast<double>(baseValue);
            m_casting_speed_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_P_ATTACK_INC:
            m_p_attack_inc = static_cast<double>(baseValue);
            m_p_attack_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_M_ATTACK_INC:
            m_m_attack_inc = static_cast<double>(baseValue);
            m_m_attack_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_HP_REGEN_INC:
            m_hp_regen_inc = static_cast<double>(baseValue);
            m_hp_regen_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_MP_REGEN_INC:
            m_mp_regen_inc = static_cast<double>(baseValue);
            m_mp_regen_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_STR_INC:
            m_Stat.m_N[1] = baseValue;
            m_StatAdd.m_N[1] = additionalValue;
            break;
        case OBJECT_FIELD_SETITEM_CON_INC:
            m_Stat.m_N[2] = baseValue;
            m_StatAdd.m_N[2] = additionalValue;
            break;
        case OBJECT_FIELD_SETITEM_DEX_INC:
            m_Stat.m_N[4] = baseValue;
            m_StatAdd.m_N[4] = additionalValue;
            break;
        case OBJECT_FIELD_SETITEM_INT_INC:
            m_Stat.m_N[0] = baseValue;
            m_StatAdd.m_N[0] = additionalValue;
            break;
        case OBJECT_FIELD_SETITEM_MEN_INC:
            m_Stat.m_N[3] = baseValue;
            m_StatAdd.m_N[3] = additionalValue;
            break;
        case OBJECT_FIELD_SETITEM_WIT_INC:
            m_Stat.m_N[5] = baseValue;
            m_StatAdd.m_N[5] = additionalValue;
            break;
        case OBJECT_FIELD_SETITEM_AVOID_INC:
            m_avoid_inc = static_cast<double>(baseValue);
            m_avoid_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_SHIELD_DEF_PROB_INC:
            m_shield_def_prob_inc = static_cast<double>(baseValue);
            m_shield_def_prob_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_WEIGHT_LIMIT_INC:
            m_weight_limit_inc = static_cast<double>(baseValue);
            m_weight_limit_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_RESIST_POISON_INC:
            m_resist_poison_inc = static_cast<double>(baseValue);
            m_resist_poison_inc_add = static_cast<double>(additionalValue);
            break;
        case OBJECT_FIELD_SETITEM_P_DEF_VS_DAGGER_INC:
            m_p_def_vs_dagger_inc = static_cast<double>(baseValue);
            m_p_def_vs_dagger_inc_add = static_cast<double>(additionalValue);
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"unknown filed type [%d]",
            //     static_cast<int>(field))
            // L2_LOG_ERROR(Log,
            //     L"unknown filed type [%d]",
            //     static_cast<int>(field))
            L2_LOG_ERROR(
                Log, "Item set %u: unsupported integer effect field %d; effect unchanged.",
                static_cast<unsigned>(m_nSetItemId), static_cast<int>(field)
            );
            break;
    }
}

void CSetItem::SetEffectParamDouble(ObjectFieldType field, double baseValue, double additionalValue) {
    L2_GUARD_FUNCTION();

    double* base = nullptr;
    double* additional = nullptr;
    switch (field) {
        case OBJECT_FIELD_SETITEM_P_DEF_INC:
            base = &m_p_def_inc;
            additional = &m_p_def_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_M_DEF_INC:
            base = &m_m_def_inc;
            additional = &m_m_def_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_HP_INC:
            base = &m_hp_inc;
            additional = &m_hp_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_MP_INC:
            base = &m_mp_inc;
            additional = &m_mp_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_BREATHE_INC:
            base = &m_breath_inc;
            additional = &m_breath_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_MOVE_SPEED_INC:
            base = &m_move_speed_inc;
            additional = &m_move_speed_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_MR_INC:
            base = &m_magic_resist_inc;
            additional = &m_magic_resist_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_CAST_SPEED_INC:
            base = &m_casting_speed_inc;
            additional = &m_casting_speed_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_P_ATTACK_INC:
            base = &m_p_attack_inc;
            additional = &m_p_attack_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_M_ATTACK_INC:
            base = &m_m_attack_inc;
            additional = &m_m_attack_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_HP_REGEN_INC:
            base = &m_hp_regen_inc;
            additional = &m_hp_regen_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_MP_REGEN_INC:
            base = &m_mp_regen_inc;
            additional = &m_mp_regen_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_AVOID_INC:
            base = &m_avoid_inc;
            additional = &m_avoid_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_SHIELD_DEF_PROB_INC:
            base = &m_shield_def_prob_inc;
            additional = &m_shield_def_prob_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_WEIGHT_LIMIT_INC:
            base = &m_weight_limit_inc;
            additional = &m_weight_limit_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_RESIST_POISON_INC:
            base = &m_resist_poison_inc;
            additional = &m_resist_poison_inc_add;
            break;
        case OBJECT_FIELD_SETITEM_P_DEF_VS_DAGGER_INC:
            base = &m_p_def_vs_dagger_inc;
            additional = &m_p_def_vs_dagger_inc_add;
            break;
        default:
            return;
    }
    *base = baseValue;
    *additional = additionalValue;
}

int CSetItem::GetSlotSetItemId(int slot) const {
    L2_GUARD_FUNCTION();
    const auto found = m_mapSlotToItemId.find(slot);
    if (found == m_mapSlotToItemId.end() || found->second == nullptr) {
        return 0;
    }
    return found->second->front();
}

void CSetItem::PushSetItemToUser(User& user, int enchanted) {
    // Current L2Server 0x007de2f4.
    L2_GUARD_FUNCTION();
    for (int slot = 0; slot < 0x3f; ++slot) {
        if (GetSlotSetItemId(slot) == 0) {
            continue;
        }
        InventoryParams parameters;
        parameters.nItemClassID = GetSlotSetItemId(slot);
        parameters.nCount = 1;
        parameters.nEnchanted = enchanted;
        static_cast<void>(user.AddItemToInventory(parameters));
    }
}

bool CSetItem::PostLoad() {
    L2_GUARD_FUNCTION();
    const int setItemId = static_cast<int>(m_nSetItemId);
    if (g_SetItemTempDataDB.find(setItemId) != g_SetItemTempDataDB.end()) {
        CSetItemTempData& staged = g_SetItemTempDataDB[setItemId];
        const std::u16string* const names[SIS_Max] = {
            &staged.m_wsSkillName,
            &staged.m_wsSetSkillName,
            &staged.m_wsAddtionalSetSkillName,
            &staged.m_wsAddtional2SetSkillName,
        };
        for (int index = 0; index < SIS_Max; ++index) {
            if (!names[index]->empty()) {
                const std::wstring hostName = Utf16ToWide(*names[index]);
                m_pSkills[index] = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(hostName.c_str()));
            }
        }
    }
    return true;
}

int CSetItem::SlotToSetItemClassID(int slot, int itemClassId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_mapSlotToItemId.find(slot);
    if (found == m_mapSlotToItemId.end() || found->second == nullptr) {
        return 0;
    }
    const std::vector<int>& itemClassIds = *found->second;
    return std::find(itemClassIds.begin(), itemClassIds.end(), itemClassId) != itemClassIds.end() ? itemClassId : 0;
}


CItemReleaseVerifier::CItemReleaseVerifier(CItem* item, const wchar_t* caller) : m_pItem(item), m_guardStr(caller) {
    m_pItem->m_bDeleting = true;
    L2_ADD_REF(m_pItem, ObjectRefType::VerifierRetain);
    AddTimer(ItemReleaseDelay(m_pItem), 0);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CItemReleaseVerifier::TimerExpired(int) {
    // Capture diagnostic identity while the verifier still owns its reference.
    const std::int32_t classId = ItemClassId(m_pItem);
    const std::int32_t remaining = L2_RELEASE(m_pItem, ObjectRefType::VerifierRetain);
    if (remaining > 0) {
        // const std::int32_t classId = ItemClassId(m_pItem)
        // const std::string caller = ::WideToUtf8(
        //     std::wstring_view(m_guardStr))
        const std::string caller = m_guardStr ? ::WideToUtf8(m_guardStr) : "<unknown>";
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] ORT - Item Release failed. "
        //     "item class [%d], refcount[%d] Caller is [%s]",
        //     kItemSource,
        //     0x15f,
        //     classId,
        //     remaining,
        //     caller.c_str())
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] ORT - Item Release failed. "
        //     "item class [%d], refcount[%d] Caller is [%s]",
        //     kItemSource,
        //     0x15f,
        //     classId,
        //     remaining,
        //     caller.c_str())
        L2_LOG_ERROR(
            Log, "Item class %d still has %d references after verifier release; requested by %s. Reporting references.",
            classId, remaining, caller.c_str()
        );
        m_pItem->ReportOrt(kItemSource, 0x160);
        g_ServerContext.NotReleasedItem.fetch_add(1, std::memory_order_seq_cst);
    }
}
