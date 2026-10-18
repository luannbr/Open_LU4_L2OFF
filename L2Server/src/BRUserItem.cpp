#include "User.h"

#include "Item.h"

#include "Shared/src/SharedFactory.h"
#include "Shared/src/SlotType.h"

#include <cstdint>

namespace {

// Current br_user_item.cpp copy at L2Server .rdata 0x00e7fb90.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

} // namespace

void User::AutoEquipArrowOrBolt(CItem* item) {
    // Current L2Server 0x004d28e4, original owner br_user_item.cpp.
    L2_GUARD_FUNCTION();

    CSharedItemData* const weapon = GetEquipedWeapon();
    if (item == nullptr || weapon == nullptr) {
        return;
    }

    if (item->m_const->m_EtcitemType == EIT_ARROW) {
        if (weapon->GetConstant()->m_AttackType != AT_BOW) {
            return;
        }
    } else if (item->m_const->m_EtcitemType == EIT_BOLT) {
        if (weapon->GetConstant()->m_AttackType != AT_CROSSBOW &&
            weapon->GetConstant()->m_AttackType != AT_TWOHANDCROSSBOW) {
            return;
        }
    } else {
        return;
    }

    const int weaponCrystal = static_cast<int>(weapon->GetConstant()->GetCrystalType());
    const int itemCrystal = static_cast<int>(item->m_const->GetCrystalType());
    if (BaseCrystal[weaponCrystal] == BaseCrystal[itemCrystal] && m_data->m_nSlotIndex[ST_LHAND] == -1) {
        m_data->m_nSlotIndex[ST_LHAND] = item->m_data->index;
        m_data->m_iLastArrowIndex = m_data->m_nSlotIndex[ST_LHAND];
        ValidateChanged(std::int64_t{VB_ITEMSKILL});
    }
}
