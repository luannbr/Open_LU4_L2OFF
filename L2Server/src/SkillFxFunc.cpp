#include "SkillFxFunc.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "AirShip.h"
#include "Area.h"
#include "CFortressFlagpole.h"
#include "CWorld.h"
#include "Castle.h"
#include "Collection.h"
#include "Creature.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "Dominion.h"
#include "Door.h"
#include "EventCampaign.h"
#include "Fishing.h"
#include "Fortress.h"
#include "HolyThing.h"
#include "InstantAgit.h"
#include "Inventory.h"
#include "Item.h"
#include "MiniGameAgit.h"
#include "NPC.h"
#include "NewAction.h"
#include "ObjectDB.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Protocol/src/NPCProtocol.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "SSQ.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/platform/Runtime.h"
#include "SharedContainer.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "Transform.h"
#include "User.h"
#include "UserBookmark.h"
#include "UserSocket.h"

namespace {

// Current skillfxfunc.obj .rdata at 0x0118d8d0. The original header emits
// one translation-unit-local copy for every consumer.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

void AddSkillModBonus(CCreature* creature, CCreatureSkillMod::ModName modName, SkillFxChangeType changeType, double bonus) {
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[static_cast<int>(modName)];
    if (changeType == SFCT_PER) {
        mod.m_Per *= (bonus + 100.0) / 100.0;
    } else if (changeType == SFCT_DIFF) {
        mod.m_Diff += bonus;
    }
}

void* DoubleBitsAsPointer(const double value) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(bits));
}


std::int32_t WrappingCubeQuarter(const std::int32_t value) noexcept {
    const std::int32_t square = runtime::MultiplyInt32Wrapping(value, value);
    return runtime::MultiplyInt32Wrapping(square, value) / 4;
}

int StaticObjectEditorId(CObject& object) {
    CStaticObject* const staticObject = object.CastStaticObject();
    return staticObject != nullptr && staticObject->m_data != nullptr ? staticObject->m_data->m_nEditorID : 0;
}

void SendHalfResistMessageToAttacker(CCreature& attacker, CObject& target) {
    User* const receiver = attacker.GetUserOrMaster();
    if (receiver == nullptr || receiver->GetSocket() == nullptr) {
        return;
    }

    CUserSocket* const socket = receiver->GetSocket();
    const int targetId = static_cast<int>(target.Id());
    const int attackerId = static_cast<int>(attacker.Id());
    const bool attackerIsPc = attacker.IsPC();
    const char16_t* const attackerName = attackerIsPc ? attacker.CastUser()->GetName2(nullptr) : nullptr;

    if (target.IsPC()) {
        User* const targetUser = target.CastUser();
        const char16_t* const targetName = targetUser->GetName2(receiver);
        if (attackerIsPc) {
            socket->Send(
                "chccScScdcd", 0x62, SystemMessageId::MAGIC_DAMAGE_REDUCED_BY_TARGET_RESISTANCE, 4, 0x0c, targetName, 0x0c,
                attackerName, 1, targetId, 1, attackerId
            );
        } else {
            socket->Send(
                "chccScdcdcd", 0x62, SystemMessageId::MAGIC_DAMAGE_REDUCED_BY_TARGET_RESISTANCE, 4, 0x0c, targetName, 2,
                attacker.GetClassId(), 1, targetId, 1, attackerId
            );
        }
        return;
    }

    int targetType = 0;
    int targetValue = 0;
    if (target.IsNPC()) {
        targetType = 2;
        targetValue = target.GetClassId();
    } else if (target.IsStaticObject()) {
        targetType = 0x0b;
        targetValue = StaticObjectEditorId(target);
    } else {
        return;
    }

    if (attackerIsPc) {
        socket->Send(
            "chccdcScdcd", 0x62, SystemMessageId::MAGIC_DAMAGE_REDUCED_BY_TARGET_RESISTANCE, 4, targetType, targetValue,
            0x0c, attackerName, 1, targetId, 1, attackerId
        );
    } else {
        socket->Send(
            "chccdcdcdcd", 0x62, SystemMessageId::MAGIC_DAMAGE_REDUCED_BY_TARGET_RESISTANCE, 4, targetType, targetValue, 2,
            attacker.GetClassId(), 1, targetId, 1, attackerId
        );
    }
}

void SendResistMessageToTarget(CCreature& attacker, CCreature& target, const unsigned int messageId) {
    User* const receiver = target.GetUserOrMaster();
    if (receiver == nullptr || receiver->GetSocket() == nullptr) {
        return;
    }

    CUserSocket* const socket = receiver->GetSocket();
    const int targetId = static_cast<int>(target.Id());
    const int attackerId = static_cast<int>(attacker.Id());
    const bool attackerIsPc = attacker.IsPC();
    const bool targetIsPc = target.IsPC();

    if (targetIsPc) {
        const char16_t* const targetName = target.CastUser()->GetName2(nullptr);
        if (attackerIsPc) {
            socket->Send(
                "chccScScdcd", 0x62, messageId, 4, 0x0c, targetName, 0x0c, attacker.CastUser()->GetName2(receiver), 1,
                targetId, 1, attackerId
            );
        } else if (attacker.IsNPC()) {
            socket->Send(
                "chccScdcdcd", 0x62, messageId, 4, 0x0c, targetName, 2, attacker.GetClassId(), 1, targetId, 1, attackerId
            );
        }
        return;
    }

    if (attackerIsPc) {
        socket->Send(
            "chccdcScdcd", 0x62, messageId, 4, 2, target.GetClassId(), 0x0c, attacker.CastUser()->GetName2(receiver), 1,
            targetId, 1, attackerId
        );
    } else if (attacker.IsNPC()) {
        socket->Send(
            "chccdcdcdcd", 0x62, messageId, 4, 2, target.GetClassId(), 2, attacker.GetClassId(), 1, targetId, 1, attackerId
        );
    }
}

} // namespace

CSkillOperateCondition::~CSkillOperateCondition() = default;

bool CSkillOperateCondition::CanUse(CCreature*, const CSkillInfo*, CObject*) {
    // Current L2Server ICF body 0x004c0008.
    return false;
}

bool CSkillOperateCondition_account_type::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b8ec.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || m_UserAccountType != UAT_PCCAFE) {
        return false;
    }
    return creature->CastUser()->m_data->m_bPCCafeUser;
}

bool CSkillOperateCondition_check_level::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a78b34.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsCreature()) {
        return false;
    }
    CCreature* checked = L2SkillFunc::GetCheckedCreature(creature, target, m_TargetType);
    return checked != nullptr && m_nLowLevel <= checked->m_data->m_nLevel && checked->m_data->m_nLevel <= m_nHighLevel;
}

bool CSkillOperateCondition_check_sex::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a78a60.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->m_data->m_Sex == m_nSex;
}

bool CSkillOperateCondition_energy_saved::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a781b0.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && m_nEnergyCount <= creature->m_nEnergyCount;
}

bool CSkillOperateCondition_soul_saved::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7826c.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && m_nSoulCount <= creature->CastUser()->m_SoulCount;
}

bool CSkillOperateCondition_mainjob::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b824.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->m_data->m_nSubJobId == 0;
}

bool CSkillOperateCondition_subjob::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b9bc.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->m_data->m_nSubJobId != 0;
}

bool CSkillOperateCondition_not_in_underwater::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a76a04.
    L2_GUARD_FUNCTION();
    return creature->IsCreature() && !creature->m_AreaMod.m_bIsUnderWater;
}

bool CSkillOperateCondition_op_2h_weapon::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a76eb4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || creature->m_data->m_nSlotIndex[ST_RLHAND] == -1) {
        return false;
    }
    return bWeaponTypeArray[static_cast<int>(creature->GetAttackType())];
}

CSkillOperateCondition_op_target_weapon_attack_type::CSkillOperateCondition_op_target_weapon_attack_type() :
    bWeaponTypeArray{}, bWeaponSlotTypeArray{}, nMagicWeaponCheckType(2) {
    // Current L2Server 0x00a7ba88.
    L2_GUARD_FUNCTION();
}

bool CSkillOperateCondition_op_target_weapon_attack_type::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7bb44.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsWeapon()) {
        return false;
    }

    CItem* const item = target->CastItem();
    if (item == nullptr || !bWeaponTypeArray[static_cast<int>(item->m_const->m_AttackType)]) {
        return false;
    }

    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
        ++slot;
    }

    // The current body reads byte +0x60 when no slot bit matches. That byte
    // is also the low byte of nMagicWeaponCheckType. Express the overlap
    // without a one-past-array access on the host.
    const bool slotAllowed =
        slot < ST_MAX ? bWeaponSlotTypeArray[slot] : (static_cast<unsigned int>(nMagicWeaponCheckType) & 0xffU) != 0;
    if (!slotAllowed) {
        return false;
    }

    if (nMagicWeaponCheckType == 2) {
        return true;
    }
    return nMagicWeaponCheckType == static_cast<int>(item->m_const->m_bIsMagicWeapon);
}

CSkillOperateCondition_op_target_armor_type::CSkillOperateCondition_op_target_armor_type() :
    bArmorTypeArray{}, bArmorSlotTypeArray{} {
    // Current L2Server 0x00a7be84.
    L2_GUARD_FUNCTION();
}

bool CSkillOperateCondition_op_target_armor_type::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7bf30.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsArmor()) {
        return false;
    }

    CItem* const item = target->CastItem();
    if (item == nullptr || !bArmorTypeArray[static_cast<int>(item->m_const->m_ArmorType)]) {
        return false;
    }

    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
        ++slot;
    }
    return slot < ST_MAX && bArmorSlotTypeArray[slot];
}

CSkillOperateCondition_op_target_all_item_type::CSkillOperateCondition_op_target_all_item_type() :
    bWeaponTypeArray{}, bArmorTypeArray{}, bSlotTypeArray{} {
    // Current L2Server 0x00a7c040.
    L2_GUARD_FUNCTION();
}

bool CSkillOperateCondition_op_target_all_item_type::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7c0fc.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }

    CItem* item = nullptr;
    if (target->IsWeapon()) {
        item = target->CastItem();
        if (item == nullptr || !bWeaponTypeArray[static_cast<int>(item->m_const->m_AttackType)]) {
            return false;
        }
    } else if (target->IsArmor()) {
        item = target->CastItem();
        if (item == nullptr || !bArmorTypeArray[static_cast<int>(item->m_const->m_ArmorType)]) {
            return false;
        }
    } else {
        return false;
    }

    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
        ++slot;
    }
    return slot < ST_MAX && bSlotTypeArray[slot];
}

bool CSkillOperateCondition_op_alignment::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7936c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    CCreature* checked = L2SkillFunc::GetCheckedCreature(creature, target, m_TargetType);
    if (checked == nullptr) {
        return false;
    }
    return m_nIsChao != 0 ? checked->m_data->m_nKarma < 0 : checked->m_data->m_nKarma >= 0;
}

bool CSkillOperateCondition_op_check_flag::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79a40.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsFortressFlagpole()) {
        return false;
    }
    CFortressFlagpole* flag = target->CastFortressFlagpole();
    return flag != nullptr && !flag->IsFlagSet();
}

bool CSkillOperateCondition_op_check_nobless::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7c710.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    User* user = creature->CastUser();
    return user != nullptr && static_cast<unsigned>(user->m_data->m_nNoblessType - 1) < 2;
}

bool CSkillOperateCondition_op_pkcount::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7947c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    CCreature* checked = L2SkillFunc::GetCheckedCreature(creature, target, m_TargetType);
    return checked != nullptr && checked->m_data->m_nPKCount > 0;
}

bool CSkillOperateCondition_race::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a77260.
    L2_GUARD_FUNCTION();
    return target->IsCreature() && target->CastCreature()->m_data->m_Race == m_RaceType;
}

bool CSkillOperateCondition_equip_shield::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a8001c.
    L2_GUARD_FUNCTION();
    CSharedItemData* item = CItemFactory::Inst().GetData(creature->m_data->m_nSlotIndex[ST_LHAND]);
    return item != nullptr && item->m_Type == IT_ARMOR;
}

CSkillOperateCondition_equip_weapon::CSkillOperateCondition_equip_weapon() : bWeaponTypeArray{} {
    // Current L2Server 0x00a76c0c.
    L2_GUARD_FUNCTION();
}

bool CSkillOperateCondition_equip_weapon::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a76cb4.
    L2_GUARD_FUNCTION();
    return bWeaponTypeArray[static_cast<int>(creature->GetRealAttackType())];
}

CSkillOperateCondition_equip_armor::CSkillOperateCondition_equip_armor() : bArmorTypeArray{} {
    // Current L2Server 0x00a76d64.
    L2_GUARD_FUNCTION();
}

bool CSkillOperateCondition_equip_armor::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a76e04.
    L2_GUARD_FUNCTION();
    return bArmorTypeArray[static_cast<int>(creature->GetArmorType())];
}

bool CSkillOperateCondition_remain_hp_per::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a775f0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    CSharedCreatureData* data = nullptr;
    if (m_TargetType == CSkillInfo::OCT_TARGET || m_TargetType == CSkillInfo::OCT_BOTH) {
        if (L2Common::IsAliveCreature(target) && target->IsCreature()) {
            CCreature* const targetCreature = target->CastCreature();
            if (targetCreature != nullptr) {
                data = targetCreature->m_data;
            }
        }
    } else if (creature->IsCreature()) {
        data = creature->m_data;
    }

    if (data == nullptr) {
        return false;
    }
    return m_Lower == (data->m_dHP <= (static_cast<double>(m_RemainHpPer) * data->m_dBaseHP) / 100.0);
}

bool CSkillOperateCondition_remain_mp_per::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a77788.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsCreature()) {
        return false;
    }
    return m_Lower ==
        (creature->m_data->m_dMP <= (static_cast<double>(m_RemainMpPer) * creature->m_data->m_dBaseMP) / 100.0);
}

bool CSkillOperateCondition_remain_cp_per::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a77890.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    return m_Lower ==
        (creature->m_data->m_dCP <= (static_cast<double>(m_RemainCpPer) * creature->m_data->m_dBaseCP) / 100.0);
}

bool CSkillOperateCondition_op_can_not_use_airship::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7ae50.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    const bool canUse = !creature->IsInAirShip();
    if (!canUse) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_ABOARD);
    }
    return canUse;
}

bool CSkillOperateCondition_op_cannot_use_target_with_private_store::CanUse(
    CCreature*, const CSkillInfo*, CWorldObject* target
) {
    // Current L2Server 0x00a7c538.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return true;
    }
    User* const user = target->CastUser();
    return user->m_data->m_StopMode != STP_STORE;
}

bool CSkillOperateCondition_op_check_honors::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7c7e4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    User* const user = creature->CastUser();
    return user != nullptr && user->m_data->m_nNoblessType == 2;
}

bool CSkillOperateCondition_op_check_not_registered_pledgegame::CanUse(
    CCreature* creature, const CSkillInfo*, CWorldObject*
) {
    // Current L2Server 0x00a7c8b0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    const bool canUse = !g_PledgeGameSystem.IsRegisteredUser(user);
    if (!canUse) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_SUBCLASS_WHILE_REGISTERED_FOR_CEREMONY_OF_CHAOS);
    }
    return canUse;
}

bool CSkillOperateCondition_op_companion::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a770c8.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }
    if (type == 0) {
        return target->IsSummonOrPet();
    }
    if (type == 1) {
        return target->IsPet();
    }
    if (type == 2) {
        return target->IsSummonOrPet() && !target->IsPet();
    }
    if (type == 3) {
        if (!target->IsSummon()) {
            return false;
        }
        if (target->CastSummon()->GetMaster() == nullptr) {
            return false;
        }
        return target->CastSummon()->GetMaster() == creature;
    }
    if (type == 4) {
        return target->IsPC();
    }
    return false;
}

bool CSkillOperateCondition_op_energy_max::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a78c3c.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }
    CCreature* const creature = target->CastCreature();
    if (creature->m_nEnergyCount < m_nMaxEnergy) {
        return true;
    }
    creature->SendSystemMessage(SystemMessageId::FORCE_AT_MAXIMUM);
    return false;
}

bool CSkillOperateCondition_op_have_summon::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7a258.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }
    if (target->CastUser()->HaveSummon()) {
        return true;
    }
    creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_NO_SERVITOR_SUMMONED);
    return false;
}

bool CSkillOperateCondition_op_have_summoned_npc::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7e0c8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_szNPCName.c_str(), false);
    if (npcClassId == -1) {
        const std::string hostName = Utf16ToUtf8(m_szNPCName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined class in can_summon_multi [%s]",
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"Undefined class in can_summon_multi [%s]",
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot evaluate op_have_summoned_npc: NPC name '%s' resolves to class ID -1; the condition fails.",
            hostName.c_str()
        );
        return false;
    }

    return creature->CastUser()->HaveSummonedNpc(npcClassId);
}

bool CSkillOperateCondition_op_have_not_summoned_npc::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7e1f8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_szNPCName.c_str(), false);
    if (npcClassId == -1) {
        const std::string hostName = Utf16ToUtf8(m_szNPCName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined class in can_summon_multi [%s]",
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"Undefined class in can_summon_multi [%s]",
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot evaluate op_have_not_summoned_npc: NPC name '%s' resolves to class ID -1; the condition fails.",
            hostName.c_str()
        );
        return false;
    }

    return !creature->CastUser()->HaveSummonedNpc(npcClassId);
}

bool CSkillOperateCondition_op_need_peacezone::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b778.
    L2_GUARD_FUNCTION();
    return AreaBasedRule::CancelPost(creature);
}

bool CSkillOperateCondition_op_not_olympiad::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7c344.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && !creature->CastUser()->IsOlympiadPlaying();
}

bool CSkillOperateCondition_op_check_cast_range::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a76314.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsWorldObject()) {
        return false;
    }
    return m_CastRange * m_CastRange <= creature->GetPos().DistCube(target->GetPos());
}

bool CSkillOperateCondition_target_my_party::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a77980.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return target->IsSummonOrPet() && !target->IsPet();
    }
    if (!target->IsCreature() || (!m_IncludeMe && target == creature)) {
        return false;
    }
    return creature->CastUser()->IsMyParty(target->CastCreature());
}

