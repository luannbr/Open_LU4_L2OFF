#include "CConfig.h"
#include "Creature.h"
#include "Fishing.h"
#include "Item.h"
#include "OlympiadStuff.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/SharedFactory.h"

#include <cstdint>

int g_nSoulshotIDTables[17][32] = {
    {34603, 33774, 21845, 5789, 1835},
    {34604, 33775, 21846, 22082, 1463},
    {34605, 33776, 22244, 21847, 22083, 1464},
    {34606, 33777, 22245, 21848, 22084, 1465},
    {34607, 33778, 22246, 21849, 22085, 1466},
    {34608, 33779, 22247, 21850, 22086, 1467},
    {46082, 23552, 23550, 22888, 22433, 34609, 33780, 17754, 1468},
    {34610, 33781, 33788, 21851, 5790, 2509, 3947},
    {34611, 33782, 33789, 21852, 22077, 22072, 2510, 3948},
    {34612, 33783, 33790, 21853, 22249, 22078, 22073, 2511, 3949},
    {34613, 33784, 33791, 21854, 22250, 22079, 22074, 2512, 3950},
    {34614, 33785, 33792, 21855, 22251, 22080, 22075, 2513, 3951},
    {34615, 33786, 33793, 21856, 22252, 22231, 22081, 22076, 2514, 3952},
    {46083, 23553, 23551, 23082, 22889, 22434, 34616, 33787, 33794, 19441, 19442, 2515, 3953},
    {35670, 20332, 6645},
    {35671, 20333, 20334, 6646, 6647},
    {38153, 38154},
};

static_assert(sizeof(g_nSoulshotIDTables) == 17U * 32U * sizeof(int));

namespace {

// Current .rdata copy at 0x00e79860.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

int ActiveStateAfterForcedStop(int activeState, bool force) {
    if (!force) {
        return activeState;
    }
    if (activeState == 1) {
        return 0;
    }
    if (activeState == 3) {
        return 2;
    }
    return activeState;
}

int FirstEnabledShotItem(std::uint32_t flags, int grade) {
    unsigned int column = 0;
    while (((flags >> (column & 31U)) & 1U) == 0U) {
        ++column;
        if (column > 31U) {
            return 0;
        }
    }
    return g_nSoulshotIDTables[grade][column];
}

void SendUseItemFailMessage(User* user, int itemClassId, bool force) {
    if (user == nullptr) {
        return;
    }
    if (user->IsOlympiadPlayer()) {
        if (!Singleton<WOlympiadDefaultSetting>::inst().IsAllowedItem(itemClassId) || force) {
            return;
        }
    }

    const int messageId = user->m_PledgeGameUser.IsPlayer() ? SystemMessageId::ITEM_AUTO_USE_DISABLED
                                                            : SystemMessageId::AUTOMATIC_ITEM_USE_DISABLED_NOT_ENOUGH_ITEM;
    user->GetSocket()->Send("chccd", 0x62, messageId, 1, 3, itemClassId);
}

} // namespace

int CCreature::CheckLastArrow(CrystalType crystalType) {
    L2_GUARD_FUNCTION();
    const int itemIndex = m_data->m_iLastArrowIndex;
    if (itemIndex == -1) {
        return -1;
    }

    CItem* const item = m_inv.GetByIndex(itemIndex);
    if (item == nullptr || item->m_const->m_EtcitemType != 2 ||
        BaseCrystal[static_cast<std::int32_t>(item->m_const->GetCrystalType())] !=
            BaseCrystal[static_cast<std::int32_t>(crystalType)]) {
        return -1;
    }
    return itemIndex;
}

int CCreature::CheckLastBolt(CrystalType crystalType) {
    L2_GUARD_FUNCTION();
    const int itemIndex = m_data->m_iLastArrowIndex;
    if (itemIndex == -1) {
        return -1;
    }

    CItem* const item = m_inv.GetByIndex(itemIndex);
    if (item == nullptr || item->m_const->m_EtcitemType != 0x1b ||
        BaseCrystal[static_cast<std::int32_t>(item->m_const->GetCrystalType())] !=
            BaseCrystal[static_cast<std::int32_t>(crystalType)]) {
        return -1;
    }
    return itemIndex;
}

