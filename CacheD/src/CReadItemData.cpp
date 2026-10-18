#include "CReadItemData.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"

#include <cstddef>
#include <cstdint>

namespace {

template <std::size_t Extent>
bool CopyTargetWide(char16_t (&destination)[Extent], const wchar_t* source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
        return false;
    }

    std::size_t output = 0;
    for (std::size_t input = 0; source[input] != L'\0'; ++input) {
        const std::uint32_t value = static_cast<std::uint32_t>(source[input]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (output + 1 >= Extent) {
                destination[0] = u'\0';
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
                return false;
            }
            destination[output++] = static_cast<char16_t>(value);
        } else if (value <= 0xffffU) {
            if (output + 1 >= Extent) {
                destination[0] = u'\0';
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
                return false;
            }
            destination[output++] = static_cast<char16_t>(value);
        } else if (value <= 0x10ffffU) {
            if (output + 2 >= Extent) {
                destination[0] = u'\0';
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
                return false;
            }
            const std::uint32_t adjusted = value - 0x10000U;
            destination[output++] = static_cast<char16_t>(0xd800U + (adjusted >> 10U));
            destination[output++] = static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU));
        } else {
            destination[0] = u'\0';
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
            return false;
        }
    }
    destination[output] = u'\0';
    return true;
}

} // namespace

CReadItemData::CReadItemData(
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
) :
    m_itemType(itemType),
    m_itemName{},
    m_defaultPrice(defaultPrice),
    m_consumeType{},
    m_canMove(canMove),
    m_isPremium(isPremium),
    m_isPeriod(isPeriod),
    m_isDuration(isDuration),
    m_isQuestItem(isQuestItem),
    m_type(type),
    m_crystalType(crystalType) {
    L2_GUARD_FUNCTION();
    CopyTargetWide(m_itemName, itemName);
    CopyTargetWide(m_consumeType, consumeType);
}