bool CSkillOperateCondition_target_my_pledge::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a77ab0.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || (!target->IsPC() && !target->IsSummonOrPet())) {
        return false;
    }

    User* const targetUser = target->GetUserOrMaster();
    User* const actorUser = creature->CastUser();
    if (targetUser == nullptr || actorUser == nullptr || (!m_IncludeMe && target == creature)) {
        return false;
    }

    if (actorUser->m_data->m_nId == targetUser->m_data->m_nId) {
        return true;
    }
    const std::uint32_t pledgeId = actorUser->m_data->m_nPledgeID;
    return pledgeId != 0 && pledgeId == targetUser->m_data->m_nPledgeID;
}

bool CSkillOperateCondition_target_my_mentor::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a77d20.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && target->IsPC() && creature->CastUser()->IsMyMentor(target->CastUser());
}

bool CSkillOperateCondition_target_my_mentee::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a77e18.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && target->IsPC() && creature->CastUser()->IsMyMentee(target->CastUser());
}

bool CSkillOperateCondition_op_target_my_pledge_academy::CanUse(
    CCreature* creature, const CSkillInfo*, CWorldObject* target
) {
    // Current L2Server 0x00a77c00.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsPC()) {
        return false;
    }

    User* const targetUser = target->CastUser();
    User* const actorUser = creature->CastUser();
    const std::uint32_t pledgeId = actorUser->m_data->m_nPledgeID;
    return pledgeId != 0 && pledgeId == targetUser->m_data->m_nPledgeID && target->CastUser()->m_data->m_nPledgeType == -1;
}

bool CSkillOperateCondition_op_in_siege_time::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a77f10.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceId);
    return residence != nullptr && residence->IsUnderSiege();
}

bool CSkillOperateCondition_op_soul_max::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a78d18.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user != nullptr && user->m_SoulCount < user->m_SkillMod.m_SoulMaxCount) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::CANNOT_ABSORB_MORE_SOULS);
    return false;
}

bool CSkillOperateCondition_op_wyvern::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a78dfc.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsCreature()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user->IsRidingWyvern() && target->IsPC();
}

bool CSkillOperateCondition_op_not_instantzone::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7c27c.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->IsInRealWorld();
}

bool CSkillOperateCondition_op_target_npc::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7dae8.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsNPC()) {
        return false;
    }
    const int npcId = target->CastNPC()->GetNPCId();
    return std::find(m_vTargetNPC.begin(), m_vTargetNPC.end(), npcId) != m_vTargetNPC.end();
}

bool CSkillOperateCondition_op_social_class::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7bdb8.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && m_nSocialClass <= creature->CastUser()->GetSocialClass();
}

bool CSkillOperateCondition_op_check_relative_height::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7c40c.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || target == nullptr || !target->IsCreature()) {
        return false;
    }

    User* const user = creature->CastUser();
    const int heightDifference = static_cast<int>(target->GetPos().z) - static_cast<int>(user->GetPos().z);
    return -m_nHeight <= heightDifference && heightDifference <= m_nHeight;
}

bool CSkillOperateCondition_op_not_cursed::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a83b20.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsPC()) {
        User* const targetUser = targetCreature->CastUser();
        static_cast<void>(CCursedWeaponMgr::inst());
        if (targetUser != nullptr && targetUser->m_nCursedWeaponClassId > 0) {
            return false;
        }
    }
    return true;
}

bool CSkillOperateCondition_possess_holything::CanUse(
    CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target
) {
    // Current L2Server 0x00a77330.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !target->IsHolyThing() || !creature->IsPC()) {
        return false;
    }
    return creature->CastUser()->CanPossessHolyThing(target->CastHolyThing(), skillInfo);
}

bool CSkillOperateCondition_can_untransform::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7668c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const user = target->CastUser();
    if (user == nullptr || user->IsTransformed(TransformType::Max) == nullptr) {
        return false;
    }

    return user->IsTransformed(TransformType::Flying) == nullptr || L2SkillFunc::CanFlyingTransform(user);
}

bool CSkillOperateCondition_can_use_in_transform::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a76788.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    return m_transformID == 0 ? target->CastCreature()->m_data->m_transformID > 0
                              : m_transformID == target->CastCreature()->m_data->m_transformID;
}

bool CSkillOperateCondition_cannot_use_in_transform::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a768a0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const user = target->CastCreature()->CastUser();
    if (user == nullptr) {
        return false;
    }

    if ((m_transformID != 0 || target->CastCreature()->m_data->m_transformID < 1) &&
        m_transformID != target->CastCreature()->m_data->m_transformID) {
        return true;
    }

    user->SendSystemMessage(SystemMessageId::EFFECT_NOT_APPLICABLE_TO_CURRENT_TRANSFORMATION);
    return false;
}

bool CSkillOperateCondition_op_fishing_cast::CanUse(CCreature*, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7a670.
    L2_GUARD_FUNCTION();
    return false;
}

bool CSkillOperateCondition_op_fishing_pumping::CanUse(CCreature*, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7a6f0.
    L2_GUARD_FUNCTION();
    return false;
}

bool CSkillOperateCondition_op_fishing_reeling::CanUse(CCreature*, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7a770.
    L2_GUARD_FUNCTION();
    return false;
}

bool CSkillOperateCondition_restart_point::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b30c.
    L2_GUARD_FUNCTION();
    if (creature->IsPC() && creature->CastUser()->m_RestartPoint == m_nRestartPoint) {
        creature->CastUser()->m_RestartPoint = RESTART_VILLAGE;
        return true;
    }
    return false;
}

bool CSkillOperateCondition_agathion_energy::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b3e8.
    L2_GUARD_FUNCTION();
    CItem* const item = creature->m_inv.GetByIndex(creature->m_data->m_nSlotIndex[ST_LBRACELET]);
    return item != nullptr && item->m_const->m_dAgathionMaxEnergy > 0.0 &&
        static_cast<double>(m_nEnergy) <= item->GetAgathionEnergy();
}

bool CSkillOperateCondition_can_stop_consume_vital_point::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b4d8.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC() && creature->CastUser()->CanSuspendConsumingVitalPoint();
}

bool CSkillOperateCondition_can_use_in_dragon_lair::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b684.
    L2_GUARD_FUNCTION();
    User* const user = creature->GetUserOrMaster();
    if (user == nullptr) {
        // Log.Add(LOG_ERROR, L"[%s][%d] SkillUser is not PC/Pet/Summon.", L"SkillFxFunc.cpp", 0x1821)
        // L2_LOG_ERROR(Log, L"[%s][%d] SkillUser is not PC/Pet/Summon.", L"SkillFxFunc.cpp", 0x1821)
        L2_LOG_ERROR(
            Log, "Cannot evaluate can_use_in_dragon_lair: the caster has no available player or owner; the condition fails."
        );
        return false;
    }
    return user->IsInRealWorld() && user->m_AreaMod.m_bIsInDragonLair;
}

bool CSkillOperateCondition_op_pledge::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7bcc4.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (m_nPledgeLevel == 0) {
        return user->m_data->m_nPledgeID == 0;
    }

    CPledge* const pledge = user->GetPledge();
    return pledge != nullptr && static_cast<unsigned int>(m_nPledgeLevel) <= pledge->GetPledgeLevel();
}

bool CSkillOperateCondition_op_check_pcbang_point::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a8a0b0.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsPC()) {
        return false;
    }

    const std::uint32_t totalBits =
        static_cast<std::uint32_t>(creature->CastUser()->m_data->m_nPCCafePoint) + static_cast<std::uint32_t>(nAmount);
    return static_cast<std::int32_t>(totalBits) < 200001;
}

bool CSkillOperateCondition_op_check_crt_effect::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a8a1b0.
    L2_GUARD_FUNCTION();
    return creature != nullptr && target->IsCreature() && target->CastCreature()->m_data->m_nExCrtEffect == 0;
}

bool CSkillOperateCondition_op_check_skill::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79710.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsCreature()) {
        return false;
    }

    CCreature* const checked = L2SkillFunc::GetCheckedCreature(creature, target, m_TargetType);
    return checked != nullptr && checked->GetAcquiredSkillLevel(m_SkillID) > 0;
}

CSkillOperateCondition_op_call_pc::CSkillOperateCondition_op_call_pc(bool includeDead, CSkillInfo* skillInfo) :
    m_bIncludeDead(includeDead) {
    // Current L2Server 0x00a7a42c.
    L2_GUARD_FUNCTION();
    if (skillInfo != nullptr) {
        skillInfo->m_bCondIncludeDead = includeDead;
    }
}

bool CSkillOperateCondition_op_call_pc::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7e328.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsPC()) {
        return false;
    }

    User* const caster = creature->CastUser();
    User* const called = target->CastUser();
    User::CallPCResult result = User::CPR_ETC;
    if (User::IsCallPCEnable(caster, called, m_bIncludeDead, result)) {
        return true;
    }

    const FVector position = caster->GetPos();
    const std::wstring casterAccount = Utf16ToWide(caster->m_sAccountName.data());
    const std::wstring calledAccount = Utf16ToWide(called->m_sAccountName.data());
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
    //     L"%s,%s,%s,%s,",
    //     static_cast<std::int64_t>(0x373),
    //     static_cast<std::int64_t>(caster->GetDBID()),
    //     static_cast<std::int64_t>(caster->GetAccountID()),
    //     static_cast<std::int64_t>(called->GetDBID()),
    //     static_cast<std::int64_t>(called->GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(caster->m_data->m_Race),
    //     static_cast<std::int64_t>(caster->m_data->m_Sex),
    //     static_cast<std::int64_t>(caster->m_data->m_nClass),
    //     static_cast<std::int64_t>(caster->m_data->m_nLevel),
    //     static_cast<std::int64_t>(called->m_data->m_Race),
    //     static_cast<std::int64_t>(called->m_data->m_Sex),
    //     static_cast<std::int64_t>(called->m_data->m_nClass),
    //     static_cast<std::int64_t>(called->m_data->m_nLevel),
    //     static_cast<std::int64_t>(result),
    //     caster->GetName(),
    //     casterAccount.c_str(),
    //     called->GetName(),
    //     calledAccount.c_str())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
        u"%s,%s,%s,%s,",
        static_cast<std::int64_t>(0x373), static_cast<std::int64_t>(caster->GetDBID()),
        static_cast<std::int64_t>(caster->GetAccountID()), static_cast<std::int64_t>(called->GetDBID()),
        static_cast<std::int64_t>(called->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(caster->m_data->m_Race), static_cast<std::int64_t>(caster->m_data->m_Sex),
        static_cast<std::int64_t>(caster->m_data->m_nClass), static_cast<std::int64_t>(caster->m_data->m_nLevel),
        static_cast<std::int64_t>(called->m_data->m_Race), static_cast<std::int64_t>(called->m_data->m_Sex),
        static_cast<std::int64_t>(called->m_data->m_nClass), static_cast<std::int64_t>(called->m_data->m_nLevel),
        static_cast<std::int64_t>(result), caster->GetName(), casterAccount.c_str(), called->GetName(), calledAccount.c_str()
    );
    return false;
}

CSkillOperateCondition_op_check_class_list::CSkillOperateCondition_op_check_class_list(
    const std::vector<std::int64_t>* classIds, SkillTargetTypeEnum targetType, bool exist
) :
    m_TargetType(targetType), m_bExist(exist) {
    // Current L2Server 0x00a83168.
    for (const std::int64_t classId : *classIds) {
        m_ClassIds.push_back(static_cast<ClassType>(runtime::Int32FromBits(classId)));
    }
}

bool CSkillOperateCondition_op_check_class_list::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a80354.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || target == nullptr) {
        return false;
    }

    CCreature* const checked = L2SkillFunc::GetCheckedCreature(creature, target, m_TargetType);
    if (checked == nullptr) {
        return false;
    }
    if (checked->IsNPC()) {
        return true;
    }

    const bool found = std::find(m_ClassIds.begin(), m_ClassIds.end(), checked->m_data->m_nClass) != m_ClassIds.end();
    return m_bExist == found;
}

CSkillOperateCondition_op_check_residence::CSkillOperateCondition_op_check_residence(
    const std::vector<std::int64_t>* residenceIds, bool pledgeMaster
) :
    m_bPledgeMaster(pledgeMaster) {
    // Current L2Server 0x00a830f4.
    for (const std::int64_t residenceId : *residenceIds) {
        m_ResidenceIds.push_back(static_cast<std::uint32_t>(runtime::Int32FromBits(residenceId)));
    }
}

bool CSkillOperateCondition_op_check_residence::CheckResidence(std::uint32_t residenceId) const {
    // Current L2Server 0x00a80298.
    L2_GUARD_FUNCTION();
    return std::find(m_ResidenceIds.begin(), m_ResidenceIds.end(), residenceId) != m_ResidenceIds.end();
}

bool CSkillOperateCondition_op_check_residence::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a8093c.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    if (!CheckResidence(pledge->GetResidenceId()) && !CheckResidence(pledge->GetWorldAgitDbId())) {
        return false;
    }
    return !m_bPledgeMaster || user->IsPledgeMaster();
}

CSkillOperateCondition_op_check_on_going_event_campaign::CSkillOperateCondition_op_check_on_going_event_campaign(
    const std::vector<std::int64_t>* campaignIds
) {
    // Current L2Server 0x00a83328.
    L2_GUARD_FUNCTION();
    for (const std::int64_t campaignId : *campaignIds) {
        m_EventCampaignIds.push_back(runtime::Int32FromBits(campaignId));
    }
}

bool CSkillOperateCondition_op_check_on_going_event_campaign::CanUse(CCreature*, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a838e0.
    L2_GUARD_FUNCTION();
    for (const int campaignId : m_EventCampaignIds) {
        if (CEventCampaignManager::Inst()->IsWorkingEventCampaignOnlyWorkingState(campaignId)) {
            return true;
        }
    }
    return false;
}

bool CSkillOperateCondition_op_exist_npc::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a80124.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature()) {
        return false;
    }

    class TLMFilter final : public CTLMFilterDist {
    public:
        TLMFilter(int inZoneId, const FVector& position, double distance, const std::set<int>* targetNpc) :
            CTLMFilterDist(inZoneId, position, distance), m_targetNpc(targetNpc) {}

        bool CanDo(CObject* object) override {
            if (!CTLMFilterDist::CanDo(object) || !object->IsNPC()) {
                return false;
            }
            const int npcId = object->CastNPC()->GetNPCId();
            return m_targetNpc->find(npcId) != m_targetNpc->end();
        }

    private:
        const std::set<int>* m_targetNpc;
    };

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(bool* found) : m_found(found) {}

        bool CanDo(CObject*, double) override {
            *m_found = true;
            return false;
        }

    private:
        bool* m_found;
    };

    CCreature* const center = creature->CastCreature();
    bool found = false;
    TLMFilter filter(static_cast<int>(center->GetInZoneID()), center->GetPos(), static_cast<double>(m_nDist), &m_vTargetNPC);
    TLMFunc function(&found);
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
    return m_bExist == found;
}

bool CSkillOperateCondition_op_need_summon::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a82098.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    FOR_ALL_SUMMON_IN_SUMMON_LIST(
        creature->CastUser(), summon,
        // Current 0x00a82098 creates in_code before the class-id lookup.
        if (m_SummonClassIds.find(summon->GetNPCId()) != m_SummonClassIds.end()) { return true; }
    );
    return false;
}

bool CSkillOperateCondition_op_need_summon_or_pet::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a824b0.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        creature->CastUser(), summon,
        if (m_SummonOrPetClassIds.find(summon->GetNPCId()) != m_SummonOrPetClassIds.end()) { return true; }
    );
    return false;
}

bool CSkillOperateCondition_op_need_agathion::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a79c70.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    return m_nAgathionId == 0 ? user->HaveAgathion() : user->HaveAgathion(m_nAgathionId);
}

bool CSkillOperateCondition_can_add_max_entrance_inzone::CanUse(
    CCreature* creature, const CSkillInfo*, CWorldObject* target
) {
    // Current L2Server 0x00a7a900.
    L2_GUARD_FUNCTION();
    return L2Common::IsAlivePC(creature) && L2Common::IsAlivePC(target) &&
        target->CastUser()->CanAddInZoneMaxEntrance(m_groupId, m_delta);
}

bool CSkillOperateCondition_can_bookmark_add_slot::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7a7f0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    if (CUserBookmarkHandler::CanAddSlot(target->CastUser(), m_slotNum)) {
        return true;
    }

    target->CastUser()->SendSystemMessage(SystemMessageId::MY_TELEPORTS_SLOTS_ALREADY_AT_MAXIMUM);
    return false;
}

bool CSkillOperateCondition_can_restore_vital_point::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7ab90.
    L2_GUARD_FUNCTION();
    return L2Common::IsAliveCreature(creature) && L2Common::IsAlivePC(target);
}

bool CSkillOperateCondition_can_use_vitality_consume_item::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7ac54.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC() && creature->CastUser()->CanUseVitalConsumeItem();
}

bool CSkillOperateCondition_can_change_vital_item_count::CanUse(
    CCreature* creature, const CSkillInfo* skillInfo, CWorldObject*
) {
    // Current L2Server 0x00a7ad1c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    if (creature->CastUser()->CanChangeVitalItemCount()) {
        return true;
    }

    const std::uint32_t encodedSkillLevel = (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16U) |
        (static_cast<std::uint32_t>(skillInfo->m_nLevel) & 0xffffU);
    creature->CastUser()->GetSocket()->Send(
        "chccdd", 0x62, SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET, 1, 4, skillInfo->m_nSkillId, encodedSkillLevel
    );
    return false;
}

bool CSkillOperateCondition_op_check_abnormal::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a761b0.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsCreature()) {
        return false;
    }

    if (m_Enable == 0) {
        return !target->CastCreature()->m_data->IsAbnormalType(m_AbnormalType);
    }
    if (m_Enable == 1) {
        return m_Level <= target->CastCreature()->m_data->GetAbnormalLevel(m_AbnormalType);
    }
    if (m_Enable == 2) {
        return m_Level < creature->m_data->GetAbnormalLevel(m_AbnormalType);
    }
    if (m_Enable == 3) {
        return creature->m_data->GetAbnormalLevel(m_AbnormalType) <= m_Level;
    }
    return false;
}