void CCreature::TurnOffSoulshot(bool force) {
    L2_GUARD_FUNCTION();

    m_data->m_nSoulShot = 0;
    m_data->m_dSoulShotAttackBonusPer = 0.0;
    m_sNowSoulShot = 0;

    if (!IsPC()) {
        if (!IsSummonOrPet()) {
            return;
        }
        CSummon* const summon = CastSummon();
        User* const master = summon->GetMaster();
        if (master == nullptr || master->m_nAutoSoulshotFlags[14] == 0U) {
            return;
        }

        const int activeState = ActiveStateAfterForcedStop(
            // master->m_AutoSoulShotUi[2].m_IsActiveState,
            master->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SOULSHOT].m_IsActiveState, force
        );
        const int itemClassId = g_bUseAutoSoulShotUI
            // ? master->m_AutoSoulShotUi[2].m_ItemClassID
            ? master->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SOULSHOT].m_ItemClassID
            : FirstEnabledShotItem(master->m_nAutoSoulshotFlags[14], 14);
        if (itemClassId == 0) {
            return;
        }

        CItem* const item = master->m_inv.GetFirstItemByClassID(itemClassId, false, false);
        if (item == nullptr || force) {
            master->TurnOffAutoSoulshot(itemClassId, activeState, 2, true);
        } else if (master->UseItem(*item, 0, false, static_cast<int>(Id()))) {
            return;
        } else {
            master->TurnOffAutoSoulshot(itemClassId, activeState, 2, true);
            force = false;
        }
        SendUseItemFailMessage(master, itemClassId, force);
        return;
    }

    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return;
    }
    const int grade = weapon->GetConstant()->GetShotGrade(Shot::SoulShot);
    if (grade == Shot::Invalid) {
        return;
    }

    if (CastUser()->m_nAutoSoulshotFlags[grade] == 0U) {
        return;
    }
    const int activeState = ActiveStateAfterForcedStop(
        // CastUser()->m_AutoSoulShotUi[0].m_IsActiveState,
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState, force
    );

    if (!g_bUseAutoSoulShotUI) {
        bool used = false;
        int attempts = 0;
        while (!used && attempts < 7) {
            ++attempts;
            const int itemClassId = FirstEnabledShotItem(CastUser()->m_nAutoSoulshotFlags[grade], grade);
            if (itemClassId == 0) {
                return;
            }
            CItem* const item = CastUser()->m_inv.GetFirstItemByClassID(itemClassId, false, false);
            if (item == nullptr || force) {
                CastUser()->TurnOffAutoSoulshot(itemClassId, 0, 4, true);
                SendUseItemFailMessage(CastUser(), itemClassId, force);
            } else if (!UseItem(*item, 0, false, 0)) {
                CastUser()->TurnOffAutoSoulshot(itemClassId, 0, 4, true);
                SendUseItemFailMessage(CastUser(), itemClassId, false);
            } else {
                used = true;
            }
            if (CastUser()->m_nAutoSoulshotFlags[grade] == 0U) {
                return;
            }
        }
        return;
    }

    // if (CastUser()->m_AutoSoulShotUi[0].m_IsActiveState != 1 &&
    if (CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState != E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_O &&
        // CastUser()->m_AutoSoulShotUi[0].m_IsActiveState != 3) {
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState != E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_X) {
        return;
    }
    const int itemClassId =
        // CastUser()->m_AutoSoulShotUi[0].m_ItemClassID.
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_ItemClassID;
    if (itemClassId == 0) {
        return;
    }
    CItem* const item = CastUser()->m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr || force) {
        CastUser()->TurnOffAutoSoulshot(itemClassId, activeState, 0, true);
        SendUseItemFailMessage(CastUser(), itemClassId, force);
    } else if (UseItem(*item, 0, false, 0)) {
        return;
    } else {
        CastUser()->TurnOffAutoSoulshot(itemClassId, activeState, 0, true);
        force = false;
        SendUseItemFailMessage(CastUser(), itemClassId, force);
    }
    CastUser();
}