bool CSkillOperateCondition_op_encumbered::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a79570.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    const std::int32_t itemCount = creature->m_data->m_nNumInventory;
    if (itemCount + m_nFreeSlot < 0 || creature->GetInventoryLimit() < itemCount + m_nFreeSlot) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT_EXCEEDED);
        return false;
    }

    const double carryLimit = creature->CarryWeight();
    const double requiredWeight =
        static_cast<double>(creature->m_inv.GetCarryingWeight()) + carryLimit * static_cast<double>(m_nFreeWeight) / 100.0;
    if (0.0 <= requiredWeight && requiredWeight <= carryLimit) {
        return true;
    }

    creature->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
    return false;
}

bool CSkillOperateCondition_op_use_firecracker::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a78ef0.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature()) {
        return false;
    }

    const FVector position = creature->GetPos();
    const int ground = CWorld::Inst().GetGeo()->FindGround(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z) + m_nHeight
    );
    return static_cast<int>(position.z) == ground;
}

bool CSkillOperateCondition_can_enchant_attribute::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79944.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsItem()) {
        return false;
    }

    CItem* const item = target->CastItem();
    if (item == nullptr || !item->m_const->m_bCanAttrEnchant) {
        return false;
    }

    const CrystalType crystal = item->m_const->GetCrystalType();
    if (crystal < CRT_S || crystal > CRT_EVENT) {
        return false;
    }

    return item->m_data->m_Type == IT_WEAPON || (item->m_data->m_Type == IT_ARMOR && (item->m_data->m_Slot & 0x100) == 0);
}

bool CSkillOperateCondition_use_praseed::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7b204.
    L2_GUARD_FUNCTION();
    CItem* const item = creature->m_inv.GetByIndex(creature->m_data->m_nSlotIndex[ST_LBRACELET]);
    return item != nullptr && item->m_const->m_dAgathionMaxEnergy > 0.0 && m_nEnergy > 0 &&
        item->GetAgathionEnergy() + static_cast<double>(m_nEnergy) <= item->m_const->m_dAgathionMaxEnergy;
}

bool CSkillOperateCondition_op_check_equip_slot::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7c600.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    bool equipped = false;
    for (int slot = 0; slot < ST_MAX; ++slot) {
        if (m_bEquipSlotTypeArray[slot] && user->m_data->m_nSlotIndex[slot] != -1) {
            equipped = true;
        }
    }
    if (!equipped && m_nSysMsgNo != -1) {
        user->SendSystemMessage(static_cast<unsigned int>(m_nSysMsgNo));
    }
    return equipped;
}

bool CSkillEffect_i_show_usable_daily_skill_count::Instant(
    CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a80480. Ctor 0x00ca5c80.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }

    std::uint16_t totalCount = 0;
    std::uint16_t usableCount = 0;
    User* const user = target->CastUser();
    if (user != nullptr) {
        auto level = m_SkillLevels.begin();
        for (auto skillId = m_SkillIds.begin(); skillId != m_SkillIds.end(); ++skillId, ++level) {
            const int targetSkillId = runtime::Int32FromBits(static_cast<std::uint32_t>(*skillId));
            const int targetSkillLevel = runtime::Int32FromBits(static_cast<std::uint32_t>(*level));
            const CSkillInfo* const skillInfo =
                g_SkillDB.GetSkillInfo(CSkillKey(targetSkillId, targetSkillLevel, 0), nullptr);
            if (skillInfo->m_nReuseDelay != -1000.0) {
                continue;
            }

            totalCount = static_cast<std::uint16_t>(totalCount + 1U);
            if (user->GetRemainReuseDelaySec(targetSkillId, targetSkillLevel, 0) <= 0.0) {
                usableCount = static_cast<std::uint16_t>(usableCount + 1U);
            }
        }

        if (target->IsPC()) {
            User* const currentUser = target->CastUser();
            const std::uint16_t unavailableBits = static_cast<std::uint16_t>(totalCount - usableCount);
            currentUser->GetSocket()->Send(
                "chcchch", 0x62, SystemMessageId::DAILY_GIFT_COUNT_AND_REMAINING_ALLOWANCE, 2, 0x15,
                static_cast<int>(runtime::Int16FromBits(unavailableBits)), 0x15,
                static_cast<int>(runtime::Int16FromBits(usableCount))
            );
        }
    }
    return true;
}

CSkillOperateCondition_op_instantzone::CSkillOperateCondition_op_instantzone() = default;

CSkillOperateCondition_op_instantzone::CSkillOperateCondition_op_instantzone(std::vector<std::int64_t>* inZoneIds) {
    // Current L2Server 0x00a831e0. The parser semantic carrier is int64,
    // while the condition stores the low int32 of each value.
    L2_GUARD_FUNCTION();
    for (const std::int64_t inZoneId : *inZoneIds) {
        m_InZoneIds.push_back(static_cast<int>(inZoneId));
    }
}

bool CSkillOperateCondition_op_instantzone::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a80688.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->IsInRealWorld()) {
        return false;
    }
    if (m_InZoneIds.empty()) {
        return true;
    }
    return std::find(m_InZoneIds.begin(), m_InZoneIds.end(), user->m_nInstantZoneTypeID) != m_InZoneIds.end();
}

bool CSkillOperateCondition_op_territory::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) {
    // Current L2Server 0x00a79008.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature()) {
        return false;
    }
    if (m_pTerritory == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillOperateCondition_op_territory m_pTerritory is NULL. "
        //     L"skill = [%d, %d]",
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel
        // )
        // L2_LOG_ERROR(Log,
        //     L"CSkillOperateCondition_op_territory m_pTerritory is NULL. "
        //     L"skill = [%d, %d]",
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel
        // )
        L2_LOG_ERROR(
            Log,
            "Cannot evaluate op_territory: territory is unavailable; skill context is %s (ID %d, level %d). The condition "
            "fails.",
            skillInfo == nullptr ? "missing" : "available", skillInfo == nullptr ? 0 : skillInfo->m_nSkillId,
            skillInfo == nullptr ? 0 : skillInfo->m_nLevel
        );
        return false;
    }

    if (m_TargetType == SOTT_SELF) {
        return m_pTerritory->IsInside(creature->GetPos(), false);
    }
    if (m_TargetType == SOTT_TARGET) {
        return m_pTerritory->IsInside(target->GetPos(), false);
    }
    if (m_TargetType == SOTT_ALL) {
        return m_pTerritory->IsInside(creature->GetPos(), false) && m_pTerritory->IsInside(target->GetPos(), false);
    }
    return false;
}

bool CSkillOperateCondition_op_not_territory::CanUse(
    CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target
) {
    // Current L2Server 0x00a791b4.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature()) {
        return false;
    }
    if (m_pTerritory == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillOperateCondition_op_not_territory m_pTerritory is NULL. "
        //     L"skill = [%d, %d]",
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel
        // )
        // L2_LOG_ERROR(Log,
        //     L"CSkillOperateCondition_op_not_territory m_pTerritory is NULL. "
        //     L"skill = [%d, %d]",
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel
        // )
        L2_LOG_ERROR(
            Log,
            "Cannot evaluate op_not_territory: territory is unavailable; skill context is %s (ID %d, level %d). The "
            "condition fails.",
            skillInfo == nullptr ? "missing" : "available", skillInfo == nullptr ? 0 : skillInfo->m_nSkillId,
            skillInfo == nullptr ? 0 : skillInfo->m_nLevel
        );
        return false;
    }

    if (m_TargetType == SOTT_SELF) {
        return !m_pTerritory->IsInside(creature->GetPos(), true);
    }
    if (m_TargetType == SOTT_TARGET) {
        return !m_pTerritory->IsInside(target->GetPos(), true);
    }
    if (m_TargetType == SOTT_ALL) {
        return !m_pTerritory->IsInside(creature->GetPos(), true) && !m_pTerritory->IsInside(target->GetPos(), true);
    }
    return false;
}

bool CSkillOperateCondition_op_can_escape::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7981c.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return true;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_DuelUserInfo.IsInPartyDuel()) {
        return false;
    }
    if (user->m_PledgeGameUser.IsPlayer()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
        return false;
    }
    if (!user->m_bCanEscape) {
        return false;
    }

    CPledgeGameField* const field = user->m_PledgeGameUser.GetField();
    return field == nullptr || !field->m_bIsUnRegist;
}

bool CSkillOperateCondition_op_can_disarm::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a839d4.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    CSharedItemData* const weapon = targetCreature->GetEquipedWeapon();
    if (weapon != nullptr) {
        SlotType slot = ST_UNDERWEAR;
        while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & weapon->m_Slot) == 0) {
            slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
        }
        if (targetCreature->IsEquippedWithForcedItem(slot)) {
            return false;
        }
    }

    if (targetCreature->IsPC()) {
        User* const user = targetCreature->CastUser();
        static_cast<void>(CCursedWeaponMgr::inst());
        if (user != nullptr && user->m_nCursedWeaponClassId > 0) {
            return false;
        }
    }
    return true;
}

bool CSkillOperateCondition_op_check_stuck_npc::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7b5a0.
    L2_GUARD_FUNCTION();
    if (!target->IsNPC() || target->IsSummonOrPet()) {
        return true;
    }
    CCreature* const npc = target->CastCreature();
    return npc->m_SkillMod.m_nBlockMoveCount == 0 && npc->m_data->m_bCanMove;
}

bool CSkillOperateCondition_op_siege_hammer::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) {
    // Current L2Server 0x00a7860c.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || target == nullptr) {
        return false;
    }

    if (skillInfo->m_OperateType == SKILL_BACK_STEP_DURATION) {
        return creature->IsSummonOrPet() && target->IsSummonOrPet() && creature->CastSummon() == target->CastSummon();
    }

    if (target->IsDoor() || target->IsBattleCamp() || target->IsControlTower() ||
        (target->IsNPC() && target->CastNPC()->m_data->m_nClassID == 0x18)) {
        return true;
    }

    if (creature->IsSummonOrPet()) {
        User* const user = creature->GetUserOrMaster();
        if (user != nullptr && user->IsPC()) {
            user->SendSystemMessage(SystemMessageId::INVALID_SERVITOR_TARGET);
        }
    }
    return false;
}

bool CSkillOperateCondition_op_equip_item::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) {
    // Current L2Server 0x00a7a9ec.
    L2_GUARD_FUNCTION();
    if ((m_TargetType == CSkillInfo::OCT_CASTER || m_TargetType == CSkillInfo::OCT_BOTH) &&
        (!L2Common::IsAliveCreature(creature) || !creature->IsEquipedItemByClassId(m_ItemClassId))) {
        return false;
    }

    if (m_TargetType != CSkillInfo::OCT_TARGET && m_TargetType != CSkillInfo::OCT_BOTH) {
        return true;
    }
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsEquipedItemByClassId(m_ItemClassId)) {
        return true;
    }

    if (creature->IsPC()) {
        const int encodedLevel = skillInfo->m_nSubLevel << 16 | static_cast<std::uint16_t>(skillInfo->m_nLevel);
        creature->CastUser()->GetSocket()->Send(
            "chccdcdd", 0x62, SystemMessageId::CANNOT_USE_ITEM_TARGET_REQUIRED_EQUIPMENT_MISSING, 2, 3, m_ItemClassId, 4,
            skillInfo->m_nSkillId, encodedLevel
        );
    }
    return false;
}

bool CSkillOperateCondition_can_refuel_airship::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7ff18.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !creature->IsInAirShip()) {
        return false;
    }
    CAirShip* const airShip = creature->GetMyAirShip();
    return airShip != nullptr && airShip->CanRefuel(m_Amount);
}

bool CSkillOperateCondition_op_home::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7d964.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsPC()) {
        return false;
    }
    User* const user = target->CastUser();
    if (user == nullptr) {
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return m_HomePoint == RESTART_VILLAGE;
    }

    CWorldAgit* const worldAgit = pledge->GetWorldAgit();
    CCastle* const castle = pledge->GetCastle();
    switch (m_HomePoint) {
        case RESTART_VILLAGE:
            return worldAgit == nullptr && castle == nullptr && pledge->GetFortress() == nullptr;
        case RESTART_AGIT:
            return worldAgit != nullptr || CInstantAgitManager::inst().GetInstantAgit(*user) != nullptr;
        case RESTART_CASTLE:
            return castle != nullptr;
        case RESTART_FORTRESS:
            return pledge->GetFortress() != nullptr;
        default:
            return false;
    }
}

bool CSkillOperateCondition_op_can_go::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7de7c. The parsed m_Dist member is deliberately
    // not consumed by this current body. The target uses a literal 40.0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !L2Common::IsAliveCreature(target)) {
        return false;
    }
    if (creature->Id() == target->Id()) {
        return true;
    }

    const FVector creaturePosition = creature->GetPos();
    const FVector targetPosition = target->GetPos();
    const double dx = targetPosition.x - creaturePosition.x;
    const double dy = targetPosition.y - creaturePosition.y;
    const double dz = targetPosition.z - creaturePosition.z;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);

    double unitX = 1.0;
    double unitY = 1.0;
    double unitZ = 1.0;
    if (length <= -1.0e-5 || 1.0e-5 <= length) {
        unitX = dx / length;
        unitY = dy / length;
        unitZ = dz / length;
    }

    FVector destination{
        creaturePosition.x + unitX * 40.0,
        creaturePosition.y + unitY * 40.0,
        creaturePosition.z + unitZ * 40.0,
    };
    FVector outputPosition{};
    bool distanceExhausted = false;
    double travelledDistance;
    CWorld& world = CWorld::Inst();
    return world.CanGo(
               targetPosition, destination, outputPosition, distanceExhausted, travelledDistance,
               static_cast<int>(creature->GetInZoneID()), false
           ) &&
        world.IsValidPosition(static_cast<int>(destination.x), static_cast<int>(destination.y));
}

bool CSkillOperateCondition_op_change_weapon::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a76ac0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    CSharedItemData* const weapon = creature->GetEquipedWeapon();
    if (weapon == nullptr) {
        return false;
    }

    if (weapon->m_bIsVariation) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_CONVERT_AUGMENTED_ITEM);
        return false;
    }

    if (CItemRestrict::Inst().CheckItemRestrict(weapon->m_nClassID)) {
        return false;
    }

    if (g_ConvertItemDB.GetConvertItem(weapon->m_nClassID) != 0) {
        return true;
    }

    creature->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_CONVERTED);
    return false;
}

bool CSkillOperateCondition_consume_body::CanConsumeBody(CCreature* creature, CNPC* npc) {
    // Current L2Server 0x00a77fe8.
    if (creature == nullptr || !L2Common::IsDeadNPC(npc) || npc->m_data->m_nClassID == 0x51) {
        return false;
    }

    const std::int32_t deathCount = npc->m_nDeathCount.load(std::memory_order_relaxed);
    const std::int32_t deathScore = static_cast<std::int32_t>(static_cast<std::uint32_t>(deathCount) * 0x29aU);
    const std::int32_t levelScore = static_cast<std::int32_t>(static_cast<std::uint32_t>(npc->m_data->m_nLevel - 2) * 1000U);
    if (deathScore <= levelScore) {
        return true;
    }

    creature->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_CORPSE_TOO_OLD);
    return false;
}

bool CSkillOperateCondition_consume_body::SetAsCorpse(CNPC* npc) {
    // Current L2Server 0x00a7805c.
    if (npc == nullptr) {
        return false;
    }

    const bool changed = [&] {
        L2_GUARD(*npc->m_lock);
        const bool changed = !npc->m_data->m_bIsCorpse;
        if (changed) {
            npc->m_data->m_bIsCorpse = true;
        }
        return changed;
    }();
    return changed;
}

bool CSkillOperateCondition_consume_body::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a780c4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsCreature() || !L2Common::IsDeadNPC(target)) {
        return false;
    }

    CNPC* const npc = target->CastNPC();
    return CanConsumeBody(creature, npc) && SetAsCorpse(npc);
}

bool CSkillOperateCondition_op_unlock::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a76f80.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }

    if (target->IsNPC()) {
        CNPC* const npc = target->CastNPC();
        if (npc != nullptr && npc->m_data->m_nClassID == 0x53) {
            return true;
        }
    }

    if (!target->IsDoor()) {
        return false;
    }
    CDoor* const door = target->CastDoor();
    if (door == nullptr ||
        (door->m_eDoorOpenMethod != DOOROPEN_SKILL && door->m_eDoorOpenMethod != DOOROPEN_TIME_SKILL &&
            door->m_eDoorOpenMethod != DOOROPEN_ITEM && door->m_eDoorOpenMethod != DOOROPEN_SKILL_ITEM)) {
        return false;
    }

    if (door->m_nInactiveTime <= 0) {
        return true;
    }

    const std::uint32_t inactiveUntil =
        static_cast<std::uint32_t>(door->m_nRemainTime) + static_cast<std::uint32_t>(door->m_nInactiveTime);
    if (runtime::GetTickCount() >= inactiveUntil) {
        return true;
    }

    creature->SendSystemMessage(SystemMessageId::DOOR_LOCKED_TRY_AGAIN_LATER);
    return false;
}

bool CSkillOperateCondition_op_harvest::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7fb78.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !L2Common::IsDeadNPC(target)) {
        return false;
    }

    if (static_cast<int>(target->CastNPC()->m_data->m_nClass) == 0x51) {
        return false;
    }

    User* const user = creature->CastUser();
    CNPC* const npc = target->CastNPC();
    if (user == nullptr || npc == nullptr) {
        return false;
    }

    // The current body takes and releases this lock before it
    // reads the harvesting fields instead of keeping it across the reads.
    {
        L2_GUARD(*npc->m_lock);
    }

    const int harvestState = npc->m_data->m_nCanHarvest;
    const std::uint32_t userSid = user->GetSmartID().Sid();
    if (harvestState == 1) {
        if (static_cast<std::uint32_t>(npc->m_data->m_nSowerSmartID) != userSid) {
            user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
            return false;
        }
        return true;
    }

    if (harvestState != 2 || static_cast<std::uint32_t>(npc->m_data->m_nSowerSmartID) == userSid) {
        return true;
    }

    CCreature* const sower = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npc->m_data->m_nSowerSmartID));
    if (sower == nullptr || !sower->IsPC()) {
        user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
        return false;
    }

    User* const sowerUser = sower->CastUser();
    CParty* const party = sowerUser->GetParty();
    if (party == nullptr) {
        user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
        return false;
    }

    // This is the literal current branch: being in the sower's party still
    // returns false, but suppresses the failure system message.
    if (!party->IsMember(userSid)) {
        user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
    }
    return false;
}

bool CSkillOperateCondition_op_collecting::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7a154.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || target == nullptr || !target->IsCollection()) {
        return false;
    }

    CCollection* const collection = target->CastCollection();
    if (collection != nullptr && collection->CanBeCollected()) {
        return true;
    }

    creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_GATHER_ANOTHER_PLAYER_ALREADY_GATHERING);
    return false;
}

bool CSkillOperateCondition_op_sweeper::CanSweep(User* user, CNPC* npc) {
    // Current L2Server 0x00a83400.
    if (user == nullptr || npc == nullptr || !L2Common::IsDeadNPC(npc) || npc->m_data->m_bIsCorpse ||
        !CSkillOperateCondition_consume_body::CanConsumeBody(user, npc)) {
        return false;
    }

    if (!npc->m_data->m_bIsSpoil || npc->m_data->m_bIsPlunded) {
        user->SendSystemMessage(SystemMessageId::SWEEP_FAILED_TARGET_NOT_SPOILED);
        return false;
    }

    ItemAmountDataStruct rewards;
    {
        L2_GUARD(*npc->m_lock);
        for (const auto& [itemClassId, amount] : npc->m_CorpseDropList) {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            CItem* const item = object != nullptr ? object->CastItem() : nullptr;
            if (item == nullptr) {
                continue;
            }

            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                rewards.Push(itemClassId, static_cast<std::int64_t>(amount));
            } else if (amount > 0) {
                for (int index = 0; index < amount; ++index) {
                    rewards.Push(itemClassId, 1);
                }
            }
        }
    }

    if (!rewards.dataList.empty()) {
        CParty* const party = user->GetParty();
        const bool routeSpoil = party != nullptr &&
            (party->GetRoutingType() == IRT_RANDOMLY_INCLUDING_SPOIL ||
                party->GetRoutingType() == IRT_ORDERLY_INCLUDING_SPOIL);
        if (routeSpoil) {
            for (const ItemAmountData* const reward : rewards.dataList) {
                if (reward == nullptr) {
                    continue;
                }
                CObject* const object = g_ObjectDB.GetObjectW(reward->m_nItemClassId, false);
                if (object == nullptr || !object->IsItem()) {
                    // Log.Add(
                    //     LOG_ERROR, L"[%s][%d] Item[%d] is not exist.", L"SkillFxFunc.cpp", 0xd89, reward->m_nItemClassId
                    // )
                    // L2_LOG_ERROR(Log, L"[%s][%d] Item[%d] is not exist.", L"SkillFxFunc.cpp", 0xd89,
                    // reward->m_nItemClassId
                    // )
                    L2_LOG_ERROR(
                        Log,
                        "Cannot validate party sweep rewards: class ID %d has no item prototype; sweep eligibility is "
                        "denied.",
                        reward->m_nItemClassId
                    );
                    return false;
                }

                CItem* const item = object->CastItem();
                if (reward->m_nItemClassId != 0x39 && !party->CheckRouteItem(item, user)) {
                    // This warning is observable, but the current body keeps
                    // checking the remaining routed rewards.
                    user->SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
                }
            }
        } else if (!user->m_inv.CheckAddableMulti(rewards, false)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
            return false;
        }
    }

    const std::uint32_t userSid = user->GetSmartID().Sid();
    if (npc->m_data->m_nCanSweeper == 1) {
        if (static_cast<std::uint32_t>(npc->m_data->m_nSpoilerSmartID) != userSid) {
            user->SendSystemMessage(SystemMessageId::NO_SWEEP_PRIORITY);
            return false;
        }
    } else if (npc->m_data->m_nCanSweeper == 2 && static_cast<std::uint32_t>(npc->m_data->m_nSpoilerSmartID) != userSid) {
        CCreature* const spoiler =
            g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npc->m_data->m_nSpoilerSmartID));
        if (spoiler == nullptr || !spoiler->IsPC()) {
            // Literal current branch: unlike the later party failures this
            // path returns silently.
            return false;
        }

        User* const spoilerUser = spoiler->CastUser();
        CParty* const spoilerParty = spoilerUser != nullptr ? spoilerUser->GetParty() : nullptr;
        if (spoilerParty == nullptr || !spoilerParty->IsMember(userSid)) {
            user->SendSystemMessage(SystemMessageId::NO_SWEEP_PRIORITY);
            return false;
        }
    }

    return true;
}

bool CSkillOperateCondition_op_sweeper::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a837f8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || target == nullptr || !L2Common::IsDeadNPC(target)) {
        return false;
    }

    return CanSweep(creature->CastUser(), target->CastNPC());
}

bool CSkillOperateCondition_can_use_swoop_cannon::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a78334.
    L2_GUARD_FUNCTION();
    User* const user = creature->GetUserOrMaster();
    if (user == nullptr) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] SkillUser is not PC/Pet/Summon.", L"SkillFxFunc.cpp", 0xe5c)
        // L2_LOG_ERROR(Log, L"[%s][%d] SkillUser is not PC/Pet/Summon.", L"SkillFxFunc.cpp", 0xe5c)
        L2_LOG_ERROR(
            Log, "Cannot evaluate can_use_swoop_cannon: the caster has no available player or owner; the condition fails."
        );
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    FVector position = user->GetPos();
    const int castleId = CCastle::GetBattleFieldCastleId(position);
    const int siegeResidenceId = pledge->GetCurrentCastleSiegeDbId(castleId);

    CResidence* activeResidence = nullptr;
    if (siegeResidenceId > 0 && pledge->GetCurrentCastleSiegeStatus(castleId) == 1) {
        CResidence* const residence = g_ResidenceDB.GetResidence(siegeResidenceId);
        if (residence == nullptr || !residence->IsCastle()) {
            return false;
        }
        activeResidence = residence->CastCastle();
    } else {
        const int fortressId = static_cast<int>(pledge->SharedData()->m_nFortressUnderOffenceSiegeId);
        if (fortressId == 0) {
            if (user->m_data->m_nDominionSiegeId == 0 || user->m_data->m_nDominionSiegeStatus == 0) {
                return false;
            }
            position = user->GetPos();
            if (!user->IsInRealWorld()) {
                return false;
            }
            return g_ResidenceDB.CanUseSwoopCannonInDominionSiege(position, false);
        }

        CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
        if (residence == nullptr || !residence->IsFortress()) {
            return false;
        }
        activeResidence = residence->CastFortress();
    }

    position = user->GetPos();
    return user->IsInRealWorld() && activeResidence != nullptr && activeResidence->IsInBattleField(position, true) &&
        !activeResidence->IsInResidence(position, false);
}

bool CSkillOperateCondition_can_mount_for_event::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7e760.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_npcName.c_str(), false);
    if (npcClassId < 0) {
        const std::string hostName = Utf16ToUtf8(m_npcName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] invalid npc name [%S]",
        //     "SkillFxFunc.cpp",
        //     0x1628,
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] invalid npc name [%S]",
        //     "SkillFxFunc.cpp",
        //     0x1628,
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot evaluate can_mount_for_event: NPC name '%s' resolves to negative class ID %d; the condition fails.",
            hostName.c_str(), npcClassId
        );
        return false;
    }

    return target->CastUser()->CanRideForEvent(npcClassId);
}

bool CSkillOperateCondition_can_summon_siege_golem::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7d730.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    const FVector position = user->GetPos();
    const int castleId = user->m_nCurCastleId;
    const int siegeResidenceId = pledge->GetCurrentCastleSiegeDbId(castleId);
    if (!((siegeResidenceId > 0 && pledge->GetCurrentCastleSiegeStatus(castleId) == 1) ||
            (user->m_data->m_nDominionSiegeId != 0 && user->m_data->m_nDominionSiegeStatus != 0))) {
        return false;
    }

    CResidence* residence = nullptr;
    if (siegeResidenceId < 1) {
        if (!CWorld::Inst().GetGeo()->IsBattleField(position)) {
            return false;
        }
        residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(position);
    } else {
        residence = g_ResidenceDB.GetResidence(siegeResidenceId);
    }
    if (residence == nullptr || !residence->IsCastle()) {
        return false;
    }

    CResidence* const castle = residence->CastCastle();
    bool usable = castle != nullptr && castle->IsInBattleField(position, true) && !castle->IsInResidence(position, false);
    if (SSQSystem::inst().GetSealOwner(SSQSeal::STRIFE) == SSQPart::DAWN) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_DUE_TO_SEAL_OF_STRIFE);
        usable = false;
    }
    return usable;
}

bool CSkillOperateCondition_op_equip_agathion_main_slot::CanUse(
    CCreature* creature, const CSkillInfo* skillInfo, CWorldObject*
) {
    // Current L2Server 0x00a82d84.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || !creature->IsAlive()) {
        return false;
    }

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(creature->m_data->m_nSlotIndex[ST_AGATHION_MAIN]);
    if (!item) {
        return false;
    }

    std::map<int, const CSkillInfo*> skills;
    creature->CastUser()->GetSkillsFromItem(*item.get(), skills, User::CCT_ANY_CRYSTAL_LEVEL);
    if (skills.find(skillInfo->m_nSkillId) != skills.end()) {
        return true;
    }

    for (const CSkillInfo* const equipSkill : item->m_ItemEquipSkillList) {
        if (equipSkill != nullptr && equipSkill->m_nSkillId == skillInfo->m_nSkillId) {
            return true;
        }
    }
    return false;
}

bool CSkillOperateCondition_op_resurrection::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a787e4.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsCreature()) {
        return false;
    }

    User* targetUser = nullptr;
    if (target->IsPC()) {
        targetUser = target->CastUser();
        int castleSiegeStatus = 0;
        int fortressSiegeStatus = 0;
        if (!targetUser->CanResurrection(castleSiegeStatus, fortressSiegeStatus)) {
            unsigned int messageId = SystemMessageId::CANNOT_RESURRECT_ON_ACTIVE_SIEGE_BATTLEFIELD;
            if (castleSiegeStatus == 2) {
                messageId = targetUser->GetSSQPart() == SSQPart::TWILIGHT
                    ? SystemMessageId::DUSK_MEMBER_CANNOT_RESURRECT
                    : SystemMessageId::CANNOT_RESURRECT_GUARDIAN_TOWER_DESTROYED;
            } else if (castleSiegeStatus == 1 || fortressSiegeStatus == 1) {
                messageId = SystemMessageId::CANNOT_RESURRECT_WITHOUT_HEADQUARTERS;
            }
            targetUser->SendSystemMessage(messageId);
            creature->SendSystemMessage(messageId);
            return false;
        }
        if (!targetUser->m_data->m_bCanBeResurrected) {
            return false;
        }
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->IsPC()) {
        if (!targetCreature->IsPet()) {
            return true;
        }

        targetCreature->CastPet();
        CPet* const pet = targetCreature->CastPet();
        User* const owner = pet->GetMaster();
        if (owner == nullptr) {
            return false;
        }
        if (owner->m_data->m_bCanResurrection == 0) {
            return true;
        }
        owner->SendSystemMessage(SystemMessageId::PET_RESURRECTION_INVALIDATED_OWNER_RESURRECTING);
        creature->SendSystemMessage(SystemMessageId::CANNOT_RESURRECT_PET_DURING_OWNER_RESURRECTION_ATTEMPT);
        return false;
    }

    User* const resurrectionUser = targetCreature->CastUser();
    CPet* const pet = resurrectionUser->GetPet();
    if (pet == nullptr || pet->m_data->m_bCanResurrection == 0) {
        return true;
    }
    resurrectionUser->SendSystemMessage(SystemMessageId::OWNER_RESURRECTION_INVALIDATED_PET_RESURRECTING);
    creature->SendSystemMessage(SystemMessageId::OWNER_RESURRECTION_INVALIDATED_PET_RESURRECTING);
    return false;
}

bool CSkillOperateCondition_can_transform::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7645c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const user = target->CastUser();
    if (user == nullptr) {
        return false;
    }
    if (user->m_SkillMod.m_bBlockBuff) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_UNDER_SPECIAL_SKILL_EFFECT);
        return false;
    }
    if (user->m_data->m_transformID > 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_ALREADY_TRANSFORMED);
        return false;
    }
    if (user->m_SkillMod.m_cannotTransform) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_PENALTY_STILL_ACTIVE);
        return false;
    }
    if (user->IsInVehicle() || user->IsInAirShip() || user->IsInShuttle()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_ON_BOAT_AIRSHIP_OR_ELEVATOR);
        return false;
    }
    if (user->IsRiding()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_WHILE_RIDING_PET);
        return false;
    }
    if (user->m_data->m_StopMode != STP_STAND) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_WHILE_SITTING);
        return false;
    }

    CTransform* const transform = g_TransformDB.Find(m_transformID, user->m_data->m_Sex);
    if (transform == nullptr || !transform->CanTransform(user)) {
        return false;
    }
    return !transform->IsFlying() || L2SkillFunc::CanFlyingTransform(user);
}

bool CSkillOperateCondition_can_transform_in_dominion::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7d300.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    int dominionId = 0;
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        if (pledge->GetCastle() != nullptr) {
            user->SendSystemMessage(SystemMessageId::TERRITORY_OWNING_CLAN_MEMBER_CANNOT_DISGUISE);
            return false;
        }
        dominionId = CDominionManager::Inst()->GetPledgeDominionSiegeID(static_cast<int>(user->m_data->m_nPledgeID));
    }

    if (user->IsEinhasadeTransformed() || user->m_data->m_nKarma < 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_DISGUISE_WHILE_CHAOTIC);
        return false;
    }

    if (dominionId == 0) {
        dominionId = CDominionManager::Inst()->GetUserDominionSiegeID(static_cast<int>(user->GetDBID()));
    }
    if (dominionId != m_nDominionID) {
        user->SendSystemMessage(SystemMessageId::DISGUISE_SCROLL_DOES_NOT_MATCH_TERRITORY);
        return false;
    }
    if (!CDominionManager::Inst()->IsInReadyToDominionSiege()) {
        user->SendSystemMessage(SystemMessageId::TERRITORY_WAR_DISGUISE_AVAILABILITY_WINDOW);
        return false;
    }
    return true;
}

bool CSkillOperateCondition_build_camp::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a77434.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC() && creature->CastUser()->CanInstallBattleCamp();
}

bool CSkillOperateCondition_build_advance_base::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a77504.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC() && creature->CastUser()->CanInstallAdvancedBattleCamp();
}

bool CSkillOperateCondition_can_use_in_battlefield::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7d550.
    L2_GUARD_FUNCTION();
    User* const user = creature->GetUserOrMaster();
    if (user == nullptr) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] SkillUser is not PC/Pet/Summon.", L"SkillFxFunc.cpp", 0xe96)
        // L2_LOG_ERROR(Log, L"[%s][%d] SkillUser is not PC/Pet/Summon.", L"SkillFxFunc.cpp", 0xe96)
        L2_LOG_ERROR(
            Log, "Cannot evaluate can_use_in_battlefield: the caster has no available player or owner; the condition fails."
        );
        return false;
    }
    if (!user->IsInRealWorld()) {
        return false;
    }

    if (!m_bAnyBattlefield) {
        return user->IsInMySiegeBattleField() != 0;
    }
    if (user->m_AreaMod.m_bIsInPvPZone || !user->m_AreaMod.m_bIsInBattleField) {
        return false;
    }

    const FVector position = user->GetPos();
    if (!CWorld::Inst().GetGeo()->IsBattleField(position)) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(position);
    return residence != nullptr &&
        (residence->IsCastle() || residence->IsFortress() || residence->IsWorldAgit() || residence->IsDominion());
}

bool CSkillOperateCondition_enchant::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7d1dc.
    L2_GUARD_FUNCTION();
    if (!target->IsItem()) {
        return false;
    }

    CItem* const item = target->CastItem();
    const int crystal = static_cast<int>(item->m_const->GetCrystalType());
    if (static_cast<int>(m_CrystalType) != BaseCrystal[crystal]) {
        return false;
    }

    const int itemEnchantType = item->m_const->m_nItemEnchantType;
    if (itemEnchantType < 1 || m_nItemEnchantTypeOp != itemEnchantType) {
        return false;
    }

    return std::find(m_vItemType.begin(), m_vItemType.end(), item->m_data->m_Type) != m_vItemType.end();
}

bool CSkillOperateCondition_enchant_divide::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7d0c0.
    L2_GUARD_FUNCTION();
    if (!target->IsItem()) {
        return false;
    }

    CItem* const item = target->CastItem();
    if (m_CrystalType != item->m_const->GetCrystalType()) {
        return false;
    }

    const int itemEnchantType = item->m_const->m_nItemEnchantType;
    if (itemEnchantType < 1 || m_nItemEnchantTypeOp != itemEnchantType) {
        return false;
    }

    return std::find(m_vItemType.begin(), m_vItemType.end(), item->m_data->m_Type) != m_vItemType.end();
}

bool CSkillOperateCondition_enchant_range::CheckEnchantRange(const int enchant) {
    // Current L2Server 0x00a7af20.
    L2_GUARD_FUNCTION();
    return m_nMinEnchant <= enchant && enchant <= m_nMaxEnchant;
}