void CCreature::TurnOffFishingShot(const bool forceArgument) {
    // Current L2Server 0x004c6824.
    L2_GUARD_FUNCTION();
    if (!IsPC()) {
        return;
    }
    m_data->m_nFishingShot = 0;

    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return;
    }
    CSharedItemConstant* const constant = weapon->GetConstant();
    if (constant == nullptr) {
        return;
    }
    const int grade = constant->GetShotGrade(Shot::FishingShot);
    if (grade == Shot::Invalid) {
        return;
    }

    if (CastUser()->m_nAutoSoulshotFlags[grade] == 0U) {
        return;
    }
    bool force = forceArgument;
    const int activeState = ActiveStateAfterForcedStop(
        // CastUser()->m_AutoSoulShotUi[0].m_IsActiveState,
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState, force
    );

    if (!g_bUseAutoSoulShotUI) {
        bool used = false;
        int attempts = 0;
        while (!used && attempts < 7) {
            ++attempts;
            const int itemClassId = FirstEnabledShotItem(CastUser()->m_nAutoSoulshotFlags[grade], grade);
            if (itemClassId == 0) {
                return;
            }
            CItem* const item = CastUser()->m_inv.GetFirstItemByClassID(itemClassId, false, false);
            if (item == nullptr || force) {
                CastUser()->TurnOffAutoSoulshot(itemClassId, 0, 4, true);
                SendUseItemFailMessage(CastUser(), itemClassId, force);
            } else if (!CastUser()->UseItem(*item, 0, false, 0)) {
                CastUser()->TurnOffAutoSoulshot(itemClassId, 0, 4, true);
                SendUseItemFailMessage(CastUser(), itemClassId, false);
            } else {
                used = true;
            }
            if (!used) {
                User* const failedUser = CastUser();
                if (failedUser->m_pFishingController != nullptr) {
                    failedUser->m_pFishingController->m_dFishingShotBonus = 0.0;
                }
            }
            if (CastUser()->m_nAutoSoulshotFlags[grade] == 0U) {
                return;
            }
        }
        return;
    }

    // if (CastUser()->m_AutoSoulShotUi[0].m_IsActiveState != 1 &&
    if (CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState != E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_O &&
        // CastUser()->m_AutoSoulShotUi[0].m_IsActiveState != 3) {
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState != E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_X) {
        return;
    }
    const int itemClassId =
        // CastUser()->m_AutoSoulShotUi[0].m_ItemClassID.
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_ItemClassID;
    if (itemClassId == 0) {
        return;
    }
    CItem* const item = CastUser()->m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr || force) {
        CastUser()->TurnOffAutoSoulshot(itemClassId, activeState, 0, true);
        SendUseItemFailMessage(CastUser(), itemClassId, force);
    } else if (CastUser()->UseItem(*item, 0, false, 0)) {
        return;
    } else {
        CastUser()->TurnOffAutoSoulshot(itemClassId, activeState, 0, true);
        force = false;
        SendUseItemFailMessage(CastUser(), itemClassId, force);
    }
    User* const failedUser = CastUser();
    if (failedUser->m_pFishingController != nullptr) {
        failedUser->m_pFishingController->m_dFishingShotBonus = 0.0;
    }
    CastUser();
}