bool CSkillOperateCondition_enchant_range::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7afc8. Several mismatched subtype branches
    // deliberately return true in the shipping body. Preserve them.
    L2_GUARD_FUNCTION();
    if (!target->IsItem()) {
        return false;
    }

    CItem* const item = target->CastItem();
    if (m_nType == -1) {
        return false;
    }

    if (item->IsWeapon()) {
        if (m_nType == 0) {
            return CheckEnchantRange(item->m_data->GetEnchanted());
        }
        if (m_nType == 1) {
            if (item->m_const->m_bIsMagicWeapon) {
                return true;
            }
            return CheckEnchantRange(item->m_data->GetEnchanted());
        }
        if (m_nType != 2 || !item->m_const->m_bIsMagicWeapon) {
            return true;
        }
        return CheckEnchantRange(item->m_data->GetEnchanted());
    }

    if (item->IsArmor()) {
        if (m_nType == 0) {
            return CheckEnchantRange(item->m_data->GetEnchanted());
        }
        if (m_nType == 3) {
            int slot = 0;
            while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
                ++slot;
            }
            if (slot == ST_ONEPIECE) {
                return CheckEnchantRange(item->m_data->GetEnchanted());
            }
        }
        return true;
    }

    if (item->IsAccessary()) {
        if (m_nType == 0) {
            return CheckEnchantRange(item->m_data->GetEnchanted());
        }
        if (m_nType == 4) {
            int slot = 0;
            while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
                ++slot;
            }
            if (slot != ST_AGATHION_MAIN) {
                return false;
            }
            return CheckEnchantRange(item->m_data->GetEnchanted());
        }
    }

    return true;
}

bool CSkillOperateCondition_op_check_class::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a82304. Slot +0x168 is IsPC and +0x208 is
    // CastUser in the current CObject query/cast surface.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || target == nullptr) {
        return false;
    }

    CCreature* const checked = L2SkillFunc::GetCheckedCreature(creature, target, m_TargetType);
    if (checked == nullptr) {
        return false;
    }
    if (!checked->IsPC()) {
        return true;
    }

    std::vector<ClassType> baseJobs;
    checked->CastUser()->GetBaseJobClass(baseJobs);
    if (std::find(baseJobs.begin(), baseJobs.end(), m_ClassID) != baseJobs.end()) {
        return m_bExist;
    }
    return m_bExist == (m_ClassID == checked->m_data->m_nClass);
}

bool CSkillOperateCondition_can_summon_cubic::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7a35c.
    L2_GUARD_FUNCTION();
    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user != nullptr && CMiniGameAgit::IsPlayer(user)) {
            return false;
        }
    }
    return true;
}

bool CSkillOperateCondition_can_summon::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject*) {
    // Current L2Server 0x00a7a4e4.
    L2_GUARD_FUNCTION();
    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user != nullptr && user->HaveSummon()) {
            return false;
        }
    }
    return L2SkillFunc::CanSummon(creature, skillInfo->m_TargetType, true, false);
}

bool CSkillOperateCondition_can_summon_multi::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject*) {
    // Current L2Server 0x00a7e630.
    L2_GUARD_FUNCTION();

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_sNPCName.c_str(), false);
    if (npcClassId == -1) {
        const std::string hostName = Utf16ToUtf8(runtime::StringView(m_sNPCName));
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined class in can_summon_multi [%s]",
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"Undefined class in can_summon_multi [%s]",
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot evaluate can_summon_multi: NPC name '%s' resolves to class ID -1; the condition fails.",
            hostName.c_str()
        );
        return false;
    }

    return L2SkillFunc::CheckMultiSummonable(creature, npcClassId) &&
        L2SkillFunc::CanSummon(creature, skillInfo->m_TargetType, true, false);
}

bool CSkillOperateCondition_can_summon_pet::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject*) {
    // Current L2Server 0x00a7a5c0.
    L2_GUARD_FUNCTION();
    return L2SkillFunc::CanSummon(creature, skillInfo->m_TargetType, false, true);
}

bool CSkillOperateCondition::CanUse(CCreature*, const CSkillInfo*, CWorldObject*) {
    // Current L2Server ICF body 0x004c0008.
    return false;
}

CSkillEffect_op_combat::CSkillEffect_op_combat(bool combat) : m_Combat(combat) {
    // Current L2Server 0x00a79f98.
}

bool CSkillEffect_op_combat::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79fac.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }
    return (creature->m_data->m_bIsInCombatMode != 0) == m_Combat;
}

CSkillEffect_op_skill::CSkillEffect_op_skill(int skillId, int skillLevel, bool exists) :
    m_SkillId(skillId), m_SkillLevel(skillLevel), m_Exist(exists) {
    // Current L2Server 0x00a79d54.
}

bool CSkillEffect_op_skill::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79d70.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }
    const int acquiredLevel = target->CastUser()->GetAcquiredSkillLevel(m_SkillId);
    return m_Exist == (acquiredLevel == m_SkillLevel);
}

CSkillEffect_op_skill_acquire::CSkillEffect_op_skill_acquire(int skillId, int use) : m_SkillId(skillId), m_Use(use) {
    // Current L2Server 0x00a79e70.
}

bool CSkillEffect_op_skill_acquire::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79e88.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    if (!target->IsPC()) {
        return true;
    }
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }
    const int acquiredLevel = target->CastUser()->GetAcquiredSkillLevel(m_SkillId);
    return static_cast<unsigned int>(m_Use) == static_cast<unsigned int>(acquiredLevel != -1);
}

CSkillEffectByWeaponType::CSkillEffectByWeaponType() : m_bWeaponTypeArray{} {
    // Current L2Server 0x00a81f54.
}

void CSkillEffectByWeaponType::SetAttackTypeArray(bool values[AT_MAX]) {
    // Current L2Server 0x00a75960.
    L2_GUARD_FUNCTION();
    std::copy_n(values, AT_MAX, m_bWeaponTypeArray);
}

void CSkillEffectByWeaponType::SetAllAttackType(bool value) {
    // Current L2Server 0x00a75a14.
    L2_GUARD_FUNCTION();
    std::fill_n(m_bWeaponTypeArray, AT_MAX, value);
}

bool CSkillEffectByWeaponType::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00a75ac0.
    L2_GUARD_FUNCTION();
    return m_bWeaponTypeArray[static_cast<int>(creature->GetRealAttackType())];
}

CSkillEffectAddBonusByWeaponType::CSkillEffectAddBonusByWeaponType(
    double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName
) :
    m_dBonus(bonus), m_Sfct(changeType), m_ModName(modName) {
    // Current L2Server 0x00a81f8c.
}

bool CSkillEffectAddBonusByWeaponType::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00a75b70.
    L2_GUARD_FUNCTION();
    const AttackType type =
        m_ModName == CCreatureSkillMod::Range ? creature->GetAttackType() : creature->GetRealAttackType();
    return m_bWeaponTypeArray[static_cast<int>(type)];
}

bool CSkillEffectAddBonusByWeaponType::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00a75c2c.
    L2_GUARD_FUNCTION();
    AddSkillModBonus(creature, m_ModName, m_Sfct, m_dBonus);
    return true;
}

CSkillEffectByArmorType::CSkillEffectByArmorType() : m_bArmorTypeArray{} {
    // Current L2Server 0x00a81fe0.
}

void CSkillEffectByArmorType::SetArmorTypeArray(bool values[AMT_MAX]) {
    // Current L2Server 0x00a75cdc.
    L2_GUARD_FUNCTION();
    std::copy_n(values, AMT_MAX, m_bArmorTypeArray);
}

void CSkillEffectByArmorType::SetAllArmorType(bool value) { std::fill_n(m_bArmorTypeArray, AMT_MAX, value); }

bool CSkillEffectByArmorType::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00a75d90.
    L2_GUARD_FUNCTION();
    return m_bArmorTypeArray[static_cast<int>(creature->GetArmorType())];
}

CSkillEffectAddBonusByArmorType::CSkillEffectAddBonusByArmorType(
    double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName
) :
    m_dBonus(bonus), m_Sfct(changeType), m_ModName(modName) {
    // Current L2Server 0x00a82008.
}

bool CSkillEffectAddBonusByArmorType::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00a75e40.
    L2_GUARD_FUNCTION();
    AddSkillModBonus(creature, m_ModName, m_Sfct, m_dBonus);
    return true;
}

CSkillEffectByAdditionalArmorType::CSkillEffectByAdditionalArmorType() : m_bAdditionalArmorTypeArray{} {
    // Current L2Server 0x00a8203c.
}

void CSkillEffectByAdditionalArmorType::SetAdditionalArmorTypeArray(bool values[AAMT_MAX]) {
    // Current L2Server 0x00a75ef0.
    L2_GUARD_FUNCTION();
    std::copy_n(values, AAMT_MAX, m_bAdditionalArmorTypeArray);
}

void CSkillEffectByAdditionalArmorType::SetAllAdditionalArmorType(bool value) {
    std::fill_n(m_bAdditionalArmorTypeArray, AAMT_MAX, value);
}

bool CSkillEffectByAdditionalArmorType::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00a75fa4.
    L2_GUARD_FUNCTION();
    return m_bAdditionalArmorTypeArray[static_cast<int>(creature->m_data->m_AdditionalArmorType)];
}

CSkillEffectAddBonusByAdditionalArmorType::CSkillEffectAddBonusByAdditionalArmorType(
    double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName
) :
    m_dBonus(bonus), m_Sfct(changeType), m_ModName(modName) {
    // Current L2Server 0x00a82064.
}

bool CSkillEffectAddBonusByAdditionalArmorType::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00a76050.
    L2_GUARD_FUNCTION();
    AddSkillModBonus(creature, m_ModName, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffectByShield::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00a80790.
    L2_GUARD_FUNCTION();
    CSharedItemData* const data = CItemFactory::Inst().GetData(creature->m_data->m_nSlotIndex[ST_LHAND]);
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(creature->m_data->m_nSlotIndex[ST_LHAND]);
    return data != nullptr && data->m_Type == IT_ARMOR && item && item->m_const->m_ArmorType != AMT_SIGIL;
}

CSkillEffectAddBonusByShield::CSkillEffectAddBonusByShield(
    double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName
) :
    m_dBonus(bonus), m_Sfct(changeType), m_ModName(modName) {
    // Current L2Server 0x00a82d54.
}

bool CSkillEffectAddBonusByShield::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00a76100.
    L2_GUARD_FUNCTION();
    AddSkillModBonus(creature, m_ModName, m_Sfct, m_dBonus);
    return true;
}

namespace L2SkillFunc {

void AddBonus(double* percent, double* difference, SkillFxChangeType changeType, double bonus) {
    // Current L2Server 0x00a73a34.
    if (changeType == SFCT_PER) {
        *percent *= (bonus + 100.0) / 100.0;
    } else if (changeType == SFCT_DIFF) {
        *difference += bonus;
    }
}

void RemoveBonus(double* percent, double* difference, SkillFxChangeType changeType, double bonus) {
    // Current L2Server 0x00a73aac.
    if (changeType == SFCT_PER) {
        *percent /= (bonus + 100.0) / 100.0;
    } else if (changeType == SFCT_DIFF) {
        *difference -= bonus;
    }
}

void AddBonus(CCreature* creature, CCreatureSkillMod::ModName modName, SkillFxChangeType changeType, double bonus) {
    // Current L2Server 0x00a73a60.
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[static_cast<int>(modName)];
    AddBonus(&mod.m_Per, &mod.m_Diff, changeType, bonus);
}

bool pump_rate_by_hp(
    CCreature* creature,
    const CSkillInfo* skillInfo,
    bool showMessage,
    double threshold,
    double& percent,
    double& difference,
    double bonus,
    SkillFxChangeType changeType
) {
    // Current L2Server 0x00a73ae0.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsLowHPState = true;

    const bool active = creature->m_data->m_dHP <= threshold * creature->m_data->m_dBaseHP;
    if (showMessage && creature->IsPC()) {
        const std::uint32_t encodedLevel =
            (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16U) | static_cast<std::uint32_t>(skillInfo->m_nLevel);
        creature->CastUser()->GetSocket()->Send(
            "chccdd", 0x62,
            active ? SystemMessageId::EFFECT_ACTIVATED_AFTER_HP_DECREASE : SystemMessageId::EFFECT_ENDED_AFTER_HP_INCREASE,
            1, 4, skillInfo->m_nSkillId, runtime::Int32FromBits(encodedLevel)
        );
    }

    if (!active) {
        return false;
    }

    AddBonus(&percent, &difference, changeType, bonus);
    if (creature->IsPC()) {
        // Current 0x00a73c89 calls ParamChanged. R8 is not an argument.
        creature->CastUser()->ParamChanged(std::int64_t{PCB_USER});
    }
    return true;
}

bool pump_rate_by_hp(
    CCreature* creature,
    const CSkillInfo* skillInfo,
    bool showMessage,
    double threshold,
    CCreatureSkillMod::ModName modName,
    double bonus,
    SkillFxChangeType changeType
) {
    // Current L2Server 0x00a73d2c.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[static_cast<int>(modName)];
    return pump_rate_by_hp(creature, skillInfo, showMessage, threshold, mod.m_Per, mod.m_Diff, bonus, changeType);
}

void _DecrementMapRef(std::map<int, int>* values, int key) {
    // Current L2Server 0x00a812f4.
    const auto it = values->find(key);
    if (it != values->end() && --it->second < 1) {
        values->erase(it);
    }
}

void _IncrementMapRef(std::map<int, int>* values, int key) {
    // Current L2Server 0x00a81ec4.
    const auto it = values->find(key);
    if (it == values->end()) {
        values->emplace(key, 1);
    } else {
        ++it->second;
    }
}

bool CanFlyingTransform(User* const user) {
    // Current L2Server 0x00a75394.
    L2_GUARD_FUNCTION();
    if (user->HavePet() || user->HaveSummon()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_WITH_PET_OR_SERVITOR_SUMMONED);
        return false;
    }

    const FVector position = user->GetPos();
    CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(
        ((static_cast<int>(position.x) + 0x50000) >> 15) + 10, ((static_cast<int>(position.y) + 0x40000) >> 15) + 10
    );
    if (zone != nullptr && zone->IsInTransformableArea(position)) {
        return true;
    }

    user->SendSystemMessage(SystemMessageId::CANNOT_BOARD_OR_DISEMBARK_AIRCRAFT_IN_THIS_AREA);
    return false;
}

bool CanSummon(CCreature* const creature, const SkillTargetTypeEnum targetType, const bool summon, const bool pet) {
    // Current L2Server 0x00a75074.
    L2_GUARD_FUNCTION();

    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (!user->CanSummon(summon, pet)) {
        return false;
    }

    FVector source{};
    if (targetType == STGT_GROUND) {
        source = creature->m_NonTargetSkillPos;
        if (source.IsEqual(FVector{})) {
            return false;
        }
    } else {
        source = creature->m_data->m_Pos;
    }

    // Current code tests the IsInAirShip slot only.
    if (creature->IsInAirShip()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AT_CURRENT_LOCATION);
        return false;
    }

    FVector destination{};
    if (!CWorld::Inst().GetValidPositionAroundPos(
            *creature, 0x28, creature->m_data->m_dCollisionRadius, source, destination
        )) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AT_CURRENT_LOCATION);
        return false;
    }
    return CWorld::Inst().CanSummonNpc(destination, user);
}

bool CheckMultiSummonable(CCreature* const creature, const int npcClassId) {
    // Current L2Server 0x00a75294.
    L2_GUARD_FUNCTION();

    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user->GetOneSummon(User::SUMMON_SINGLE) != nullptr) {
        return false;
    }

    const int summonPoint = CSummon::CheckMultiSummonPoint(npcClassId);
    return summonPoint > 0 && creature->CastUser()->CheckSummonPointAddable(summonPoint);
}

CCreature* GetCheckedCreature(CCreature* creature, CObject* target, SkillTargetTypeEnum targetType) {
    // Current L2Server 0x00a745ac.
    L2_GUARD_FUNCTION();
    if (targetType == STGT_SELF) {
        return creature;
    }
    if (targetType == STGT_TARGET) {
        if (target != nullptr && target->IsCreature()) {
            return target->CastCreature();
        }
        return nullptr;
    }
    // Log.Add(LOG_ERROR, L"not implemented target type (%d) in check_level.", static_cast<int>(targetType))
    // L2_LOG_ERROR(Log, L"not implemented target type (%d) in check_level.", static_cast<int>(targetType))
    L2_LOG_ERROR(
        Log,
        "Unsupported target type %d while selecting a creature; only self and target are handled. No creature is returned.",
        static_cast<int>(targetType)
    );
    return nullptr;
}

bool ApplyShieldDefense(
    CCreature& attacker, CObject* const target, const double probability, double& shieldDefense, const bool notify
) {
    // Current L2Server 0x00a73e3c.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }
    if (!target->IsCreature()) {
        shieldDefense = 0.0;
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        shieldDefense = 0.0;
        return false;
    }
    shieldDefense = targetCreature->GetShieldDefense();
    if (shieldDefense == 0.0) {
        return false;
    }
    if (!targetCreature->IsShieldDefend(attacker, probability)) {
        shieldDefense = 0.0;
        return false;
    }

    const bool perfectDefense = genrand64_real1() * 100.0 <= targetCreature->GetShieldDefenseRate() * 2.0;
    if (target->IsPC()) {
        if (perfectDefense) {
            target->CastUser()->SendSystemMessage(SystemMessageId::PERFECT_SHIELD_DEFENSE_SUCCEEDED);
        } else {
            if (notify) {
                CCreature::SendMagicAttckInfo(&attacker, target->CastCreature(), MagicAttackInfo::ShieldDefense);
            }
            target->CastUser()->SendSystemMessage(SystemMessageId::SHIELD_DEFENSE_SUCCEEDED);
        }
    }
    return perfectDefense;
}

MagicAttackerInfo GetMagicAttackerInfo(CCreature& creature) {
    // Current L2Server 0x00a741a0.
    L2_GUARD_FUNCTION();
    MagicAttackerInfo result;
    L2_GUARD(*creature.m_lock);

    result.m_MAttack = creature.m_data->m_dParam[VALIDATE::MATTACK];
    if (creature.m_data->m_bSpiritShot) {
        const double extra = creature.GetSpritShotExtraBonus() + 1.0;
        result.m_MAttack = extra * extra * (creature.m_data->m_dSpiritShotAttackBonusPer + 1.0) * result.m_MAttack;
    }
    result.m_MagicPower = result.m_MAttack;
    result.m_WitBonus = creature.GetMHit();
    result.m_Penalty = creature.GetWeaponGradePenalty() > 0;
    return result;
}