void CCreature::TurnOffSpiritshot(bool force) {
    L2_GUARD_FUNCTION();

    m_data->m_bSpiritShot = false;
    m_data->m_dSpiritShotAttackBonusPer = 0.0;
    m_data->m_dSpiritShotSpeedBonusPer = 0.0;
    m_dSpiritShotHealBonus = 0.0;

    if (!IsPC()) {
        if (!IsSummonOrPet()) {
            return;
        }
        CSummon* const summon = CastSummon();
        User* const master = summon->GetMaster();
        if (master == nullptr || master->m_nAutoSoulshotFlags[15] == 0U) {
            return;
        }

        const int activeState = ActiveStateAfterForcedStop(
            // master->m_AutoSoulShotUi[3].m_IsActiveState,
            master->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SPIRITSHOT].m_IsActiveState, force
        );
        const int itemClassId = g_bUseAutoSoulShotUI
            // ? master->m_AutoSoulShotUi[3].m_ItemClassID
            ? master->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SPIRITSHOT].m_ItemClassID
            : FirstEnabledShotItem(master->m_nAutoSoulshotFlags[15], 15);
        if (itemClassId == 0) {
            return;
        }

        CItem* const item = master->m_inv.GetFirstItemByClassID(itemClassId, false, false);
        if (item == nullptr || force) {
            master->TurnOffAutoSoulshot(itemClassId, activeState, 3, true);
        } else if (master->UseItem(*item, 0, false, static_cast<int>(Id()))) {
            return;
        } else {
            master->TurnOffAutoSoulshot(itemClassId, activeState, 3, true);
            force = false;
        }
        SendUseItemFailMessage(master, itemClassId, force);
        return;
    }

    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return;
    }
    const int grade = weapon->GetConstant()->GetShotGrade(Shot::SpiritShot);
    if (grade == Shot::Invalid) {
        return;
    }

    if (CastUser()->m_nAutoSoulshotFlags[grade] == 0U) {
        return;
    }
    const int activeState = ActiveStateAfterForcedStop(
        // CastUser()->m_AutoSoulShotUi[1].m_IsActiveState,
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SPIRITSHOT].m_IsActiveState, force
    );

    if (!g_bUseAutoSoulShotUI) {
        bool used = false;
        int attempts = 0;
        while (!used && attempts < 7) {
            ++attempts;
            const int itemClassId = FirstEnabledShotItem(CastUser()->m_nAutoSoulshotFlags[grade], grade);
            if (itemClassId == 0) {
                return;
            }
            CItem* const item = CastUser()->m_inv.GetFirstItemByClassID(itemClassId, false, false);
            if (item == nullptr || force) {
                CastUser()->TurnOffAutoSoulshot(itemClassId, 0, 4, true);
                SendUseItemFailMessage(CastUser(), itemClassId, force);
            } else if (!UseItem(*item, 0, false, 0)) {
                CastUser()->TurnOffAutoSoulshot(itemClassId, 0, 4, true);
                SendUseItemFailMessage(CastUser(), itemClassId, false);
            } else {
                used = true;
            }
            if (CastUser()->m_nAutoSoulshotFlags[grade] == 0U) {
                return;
            }
        }
        return;
    }

    // if (CastUser()->m_AutoSoulShotUi[1].m_IsActiveState != 1 &&
    if (CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SPIRITSHOT].m_IsActiveState !=
            E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_O &&
        // CastUser()->m_AutoSoulShotUi[1].m_IsActiveState != 3) {
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SPIRITSHOT].m_IsActiveState !=
            E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_X) {
        return;
    }
    const int itemClassId =
        // CastUser()->m_AutoSoulShotUi[1].m_ItemClassID.
        CastUser()->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SPIRITSHOT].m_ItemClassID;
    if (itemClassId == 0) {
        return;
    }
    CItem* const item = CastUser()->m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr || force) {
        CastUser()->TurnOffAutoSoulshot(itemClassId, activeState, 1, true);
        SendUseItemFailMessage(CastUser(), itemClassId, force);
    } else if (UseItem(*item, 0, false, 0)) {
        return;
    } else {
        CastUser()->TurnOffAutoSoulshot(itemClassId, activeState, 1, true);
        force = false;
        SendUseItemFailMessage(CastUser(), itemClassId, force);
    }
    CastUser();
}