MagicTargetInfo GetMagicTargetInfo(CCreature& creature, CObject& target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a74314.
    L2_GUARD_FUNCTION();
    MagicTargetInfo result;
    if (target.IsCreature()) {
        CCreature* const targetCreature = target.CastCreature();
        if (targetCreature != nullptr) {
            L2_GUARD(*targetCreature->m_lock);
            result.m_AttrResist = creature.GetSkillAttributeBonus(targetCreature, skillInfo, false);
            result.m_MDefend = targetCreature->m_data->m_dParam[VALIDATE::MDEFEND];
            result.m_WitBonus = targetCreature->GetMHit();
            result.m_Level = targetCreature->Level();
            result.m_pTargetCreature = targetCreature;
        }
    } else if (target.IsStaticObject()) {
        CStaticObject* const staticObject = target.CastStaticObject();
        if (staticObject != nullptr) {
            L2_GUARD(*staticObject->m_lock);
            result.m_MDefend = staticObject->GetMDefend();
            result.m_AttrResist = 1.0;
            result.m_pTargetStaticObject = staticObject;
        }
    }

    if (target.IsPC() && creature.IsNPC()) {
        User* const targetUser = target.CastUser();
        CNPC* const attackerNpc = creature.CastNPC();
        if (targetUser != nullptr && attackerNpc != nullptr) {
            CCreatureSkillMod::BonusInfoInCondition* const bonus =
                targetUser->m_SkillMod.GetPVEMagicDefenceBonus(attackerNpc->GetClassId());
            if (bonus != nullptr) {
                result.m_MDefend = ((bonus->m_dAmountPer + 100.0) / 100.0) * result.m_MDefend + bonus->m_dAmountDiff;
            }
        }
    }
    if (result.m_pTargetCreature != nullptr) {
        result.m_MDefend *= creature.m_SkillMod.m_dMagicalPenetrateFactor;
    }
    if (result.m_MDefend == 0.0) {
        result.m_MDefend = 1.0;
    }
    return result;
}

double ApplyMagicCriticalToDamage(const double damage, CCreature& attacker, CObject& target) {
    // Current L2Server 0x00a74950.
    L2_GUARD_FUNCTION();
    double targetPercent = 1.0;
    double targetDifference = 0.0;
    if (target.IsCreature()) {
        CCreature* const targetCreature = target.CastCreature();
        if (targetCreature != nullptr) {
            const CCreatureSkillMod::Mod& targetMod =
                targetCreature->m_SkillMod.m_Mods[CCreatureSkillMod::MagicDefenceCriticalDamage];
            targetPercent = targetMod.m_Per;
            targetDifference = targetMod.m_Diff;
        }
    }

    const CCreatureSkillMod::Mod& attackerMod = attacker.m_SkillMod.m_Mods[CCreatureSkillMod::MagicCriticalDamage];
    double result = targetPercent * attackerMod.m_Per * damage + targetDifference + attackerMod.m_Diff;
    if (!FloatChecker::CheckValidation(result)) {
        if (target.IsCreature()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s]->[%s]1 inval dmg:%.5f(%p) "
            //     L"fpclass:%04x ",
            //     attacker.GetName(), target.GetName(), result, DoubleBitsAsPointer(result), FloatChecker::FPClass(result)
            // )
            // Noise: The following diagnostic includes the invalid critical damage, inputs and fallback in one record.
            // L2_LOG_ERROR(Log,
            //     L"FloatERR [%s]->[%s]1 inval dmg:%.5f(%p) "
            //     L"fpclass:%04x ",
            //     attacker.GetName(), target.GetName(), result, DoubleBitsAsPointer(result), FloatChecker::FPClass(result)
            // )
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s]->[%s]2 inval org_dmg:%.5f(%p) "
            //     L"actor(p:%.5f(%p), d:%.5f(%p)) "
            //     L"target(p:%.5f(%p), d:%.5f(%p))",
            //     attacker.GetName(), target.GetName(), damage, DoubleBitsAsPointer(damage), attackerMod.m_Per,
            //     DoubleBitsAsPointer(attackerMod.m_Per), attackerMod.m_Diff, DoubleBitsAsPointer(attackerMod.m_Diff),
            //     targetPercent, DoubleBitsAsPointer(targetPercent), targetDifference, DoubleBitsAsPointer(targetDifference)
            // )
            // L2_LOG_ERROR(Log,
            //     L"FloatERR [%s]->[%s]2 inval org_dmg:%.5f(%p) "
            //     L"actor(p:%.5f(%p), d:%.5f(%p)) "
            //     L"target(p:%.5f(%p), d:%.5f(%p))",
            //     attacker.GetName(), target.GetName(), damage, DoubleBitsAsPointer(damage), attackerMod.m_Per,
            //     DoubleBitsAsPointer(attackerMod.m_Per), attackerMod.m_Diff, DoubleBitsAsPointer(attackerMod.m_Diff),
            //     targetPercent, DoubleBitsAsPointer(targetPercent), targetDifference, DoubleBitsAsPointer(targetDifference)
            // )
            // L2_LOG_ERROR(Log,
            //     "Magic critical damage failed validation from '%s' to '%s': result %.17g (bits 0x%016llX), input damage
            //     %.17g (bits 0x%016llX), attacker multiplier %.17g (bits 0x%016llX), attacker offset %.17g (bits
            //     0x%016llX), target multiplier %.17g (bits 0x%016llX), target offset %.17g (bits 0x%016llX). Floating-point
            //     class 0x%04X. Reporting the stack and using damage 1.04.", WideToUtf8(attacker.GetName()).c_str(),
            //     WideToUtf8(target.GetName()).c_str(), result, static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(result))), damage, static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))), attackerMod.m_Per,
            //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Per))),
            //     attackerMod.m_Diff, static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Diff))), targetPercent,
            //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetPercent))),
            //     targetDifference, static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetDifference))),
            //     static_cast<unsigned>(FloatChecker::FPClass(result)))
            L2_LOG_ERROR(
                Log,
                "Magic critical damage failed validation from '%s' to '%s': result %.17g (bits 0x%016llX), input damage "
                "%.17g (bits 0x%016llX), attacker multiplier %.17g (bits 0x%016llX), attacker offset %.17g (bits "
                "0x%016llX), target multiplier %.17g (bits 0x%016llX), target offset %.17g (bits 0x%016llX); floating-point "
                "class 0x%04X. Reporting the stack and using damage 1.04.",
                Utf16ToUtf8(attacker.GetName()).c_str(), Utf16ToUtf8(target.GetName()).c_str(), result,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(result))), damage,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))),
                attackerMod.m_Per,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Per))),
                attackerMod.m_Diff,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Diff))),
                targetPercent,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetPercent))),
                targetDifference,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetDifference))),
                static_cast<unsigned>(FloatChecker::FPClass(result))
            );
        } else {
            // Log.Add(
            //     LOG_ERROR, L"FloatERR [%s]1 inval dmg:%.5f(%p) fpclass:%04x", attacker.GetName(), result,
            //     DoubleBitsAsPointer(result), FloatChecker::FPClass(result)
            // )
            // Noise: The following diagnostic includes the invalid critical damage, inputs and fallback in one record.
            // L2_LOG_ERROR(Log, L"FloatERR [%s]1 inval dmg:%.5f(%p) fpclass:%04x", attacker.GetName(), result,
            //     DoubleBitsAsPointer(result), FloatChecker::FPClass(result)
            // )
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s]2 inval org_dmg:%.5f(%p) "
            //     L"actor(p:%.5f(%p), d:%.5f(%p)) "
            //     L"target(p:%.5f(%p), d:%.5f(%p))",
            //     attacker.GetName(), damage, DoubleBitsAsPointer(damage), attackerMod.m_Per,
            //     DoubleBitsAsPointer(attackerMod.m_Per), attackerMod.m_Diff, DoubleBitsAsPointer(attackerMod.m_Diff),
            //     targetPercent, DoubleBitsAsPointer(targetPercent), targetDifference, DoubleBitsAsPointer(targetDifference)
            // )
            // L2_LOG_ERROR(Log,
            //     L"FloatERR [%s]2 inval org_dmg:%.5f(%p) "
            //     L"actor(p:%.5f(%p), d:%.5f(%p)) "
            //     L"target(p:%.5f(%p), d:%.5f(%p))",
            //     attacker.GetName(), damage, DoubleBitsAsPointer(damage), attackerMod.m_Per,
            //     DoubleBitsAsPointer(attackerMod.m_Per), attackerMod.m_Diff, DoubleBitsAsPointer(attackerMod.m_Diff),
            //     targetPercent, DoubleBitsAsPointer(targetPercent), targetDifference, DoubleBitsAsPointer(targetDifference)
            // )
            // L2_LOG_ERROR(Log,
            //     "Magic critical damage failed validation from '%s': result %.17g (bits 0x%016llX), input damage %.17g
            //     (bits 0x%016llX), attacker multiplier %.17g (bits 0x%016llX), attacker offset %.17g (bits 0x%016llX),
            //     target multiplier %.17g (bits 0x%016llX), target offset %.17g (bits 0x%016llX). Floating-point class
            //     0x%04X. Reporting the stack and using damage 1.04.", WideToUtf8(attacker.GetName()).c_str(), result,
            //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(result))), damage,
            //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))),
            //     attackerMod.m_Per, static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Per))), attackerMod.m_Diff,
            //     static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Diff))), targetPercent,
            //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetPercent))),
            //     targetDifference, static_cast<unsigned long
            //     long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetDifference))),
            //     static_cast<unsigned>(FloatChecker::FPClass(result)))
            L2_LOG_ERROR(
                Log,
                "Magic critical damage failed validation from '%s': result %.17g (bits 0x%016llX), input damage %.17g (bits "
                "0x%016llX), attacker multiplier %.17g (bits 0x%016llX), attacker offset %.17g (bits 0x%016llX), target "
                "multiplier %.17g (bits 0x%016llX), target offset %.17g (bits 0x%016llX); floating-point class 0x%04X. "
                "Reporting the stack and using damage 1.04.",
                Utf16ToUtf8(attacker.GetName()).c_str(), result,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(result))), damage,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))),
                attackerMod.m_Per,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Per))),
                attackerMod.m_Diff,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerMod.m_Diff))),
                targetPercent,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetPercent))),
                targetDifference,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetDifference))),
                static_cast<unsigned>(FloatChecker::FPClass(result))
            );
        }
        runtime::process::ReportCallStack(LOG_ERROR, false);
        result = 1.04;
    }
    return result;
}

double ApplyBonusToAttackDamage(double damage, CCreature& attacker, const CSkillInfo& skillInfo) {
    // Current L2Server 0x00a74c68.
    L2_GUARD_FUNCTION();
    const int magicType = skillInfo.m_nIsMagic;
    if (magicType == 0 || magicType == 6) {
        const CCreatureSkillMod::Mod& mod = attacker.m_SkillMod.m_Mods[CCreatureSkillMod::PSkillPower];
        damage = damage * mod.m_Per + mod.m_Diff;
    } else if (magicType == 1 || magicType == 7) {
        const CCreatureSkillMod::Mod& mod = attacker.m_SkillMod.m_Mods[CCreatureSkillMod::PSpellPower];
        damage = damage * mod.m_Per + mod.m_Diff;
    }
    return damage;
}

double GetSoulBonus(CCreature* const creature, const CSkillInfo* const skillInfo) {
    // Current L2Server 0x00a74f88.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && skillInfo != nullptr && creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user != nullptr) {
            return static_cast<double>(user->m_UsedSoulCount - skillInfo->m_nSoul) * 0.05 + 1.3;
        }
    }
    return 1.0;
}

double CalcHealingAmount(
    CCreature* const creature, const CSkillInfo* const skillInfo, CCreature* const target, const double power
) {
    // Current L2Server 0x00a80f90.
    if (target != nullptr && target->IsNPC()) {
        CNPC* const npc = target->CastNPC();
        if (npc != nullptr && (npc->m_nClassId == 0xf772f || npc->m_nClassId == 0xf7730)) {
            return 0.0;
        }
    }

    double magicAttack = 0.0;
    double spiritShotAttackBonus = 0.0;
    {
        L2_GUARD(*creature->m_lock);
        magicAttack = creature->m_data->m_dParam[VALIDATE::MATTACK];
        if (creature->m_data->m_bSpiritShot) {
            spiritShotAttackBonus = magicAttack * creature->m_data->m_dSpiritShotAttackBonusPer;
        }
    }

    const double healBonusDiff = creature->m_SkillMod.m_Mods[CCreatureSkillMod::HealBonus].m_Diff;
    const double targetHealBonus = target->m_SkillMod.m_Mods[CCreatureSkillMod::HealBonus].m_Per;
    const double magicLevel = skillInfo->m_nMagicLevel;
    const double baseLevelBonus = magicLevel * 0.0116 * magicLevel + magicLevel * 0.6371 + 6.4512;
    double attackPenalty = magicLevel * 0.007 * magicLevel * magicLevel -
        magicLevel * 0.00004 * magicLevel * magicLevel * magicLevel - magicLevel * 0.1923 * magicLevel +
        magicLevel * 3.6064 - 1.0966 - magicAttack;
    attackPenalty = std::max(attackPenalty, 0.0);
    const double adjustedLevelBonus = std::max(baseLevelBonus - attackPenalty / 5.0, 0.0);
    double amount = (std::sqrt(magicAttack + spiritShotAttackBonus) + power + healBonusDiff +
                        adjustedLevelBonus * creature->m_dSpiritShotHealBonus) *
        targetHealBonus;

    const double levelBonus = creature->LevelBonus();
    const double maximumBonus = (std::pow(2.71828, levelBonus) * creature->EquipedWeaponValidate(VALIDATE::MATTACK) *
                                    creature->MenBonusF() * 2.71828 / std::pow(levelBonus, 2.71828)) *
        levelBonus * levelBonus / 6.0;
    amount += std::min(amount, maximumBonus);

    const double criticalProbability = GetMagicCriticalProb(*creature, *target, skillInfo, creature->WitBonusF());
    if (genrand64_real1() * 100.0 <= criticalProbability) {
        amount *= 2.0;
        CCreature::SendMagicAttckInfo(creature, target, MagicAttackInfo::HealCritical);
        creature->SendSystemMessage(SystemMessageId::MAGIC_CRITICAL_HIT);
    }
    return amount;
}

void SendHpMpChangedSystemMessage(CCreature* const source, CCreature& target, const int amount, const int messageId) {
    // Current L2Server 0x00a74d4c.
    L2_GUARD_FUNCTION();
    if (!target.IsPC() || amount < 0) {
        return;
    }

    User* const targetUser = target.CastUser();
    CUserSocket* const targetSocket = targetUser->GetSocket();
    if (source == nullptr || source->GetIndex() == targetUser->GetIndex()) {
        targetSocket->Send("chccd", 0x62, messageId, 1, 1, amount);
        return;
    }

    if (source->IsPC()) {
        User* const sourceUser = source->CastUser();
        targetSocket->Send(
            "chccScd", 0x62, static_cast<std::int16_t>(messageId + 1), 2, 0x0c, sourceUser->GetName2(targetUser), 1, amount
        );
        return;
    }

    if (source->IsNPC()) {
        targetSocket->Send("chccdcd", 0x62, static_cast<std::int16_t>(messageId + 1), 2, 2, source->GetClassId(), 1, amount);
    }
}

double _CalcPysicalSkillCriticalBonus(CCreature& creature, const double specialProbability) {
    // Current L2Server 0x00a74020.
    L2_GUARD_FUNCTION();
    const CCreatureSkillMod::Mod& statMod = creature.m_SkillMod.m_Mods[CCreatureSkillMod::PhysicalSkillCriticalProbByStat];
    const CCreatureSkillMod::Mod& skillMod = creature.m_SkillMod.m_Mods[CCreatureSkillMod::PhysicalSkillCriticalProb];
    return creature.StrBonusF() * specialProbability * statMod.m_Per * skillMod.m_Per + skillMod.m_Diff;
}

double CalcPhysicalSkillCritcalBonus(
    CCreature& creature, const int skillId, const bool applyDexBonus, const double specialProbability
) {
    // Current L2Server 0x00a80b2c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(creature.m_SkillMod.m_lockSkillCritical);
        const auto found = creature.m_SkillMod.m_mapSkillCritical.find(skillId);
        if (found != creature.m_SkillMod.m_mapSkillCritical.end()) {
            return found->second;
        }
    }

    double probability = _CalcPysicalSkillCriticalBonus(creature, specialProbability);
    if (applyDexBonus) {
        probability *= creature.DexBonusF();
    }
    return probability;
}

double CalcPysicalSkillCriticalDamage(CCreature& creature, const double damage) {
    // Current L2Server 0x00a740ec.
    L2_GUARD_FUNCTION();
    const CCreatureSkillMod::Mod& mod = creature.m_SkillMod.m_Mods[CCreatureSkillMod::PhysicalSkillCriticalDamage];
    return damage * 2.0 * mod.m_Per + mod.m_Diff;
}

void CheckActionIsCritical(CSkillAction2* const action) {
    // Current L2Server 0x00a758d0.
    L2_GUARD_FUNCTION();
    action->bCritical = true;
}

double CalcCriticalPhysicalAttack(
    CCreature& creature, CSkillAction2* const action, double damage, const int specialProbability
) {
    // Current L2Server 0x00a816a0.
    L2_GUARD_FUNCTION();
    const int skillId = action != nullptr ? action->GetSkillId() : 0;
    const double probability =
        CalcPhysicalSkillCritcalBonus(creature, skillId, false, static_cast<double>(specialProbability));
    if (genrand64_real1() * 100.0 < probability) {
        creature.SendSystemMessageWithSelf(SystemMessageId::CHARACTER_CRITICAL_HIT);
        creature.TriggerAlterSkill(AlterSkill::Critical, ABNS_INVALID);
        if (action != nullptr) {
            CheckActionIsCritical(action);
        }
        damage = CalcPysicalSkillCriticalDamage(creature, damage);
    }
    return damage;
}

double CalculateDamage(const CalculateDamageParam& parameters, const bool useLevelBonus) {
    // Current L2Server 0x00a8271c.
    L2_GUARD_FUNCTION();
    if (parameters.pSkillInfo == nullptr) {
        return 0.0;
    }

    CCreature& attacker = *parameters.pSkillUser;
    CWorldObject& target = *parameters.pTarget;
    double physicalAttack = 0.0;
    {
        L2_GUARD(*attacker.m_lock);
        physicalAttack = attacker.m_data->m_dParam[VALIDATE::PATTACK];
    }

    CCreature* targetCreature = nullptr;
    double physicalDefence = 1.0;
    double shieldDefence = 0.0;
    double targetHp = 0.0;
    bool perfectShieldDefence = false;
    if (target.IsCreature()) {
        targetCreature = target.CastCreature();
        L2_GUARD(*targetCreature->m_lock);
        physicalDefence = targetCreature->GetPDefend();
        if (parameters.ignoreDefence == 2 || parameters.ignoreDefence == 3) {
            physicalDefence -= physicalDefence * static_cast<double>(parameters.physicalprob) / 100.0;
        }
        if (parameters.ignoreDefence == 0 || parameters.ignoreDefence == 3) {
            perfectShieldDefence = ApplyShieldDefense(attacker, targetCreature, 1.0, shieldDefence, true);
        }
        targetHp = targetCreature->m_data->m_dHP;
    } else if (target.IsStaticObject()) {
        CStaticObject* const staticObject = target.CastStaticObject();
        L2_GUARD(*staticObject->m_lock);
        physicalDefence = staticObject->GetPDefend();
        targetHp = staticObject->GetHP();
    }

    const double randomDamage = attacker.GetWeaponRandomDamage();
    double weaponTraitBonus = 1.0;
    double skillAttributeBonus = 1.0;
    if (targetCreature != nullptr) {
        weaponTraitBonus = targetCreature->GetWeaponTraitBonus(attacker.m_data->m_AttackType);
        weaponTraitBonus = std::clamp(weaponTraitBonus, 0.05, 2.0);
        skillAttributeBonus = attacker.GetSkillAttributeBonus(targetCreature, parameters.pSkillInfo, true);
    }

    double shotBonus = attacker.GetBonusAndTurnNextSoulShotState() + 1.0;
    if (shotBonus > 1.0) {
        shotBonus *= attacker.GetSoulShotExtraBonus() + 1.0;
    }
    const double soulBonus = parameters.bUseSoul ? GetSoulBonus(parameters.pSkillUser, parameters.pSkillInfo) : 1.0;

    double damage = 0.0;
    if (!perfectShieldDefence) {
        const AttackType attackType = attacker.GetAttackType();
        double power = parameters.power;
        double powerPercent = parameters.powerPercent;
        if (parameters.pSkillInfo->m_AffectScope != SAFS_SINGLE) {
            const double proportion = parameters.pSkillInfo->GetDamageProportionBonus(attacker);
            power *= proportion;
            powerPercent *= proportion;
        }

        const bool bowAttack = attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW;
        const double additionalPower = bowAttack ? power + physicalAttack : 0.0;

        if (attacker.IsNPC() && target.IsPC()) {
            CNPC* const npc = attacker.CastNPC();
            User* const targetUser = target.CastUser();
            if (npc != nullptr && targetUser != nullptr) {
                CCreatureSkillMod::BonusInfoInCondition* const bonus =
                    targetUser->m_SkillMod.GetPVEPhysicalDefenceBonus(npc->GetNPCId());
                if (bonus != nullptr) {
                    physicalDefence = (bonus->m_dAmountPer + 100.0) / 100.0 * physicalDefence + bonus->m_dAmountDiff;
                }
            }
        }
        if (targetCreature != nullptr) {
            physicalDefence *= attacker.m_SkillMod.m_dArmorPenetrateFactor;
            shieldDefence *= attacker.m_SkillMod.m_dShieldPenetrateFactor;
        }

        const double damageConstant = bowAttack ? 70.0 : 77.0;
        const double attackTerm =
            useLevelBonus ? attacker.LevelBonus() * powerPercent * physicalAttack : powerPercent * physicalAttack;
        damage = (attackTerm + power + additionalPower) * shotBonus * soulBonus * randomDamage * damageConstant /
            (shieldDefence + physicalDefence);
        damage *= weaponTraitBonus * skillAttributeBonus;

        if (target.IsPCOrSummon()) {
            damage *= attacker.m_SkillMod.m_dPvpPhysicalSkillBonusPer;
            User* const targetUser = target.GetUserOrMaster();
            if (targetUser != nullptr) {
                damage *= targetUser->m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer;
            }
        } else if (target.IsBoss()) {
            damage *= attacker.m_SkillMod.m_dPvBossPhysicalSkillBonusPer;
        } else {
            damage *= attacker.m_SkillMod.m_dPvePhysicalSkillBonusPer;
        }

        damage = CalcCriticalPhysicalAttack(attacker, parameters.pAction, damage, parameters.specialProb);
        damage = ApplyBonusToAttackDamage(damage, attacker, *parameters.pSkillInfo);
    }

    if (parameters.saveHP > 0 && targetHp - damage < static_cast<double>(parameters.saveHP)) {
        damage = targetHp - static_cast<double>(parameters.saveHP);
    }
    return damage;
}

double GetMagicResisted(const bool fullResist, const ResistanceInfo& info) {
    // Current L2Server 0x00a7cb14.
    L2_GUARD_FUNCTION();
    const double levelFactor = info.highLevel ? 2.64 : 7.0;
    const double victimStat = info.highLevel ? std::sqrt(info.dVictimWitBonus) : info.dVictimWitBonus * info.dVictimWitBonus;
    const double attackerStat =
        info.highLevel ? std::sqrt(info.dAttackerWitBonus) : info.dAttackerWitBonus * info.dAttackerWitBonus;

    const int constant = fullResist ? (info.highLevel ? -30 : -130) : (info.highLevel ? 0 : -100);
    const double penalty = info.bAttackerPenalty ? 160.0 : 0.0;
    std::int32_t levelDifference = info.nVictimLevel - info.nAttackerLevel;
    if (info.bPC2NPC && info.highLevel && !fullResist) {
        levelDifference = WrappingCubeQuarter(levelDifference);
    }

    const double result = (static_cast<double>(levelDifference) * levelFactor + static_cast<double>(constant) + penalty) *
            (victimStat / attackerStat) * info.dSkillBonusPer +
        info.dSkillBonusDiff;
    if (!(result >= 0.0)) {
        return 0.0;
    }
    return result <= 50.0 ? result : 50.0;
}

bool CheckIsMagicHit(CCreature* const attacker, CObject* const target) {
    // Current L2Server 0x00a7cca8.
    L2_GUARD_FUNCTION();
    if (attacker == nullptr || target == nullptr || !target->IsCreature()) {
        return true;
    }
    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr || targetCreature->m_data == nullptr) {
        return true;
    }

    double probability = ((targetCreature->m_data->m_dParam[VALIDATE::MAVOID] - 46.43804664723032) - attacker->GetMHit()) *
        -14782.327586206897;
    probability = std::max(probability, 0.0);
    probability = std::pow(probability, 1.0 / 3.0) + 7.63;
    probability = std::clamp(probability, 30.0, 100.0);
    return genrand64_real1() * 100.0 < probability;
}

double ApplyDamageToTarget(CCreature* const attacker, CObject* const target, double damage, const bool reflected) {
    // Current L2Server 0x00a7ce3c.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        if (target->IsStaticObject()) {
            return target->CastStaticObject()->GotDamageBy(attacker, damage);
        }
        return 0.0;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return 0.0;
    }

    double appliedDamage = 0.0;
    User* const targetUser = targetCreature->IsPC() ? targetCreature->CastUser() : nullptr;
    const bool shouldReflect = targetUser != nullptr && targetUser->m_SkillMod.m_dReflectDDPer > 0.0 &&
        genrand64_real1() * 100.0 <= targetUser->m_SkillMod.m_dReflectDDPer;
    if (!shouldReflect) {
        appliedDamage =
            targetCreature->GotDamageBy(attacker, damage, false, reflected ? DAMAGE_OVER_HIT : DAMAGE_SKILL, true);
    } else {
        damage = std::min(damage, targetUser->m_data->m_dParam[VALIDATE::MDEFEND] * 1.5);
        if (attacker != nullptr && attacker->m_SkillDurationMod.m_dReflectDDResistPer > 0.0) {
            const double factor = std::max((100.0 - attacker->m_SkillDurationMod.m_dReflectDDResistPer) / 100.0, 0.0);
            damage *= factor;
            if (static_cast<int>(damage) == 0) {
                return 0.0;
            }
        }
        appliedDamage = ApplyDamageToTarget(targetCreature, attacker, damage, reflected);
    }

    targetCreature->ApplyGotDamageBySkill(attacker, damage, appliedDamage);
    return appliedDamage;
}

bool CheckResisted(const bool fullResist, const ResistanceInfo& info) {
    // Current L2Server 0x00a7e8b0.
    L2_GUARD_FUNCTION();
    return genrand64_real1() * 100.0 <= GetMagicResisted(fullResist, info);
}

int CheckMagicResisted(
    CCreature& attacker,
    CObject& target,
    const MagicAttackerInfo& attackerInfo,
    const MagicTargetInfo& targetInfo,
    double,
    double,
    double& damage
) {
    // Current L2Server 0x00a7e994.
    L2_GUARD_FUNCTION();
    ResistanceInfo info{};
    info.highLevel = target.Level() > 77;
    info.nVictimLevel = targetInfo.m_Level;
    info.dVictimWitBonus = targetInfo.m_WitBonus;
    info.nAttackerLevel = attacker.Level();
    info.dAttackerWitBonus = attackerInfo.m_WitBonus;
    info.bAttackerPenalty = attackerInfo.m_Penalty;
    info.dSkillBonusPer = 1.0;
    info.dSkillBonusDiff = 0.0;
    info.bPC2NPC = attacker.IsPCOrPCRelatedNPC() && !target.IsPCOrPCRelatedNPC();
    if (info.bPC2NPC) {
        User* const user = attacker.GetUserOrMaster();
        if (user != nullptr) {
            info.nAttackerLevel = user->Level();
        }
    }
    if (target.IsCreature()) {
        info.dSkillBonusPer = target.CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDDResist].m_Per;
        info.dSkillBonusDiff = target.CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDDResist].m_Diff;
    }

    int result = 0;
    if (CheckResisted(true, info)) {
        damage = 1.0;
        result = 1;
    } else if (CheckResisted(false, info)) {
        damage *= 0.5;
        result = 2;
    } else {
        return 0;
    }

    CObject* const actualTarget = target.IsCreature() ? static_cast<CObject*>(targetInfo.m_pTargetCreature)
                                                      : static_cast<CObject*>(targetInfo.m_pTargetStaticObject);
    if (actualTarget == nullptr) {
        return result;
    }

    if (result == 1) {
        attacker.SendSystemMessage(SystemMessageId::CHARACTER_ATTACK_FAILED);
    } else {
        SendHalfResistMessageToAttacker(attacker, *actualTarget);
    }

    if (target.IsCreature() && actualTarget->GetUserOrMaster() != nullptr) {
        CCreature* const targetCreature = actualTarget->CastCreature();
        if (targetCreature != nullptr) {
            CCreature::SendMagicAttckInfo(&attacker, targetCreature, MagicAttackInfo::Resistance);
            SendResistMessageToTarget(attacker, *targetCreature, result == 1 ? 0x8ddU : 0x8dfU);
        }
    }
    return result;
}

double CalcFinalDamage(
    CCreature& attacker,
    CObject& target,
    const CSkillInfo* const skillInfo,
    const MagicAttackerInfo& attackerInfo,
    const MagicTargetInfo& targetInfo,
    const double power,
    const double distance,
    double damage,
    const double critical,
    const bool checkResist
) {
    // Current L2Server 0x00a7f578.
    if (!FloatChecker::CheckValidation(damage)) {
        const int fpClass = FloatChecker::FPClass(damage);
        const char16_t* const attackerName = attacker.GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval dmg:%.5f(%p) fpclass:%04x "
        //     L"skill(%d, %d) skillpwr:%.5f(%p)",
        //     attackerName, damage, DoubleBitsAsPointer(damage), fpClass,
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel, power, DoubleBitsAsPointer(power)
        // )
        // L2_LOG_ERROR(Log,
        //     L"FloatERR [%s] inval dmg:%.5f(%p) fpclass:%04x "
        //     L"skill(%d, %d) skillpwr:%.5f(%p)",
        //     attackerName, damage, DoubleBitsAsPointer(damage), fpClass,
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel, power, DoubleBitsAsPointer(power)
        // )
        // L2_LOG_ERROR(Log,
        //     "Initial magic damage failed validation for '%s', skill %d level %d: damage %.17g (bits 0x%016llX), skill
        //     power %.17g (bits 0x%016llX). Floating-point class 0x%04X. Reporting the stack and using damage 1.01.",
        //     attackerName == nullptr ? "<null>" : WideToUtf8(attackerName).c_str(),
        //     skillInfo == nullptr ? 0 : skillInfo->m_nSkillId,
        //     skillInfo == nullptr ? 0 : skillInfo->m_nLevel,
        //     damage, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))),
        //     power, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(power))),
        //     static_cast<unsigned>(fpClass))
        L2_LOG_ERROR(
            Log,
            "Initial magic damage failed validation for '%s', skill %d level %d: damage %.17g (bits 0x%016llX), skill power "
            "%.17g (bits 0x%016llX); floating-point class 0x%04X. Reporting the stack and using damage 1.01.",
            attackerName == nullptr ? "<null>" : Utf16ToUtf8(attackerName).c_str(),
            skillInfo == nullptr ? 0 : skillInfo->m_nSkillId, skillInfo == nullptr ? 0 : skillInfo->m_nLevel, damage,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))), power,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(power))),
            static_cast<unsigned>(fpClass)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        damage = 1.01;
    }

    const double previousDamage = damage;
    if (target.IsPC()) {
        User* const targetUser = target.GetUserOrMaster();
        damage *= attacker.m_SkillMod.m_dPvpMagicalSkillBonusPer;
        if (targetUser != nullptr) {
            damage *= targetUser->m_SkillMod.m_dPvpMagicalSkillDefenceBonusPer;
            if (!FloatChecker::CheckValidation(damage)) {
                const int fpClass = FloatChecker::FPClass(damage);
                static_cast<void>(targetUser->GetName());
                static_cast<void>(attacker.GetName());
                // Log.Add(
                //     LOG_ERROR,
                //     L"FloatERR [%s]->[%s]1 inval dmg:%.5f(%p) "
                //     L"fpclass:%04x skill(%d, %d)",
                //     firstAttackerName, firstTargetName, damage, DoubleBitsAsPointer(damage), fpClass,
                //     skillInfo->m_nSkillId, skillInfo->m_nLevel
                // )
                // Noise: The following diagnostic includes the failed damage, skill, inputs and fallback in one record.
                // L2_LOG_ERROR(Log,
                //     L"FloatERR [%s]->[%s]1 inval dmg:%.5f(%p) "
                //     L"fpclass:%04x skill(%d, %d)",
                //     firstAttackerName, firstTargetName, damage, DoubleBitsAsPointer(damage), fpClass,
                //     skillInfo->m_nSkillId, skillInfo->m_nLevel
                // )

                const double targetPvpDefence = targetUser->m_SkillMod.m_dPvpMagicalSkillDefenceBonusPer;
                const double attackerPvpBonus = attacker.m_SkillMod.m_dPvpMagicalSkillBonusPer;
                // const wchar_t* const secondTargetName = targetUser->GetName()
                const std::string secondTargetName = Utf16ToUtf8(targetUser->GetName());
                // const wchar_t* const secondAttackerName = attacker.GetName()
                const std::string secondAttackerName = Utf16ToUtf8(attacker.GetName());
                // Log.Add(
                //     LOG_ERROR,
                //     L"FloatERR [%s]->[%s]2 inval pwr:%.5f(%p) "
                //     L"prv_dmg:%.5f(%p) pvp_b(p:%.5f(%p) d:%.5f(%p))",
                //     secondAttackerName, secondTargetName, power, DoubleBitsAsPointer(power), previousDamage,
                //     DoubleBitsAsPointer(previousDamage), attackerPvpBonus, DoubleBitsAsPointer(attackerPvpBonus),
                //     targetPvpDefence, DoubleBitsAsPointer(targetPvpDefence)
                // )
                // L2_LOG_ERROR(Log,
                //     L"FloatERR [%s]->[%s]2 inval pwr:%.5f(%p) "
                //     L"prv_dmg:%.5f(%p) pvp_b(p:%.5f(%p) d:%.5f(%p))",
                //     secondAttackerName, secondTargetName, power, DoubleBitsAsPointer(power), previousDamage,
                //     DoubleBitsAsPointer(previousDamage), attackerPvpBonus, DoubleBitsAsPointer(attackerPvpBonus),
                //     targetPvpDefence, DoubleBitsAsPointer(targetPvpDefence)
                // )
                L2_LOG_ERROR(
                    Log,
                    "Magic damage failed validation after PvP bonuses from '%s' to '%s', skill %d level %d: damage %.17g "
                    "(bits 0x%016llX), skill power %.17g (bits 0x%016llX), input damage %.17g (bits 0x%016llX), observed "
                    "PvP attacker multiplier %.17g (bits 0x%016llX), observed PvP defence multiplier %.17g (bits "
                    "0x%016llX); floating-point class 0x%04X. Reporting the stack and using damage 1.02.",
                    secondAttackerName.c_str(), secondTargetName.c_str(),
                    skillInfo == nullptr ? 0 : skillInfo->m_nSkillId, skillInfo == nullptr ? 0 : skillInfo->m_nLevel, damage,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))), power,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(power))),
                    previousDamage,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(previousDamage))),
                    attackerPvpBonus,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerPvpBonus))),
                    targetPvpDefence,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(targetPvpDefence))),
                    static_cast<unsigned>(fpClass)
                );
                runtime::process::ReportCallStack(LOG_ERROR, false);
                damage = 1.02;
            }
        }
    } else {
        damage *= target.IsBoss() ? attacker.m_SkillMod.m_dPvBossMagicalSkillBonusPer
                                  : attacker.m_SkillMod.m_dPveMagicalSkillBonusPer;
        if (!FloatChecker::CheckValidation(damage)) {
            const int fpClass = FloatChecker::FPClass(damage);
            static_cast<void>(target.GetName());
            static_cast<void>(attacker.GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s]->[%s]1 inval dmg:%.5f(%p) "
            //     L"fpclass:%04x skill(%d, %d)",
            //     firstAttackerName, firstTargetName, damage, DoubleBitsAsPointer(damage), fpClass,
            //     skillInfo->m_nSkillId, skillInfo->m_nLevel
            // )
            // Noise: The following diagnostic includes the failed damage, skill, inputs and fallback in one record.
            // L2_LOG_ERROR(Log,
            //     L"FloatERR [%s]->[%s]1 inval dmg:%.5f(%p) "
            //     L"fpclass:%04x skill(%d, %d)",
            //     firstAttackerName, firstTargetName, damage, DoubleBitsAsPointer(damage), fpClass,
            //     skillInfo->m_nSkillId, skillInfo->m_nLevel
            // )

            const double attackerPveBonus = attacker.m_SkillMod.m_dPveMagicalSkillBonusPer;
            // const wchar_t* const secondTargetName = target.GetName()
            const std::string secondTargetName = Utf16ToUtf8(target.GetName());
            // const wchar_t* const secondAttackerName = attacker.GetName()
            const std::string secondAttackerName = Utf16ToUtf8(attacker.GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s]->[%s]2 inval pwr:%.5f(%p) "
            //     L"prv_dmg:%.5f(%p) pvp_b(p:%.5f(%p))",
            //     secondAttackerName, secondTargetName, power, DoubleBitsAsPointer(power), previousDamage,
            //     DoubleBitsAsPointer(previousDamage), attackerPveBonus, DoubleBitsAsPointer(attackerPveBonus)
            // )
            // L2_LOG_ERROR(Log,
            //     L"FloatERR [%s]->[%s]2 inval pwr:%.5f(%p) "
            //     L"prv_dmg:%.5f(%p) pvp_b(p:%.5f(%p))",
            //     secondAttackerName, secondTargetName, power, DoubleBitsAsPointer(power), previousDamage,
            //     DoubleBitsAsPointer(previousDamage), attackerPveBonus, DoubleBitsAsPointer(attackerPveBonus)
            // )
            L2_LOG_ERROR(
                Log,
                "Magic damage failed validation after target bonuses from '%s' to '%s', skill %d level %d: damage %.17g "
                "(bits 0x%016llX), skill power %.17g (bits 0x%016llX), input damage %.17g (bits 0x%016llX), observed PvE "
                "multiplier %.17g (bits 0x%016llX); floating-point class 0x%04X. Reporting the stack and using damage 1.02.",
                secondAttackerName.c_str(), secondTargetName.c_str(),
                skillInfo == nullptr ? 0 : skillInfo->m_nSkillId, skillInfo == nullptr ? 0 : skillInfo->m_nLevel, damage,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))), power,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(power))),
                previousDamage,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(previousDamage))),
                attackerPveBonus,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(attackerPveBonus))),
                static_cast<unsigned>(fpClass)
            );
            runtime::process::ReportCallStack(LOG_ERROR, false);
            damage = 1.02;
        }
    }

    if (checkResist) {
        CheckMagicResisted(
            attacker, target, attackerInfo, targetInfo, static_cast<double>(skillInfo->m_nMagicLevel), power, damage
        );
    }
    if (distance < 0.0) {
        damage *= -1.0;
        damage *= distance;
    }

    const double randomDamage = attacker.GetWeaponRandomDamage();
    const double criticalDamage = damage * critical;
    double finalDamage = randomDamage * criticalDamage;
    if (!FloatChecker::CheckValidation(finalDamage)) {
        const int fpClass = FloatChecker::FPClass(finalDamage);
        static_cast<void>(attacker.GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]1 inval dmg:%.5f(%p) fpclass:%04x "
        //     L"skill(%d, %d)",
        //     firstAttackerName, finalDamage, DoubleBitsAsPointer(finalDamage), fpClass,
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel
        // )
        // Noise: The following diagnostic includes the failed final damage, actual calculation inputs and fallback in one
        // record. L2_LOG_ERROR(Log,
        //     L"FloatERR [%s]1 inval dmg:%.5f(%p) fpclass:%04x "
        //     L"skill(%d, %d)",
        //     firstAttackerName, finalDamage, DoubleBitsAsPointer(finalDamage), fpClass,
        //     skillInfo->m_nSkillId, skillInfo->m_nLevel
        // )

        static_cast<void>(attacker.GetWeaponRandomDamage());
        static_cast<void>(attacker.GetWeaponRandomDamage());
        // const wchar_t* const secondAttackerName = attacker.GetName()
        const std::string secondAttackerName = Utf16ToUtf8(attacker.GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]2 inval pwr:%.5f(%p) prv_dmg:%.5f(%p) "
        //     L"crit:%.5f(%p) rand_dmg:%.5f(%p)",
        //     secondAttackerName, power, DoubleBitsAsPointer(power), damage, DoubleBitsAsPointer(damage), critical,
        //     DoubleBitsAsPointer(critical), randomDamageValue, DoubleBitsAsPointer(randomDamageBits)
        // )
        // Diagnostic accuracy: randomDamage is the multiplier used in the calculation. Preserve the two later original
        // GetWeaponRandomDamage calls and their ordering, even though their resampled values no longer label the applied
        // multiplier. L2_LOG_ERROR(Log,
        //     L"FloatERR [%s]2 inval pwr:%.5f(%p) prv_dmg:%.5f(%p) "
        //     L"crit:%.5f(%p) rand_dmg:%.5f(%p)",
        //     secondAttackerName, power, DoubleBitsAsPointer(power), damage, DoubleBitsAsPointer(damage), critical,
        //     DoubleBitsAsPointer(critical), randomDamageValue, DoubleBitsAsPointer(randomDamageBits)
        // )
        L2_LOG_ERROR(
            Log,
            "Final magic damage failed validation for '%s', skill %d level %d: final damage %.17g (bits 0x%016llX), skill "
            "power %.17g (bits 0x%016llX), input damage %.17g (bits 0x%016llX), critical multiplier %.17g (bits 0x%016llX), "
            "applied random multiplier %.17g (bits 0x%016llX); floating-point class 0x%04X. Reporting the stack and using "
            "damage 1.03.",
            secondAttackerName.c_str(), skillInfo == nullptr ? 0 : skillInfo->m_nSkillId,
            skillInfo == nullptr ? 0 : skillInfo->m_nLevel, finalDamage,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(finalDamage))), power,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(power))), damage,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(damage))), critical,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(critical))), randomDamage,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(DoubleBitsAsPointer(randomDamage))),
            static_cast<unsigned>(fpClass)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        finalDamage = 1.03;
    }

    if (critical == 1.0) {
        return finalDamage;
    }
    if (critical > 1.0) {
        return ApplyMagicCriticalToDamage(finalDamage, attacker, target);
    }
    return 1.0;
}

double GetMagicCriticalProb(CCreature& attacker, CObject& target, const CSkillInfo* const skillInfo, const double witBonus) {
    // Current L2Server 0x00a80cbc.
    L2_GUARD_FUNCTION();
    if (skillInfo != nullptr) {
        L2_GUARD(attacker.m_SkillMod.m_lockSkillCritical);
        const auto found = attacker.m_SkillMod.m_mapSkillCritical.find(skillInfo->m_nSkillId);
        if (found != attacker.m_SkillMod.m_mapSkillCritical.end()) {
            return static_cast<double>(found->second);
        }
    }

    double targetPercent = 1.0;
    double targetDifference = 0.0;
    if (target.IsCreature()) {
        CCreature* const targetCreature = target.CastCreature();
        if (targetCreature != nullptr) {
            const CCreatureSkillMod::Mod& mod =
                targetCreature->m_SkillMod.m_Mods[CCreatureSkillMod::MagicDefenceCriticalProb];
            if (mod.m_Diff != 0.0 || mod.m_Per != 1.0) {
                targetPercent = mod.m_Per;
                targetDifference = mod.m_Diff;
            }
        }
    }

    double base = static_cast<double>(attacker.m_data->m_nOrgParam[VALIDATE::MCRITICAL]);
    if (skillInfo != nullptr && skillInfo->m_MagicCriticalRate != 0.0) {
        base += skillInfo->m_MagicCriticalRate;
    }
    const CCreatureSkillMod::Mod& attackerMod = attacker.m_SkillMod.m_Mods[CCreatureSkillMod::MagicCriticalProb];
    double probability = targetPercent * attackerMod.m_Per * base * witBonus + targetDifference + attackerMod.m_Diff;

    const int attackerLevel = attacker.Level();
    if (attackerLevel < 78 && target.Level() < 78) {
        return std::clamp(probability, 0.5, 50.0);
    }
    probability +=
        std::sqrt(static_cast<double>(attackerLevel)) * static_cast<double>(attackerLevel - target.Level()) / 25.0;
    return std::clamp(probability, 0.5, 32.0);
}

double CalculateProbability(const double probability, CCreature* creature, CCreature* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a746b0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || skillInfo == nullptr) {
        return 0.0;
    }

    return (static_cast<double>(skillInfo->m_nMagicLevel) + probability - static_cast<double>(target->Level()) + 30.0 -
               static_cast<double>(target->m_data->m_Stat.m_N[0])) *
        creature->GetSkillAttributeBonus(target, skillInfo, false);
}

double CalculateProbability(const double probability, CCreature* creature, CNPC* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a74800.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || skillInfo == nullptr) {
        return 0.0;
    }

    return (static_cast<double>(skillInfo->m_nMagicLevel) + probability - static_cast<double>(target->Level()) + 30.0 -
               static_cast<double>(target->m_data->m_Stat.m_N[0])) *
        creature->GetSkillAttributeBonus(target, skillInfo, false);
}

double GetMagicCritical(CCreature& attacker, CObject& target, const CSkillInfo* const skillInfo, const double witBonus) {
    // Current L2Server 0x00a817f4.
    L2_GUARD_FUNCTION();
    if (skillInfo != nullptr) {
        const int magicType = skillInfo->m_nIsMagic;
        if (magicType == 0 || magicType == 6 || (magicType >= 10 && magicType <= 13)) {
            return 1.0;
        }
    }

    const double probability = GetMagicCriticalProb(attacker, target, skillInfo, witBonus);
    if (genrand64_real1() * 100.0 < probability) {
        CCreature::SendMagicAttckInfo(&attacker, target.CastCreature(), MagicAttackInfo::Critical);
        attacker.SendSystemMessage(SystemMessageId::MAGIC_CRITICAL_HIT);
        return 2.0;
    }
    return 1.0;
}

double CalcFinalDamage(
    CCreature& attacker,
    CObject& target,
    const CSkillInfo* const skillInfo,
    const MagicAttackerInfo& attackerInfo,
    const MagicTargetInfo& targetInfo,
    const double power,
    const double distance,
    const double damage,
    const bool checkResist
) {
    // Current L2Server 0x00a81960.
    const double critical = GetMagicCritical(attacker, target, skillInfo, attackerInfo.m_WitBonus);
    return CalcFinalDamage(
        attacker, target, skillInfo, attackerInfo, targetInfo, power, distance, damage, critical, checkResist
    );
}

bool CalcAndApplyMagicAttackDamage(
    CCreature* const attacker,
    CObject* const target,
    const CSkillInfo* const skillInfo,
    const double distance,
    double power,
    const bool useSoul,
    const bool reflected,
    const bool forceHit
) {
    // Current L2Server 0x00a819fc.
    L2_GUARD_FUNCTION();
    if (!IsAttackable(target)) {
        return false;
    }
    if (!forceHit && !CheckIsMagicHit(attacker, target)) {
        if (attacker != nullptr) {
            CCreature::SendMagicAttckInfo(attacker, target->CastCreature(), MagicAttackInfo::Avoid);
        }
        return true;
    }

    if (attacker != nullptr && skillInfo != nullptr) {
        const MagicAttackerInfo attackerInfo = GetMagicAttackerInfo(*attacker);
        const MagicTargetInfo targetInfo = GetMagicTargetInfo(*attacker, *target, skillInfo);
        const double soulBonus = useSoul ? GetSoulBonus(attacker, skillInfo) : 1.0;
        if (skillInfo->m_AffectScope != SAFS_SINGLE) {
            power *= skillInfo->GetDamageProportionBonus(*attacker);
        }
        const double baseDamage = (std::sqrt(attackerInfo.m_MagicPower * soulBonus) * power * 91.0 / targetInfo.m_MDefend) *
            targetInfo.m_AttrResist;
        double damage = CalcFinalDamage(
            *attacker, *target, skillInfo, attackerInfo, targetInfo, power, distance, baseDamage, !reflected
        );
        damage = ApplyBonusToAttackDamage(damage, *attacker, *skillInfo);
        ApplyDamageToTarget(attacker, target, damage, reflected);
    }
    return true;
}

bool CalcAndApplyMagicAttackRangeDamage(
    CCreature* const attacker,
    CObject* const target,
    const CSkillInfo* const skillInfo,
    const double distance,
    const double power,
    const double percent,
    const bool reflected,
    const bool useSoul,
    const bool forceHit
) {
    // Current L2Server 0x00a81c3c.
    L2_GUARD_FUNCTION();
    if (!IsAttackable(target)) {
        return false;
    }
    if (!forceHit && !CheckIsMagicHit(attacker, target)) {
        if (attacker != nullptr) {
            CCreature::SendMagicAttckInfo(attacker, target->CastCreature(), MagicAttackInfo::Avoid);
        }
        return false;
    }

    if (attacker != nullptr && skillInfo != nullptr) {
        const double critical = GetMagicCritical(*attacker, *target, skillInfo, attacker->GetMHit());
        double shieldDefense = 0.0;
        double damage = 1.0;
        if (!ApplyShieldDefense(*attacker, target, critical, shieldDefense, true)) {
            const MagicAttackerInfo attackerInfo = GetMagicAttackerInfo(*attacker);
            const MagicTargetInfo targetInfo = GetMagicTargetInfo(*attacker, *target, skillInfo);
            const double soulBonus = useSoul ? GetSoulBonus(attacker, skillInfo) : 1.0;
            const double baseDamage = (std::sqrt(attackerInfo.m_MagicPower * soulBonus) * power * 91.0 /
                                          (shieldDefense * percent / 100.0 + targetInfo.m_MDefend)) *
                targetInfo.m_AttrResist;
            damage = CalcFinalDamage(
                *attacker, *target, skillInfo, attackerInfo, targetInfo, power, distance, baseDamage, critical, true
            );
            damage = ApplyBonusToAttackDamage(damage, *attacker, *skillInfo);
        }
        ApplyDamageToTarget(attacker, target, damage, reflected);
    }
    return true;
}

bool IsAttackable(CObject* object) {
    // Current L2Server 0x00a739c4.
    if (object == nullptr) {
        return false;
    }
    if (object->IsCreature()) {
        CCreature* const creature = object->CastCreature();
        return creature != nullptr && creature->IsActive();
    }
    return object->IsStaticObject() || object->IsHolyThing() || object->IsChair() || object->IsTeamBattleFlag();
}

double IncreaseCP(CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount, int reason) {
    // Current L2Server 0x00a754e4.
    L2_GUARD_FUNCTION();
    if (changeType == SFCT_PER) {
        return creature->IncreaseCP_per(amount, reason, objectId);
    }
    if (changeType == SFCT_DIFF) {
        return creature->IncreaseCP_diff(amount, reason, objectId, false);
    }
    return 0.0;
}

double DecreaseCP(CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount, int reason) {
    // Current L2Server 0x00a755dc.
    L2_GUARD_FUNCTION();
    if (changeType == SFCT_PER) {
        return creature->DecreaseCP_per(amount, reason, objectId);
    }
    if (changeType == SFCT_DIFF) {
        return creature->DecreaseCP_diff(amount, reason, objectId);
    }
    return 0.0;
}

double IncreaseHP(
    CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount, int reason, bool broadcast
) {
    // Current L2Server 0x00a756d0.
    L2_GUARD_FUNCTION();
    if (changeType == SFCT_PER) {
        return creature->IncreaseHP_per(amount, broadcast, reason, objectId);
    }
    if (changeType == SFCT_DIFF) {
        return creature->IncreaseHP_diff(amount, broadcast, reason, objectId, false);
    }
    return 0.0;
}

double IncreaseMP(CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount) {
    // Current L2Server 0x00a757dc.
    L2_GUARD_FUNCTION();
    if (changeType == SFCT_PER) {
        return creature->IncreaseMP_per(amount, 3, objectId);
    }
    if (changeType == SFCT_DIFF) {
        return creature->IncreaseMP_diff(amount, 3, objectId, false);
    }
    return 0.0;
}

void CancelAllActionForMove(CCreature* creature) {
    // Current L2Server 0x00a7f39c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    User* const user = creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        CFishingController* const fishing = user->m_pFishingController;
        if (fishing != nullptr && (fishing->m_FishingState != FISHINGS_NONE || fishing->m_bIsAutoFishing == true)) {
            user->StopMove();
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
            user->GetController()->DoAutoFishing(false);
        }
        if (user->m_data->m_StopMode == STP_STORE) {
            user->StopStore(true, false);
        }
    }

    if (creature->m_data->m_StopMode == STP_SIT) {
        creature->GetController()->Stand(0);
    }
    creature->DoNothing();
    // creature->ActionFailed(0)
    creature->ActionFailed(AF_DEFAULT);
}

void BlockAction(CCreature* creature, bool success) {
    // Current L2Server 0x00a7fab4.
    creature->m_SkillMod.m_bCanAct = false;
    creature->m_data->m_bCanAct = 0;
    creature->GetController()->DoNothing(success);
    if (creature->IsNPC()) {
        creature->CastNPC()->ParamChanged(NPCT_BLOCKACTION);
    } else if (creature->IsPC()) {
        creature->ParamChanged(PCB_CHAR_SKILL);
    } else {
        creature->ParamChanged();
    }
}

} // namespace L2SkillFunc
