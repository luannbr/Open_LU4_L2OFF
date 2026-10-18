#include <optional>
#include "AirShip.h"
#include "BattleCamp.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CFortressFlagpole.h"
#include "CWorld.h"
#include "Castle.h"
#include "Chair.h"
#include "Collection.h"
#include "Creature.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Dominion.h"
#include "Door.h"
#include "EventController.h"
#include "Fishing.h"
#include "GameEventSystem.h"
#include "HolyThing.h"
#include "Item.h"
#include "L2Time.h"
#include "Manor.h"
#include "NPC.h"
#include "NewAction.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OwnThing.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "SharedContainer.h"
#include "SignBoard.h"
#include "SkillFxFunc.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "Trap.h"
#include "User.h"
#include "UserBasicAction.h"
#include "UserBookmark.h"
#include "UserSocket.h"
#include "WorldCollision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

namespace {


constexpr double kReuseSkillProbabilityEpsilon = 9.999999747378752e-06;

bool IsInRebalanceRange(const CCreature& source, const CCreature& target) {
    const FVector sourcePosition = source.GetPos();
    const FVector targetPosition = target.GetPos();
    const double x = targetPosition.x - sourcePosition.x;
    const double y = targetPosition.y - sourcePosition.y;
    const double z = targetPosition.z - sourcePosition.z;
    return x * x + y * y + z * z < 1000000.0;
}

bool RebalanceHpTotalOverflow(const wchar_t* format, double hpSum, double hpMaxSum) {
    if (hpSum <= 1000000.0 && hpMaxSum <= 1000000.0) {
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     format,
    //     static_cast<int>(hpSum),
    //     hpSum,
    //     static_cast<int>(hpMaxSum),
    //     hpMaxSum)
    // L2_LOG_ERROR(Log,
    //     format,
    //     static_cast<int>(hpSum),
    //     hpSum,
    //     static_cast<int>(hpMaxSum),
    //     hpMaxSum)
    L2_LOG_ERROR(
        Log,
        "%s stopped: HP total %.17g and maximum HP total %.17g fail the <= 1000000 checks; earlier HP changes are retained.",
        format == nullptr ? "HP redistribution" : WideToUtf8(format).c_str(), hpSum, hpMaxSum
    );
    return true;
}

bool ShouldNotifyPhysicalShieldDefence(const CSkillInfo* skillInfo, bool infected) {
    if (skillInfo == nullptr || !infected) {
        return true;
    }

    switch (skillInfo->m_OperateType) {
        case SKILL_DURATION:
        case SKILL_DURATION_LEVEL_UP:
        case SKILL_SELFDURATION:
        case SKILL_CHARGE_DURATION:
        case SKILL_AURA_ACTIVITY:
        case SKILL_BACK_STEP_DURATION:
        case SKILL_AURA_ACTIVITY2:
        case SKILL_PARTY_INSTANT:
        case SKILL_DUAL_PUMP1:
        case SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK:
            return false;
        default:
            return true;
    }
}

} // namespace

namespace SkillReuseHelper {

namespace {

int SignExtendInt8(const int value) noexcept {
    const std::uint8_t bits = static_cast<std::uint8_t>(value);
    std::int8_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return static_cast<int>(result);
}

int SignExtendInt16(const int value) noexcept {
    const std::uint16_t bits = static_cast<std::uint16_t>(value);
    std::int16_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return static_cast<int>(result);
}

} // namespace

void SendSystemMsg(User& user, const int seconds, int secondMessage, int minuteMessage, int hourMessage) {
    // Current L2Server 0x00a8c8a0.
    L2_GUARD_FUNCTION();
    if (seconds < 61) {
        if (hourMessage == 0 && secondMessage > 0) {
            hourMessage = secondMessage + 2;
        }
        user.GetSocket()->Send("chccc", 0x62, SignExtendInt16(hourMessage), 1, 0x14, 0);
        return;
    }

    if (seconds < 3601) {
        const int minutes = SignExtendInt8(seconds / 60);
        if (minuteMessage == 0 && secondMessage > 0) {
            minuteMessage = secondMessage + 1;
        }
        user.GetSocket()->Send(
            "chccccc", 0x62, SignExtendInt16(minuteMessage), 2, 0x14, minutes, 0x14,
            SignExtendInt8(SignExtendInt8(seconds) - minutes * 60)
        );
        return;
    }

    if (secondMessage != 0) {
        const int remainder = seconds % 3600;
        const int minutes = SignExtendInt8(remainder / 60);
        user.GetSocket()->Send(
            "chcchcccc", 0x62, SignExtendInt16(secondMessage), 3, 0x15, SignExtendInt16(seconds / 3600), 0x14, minutes, 0x15,
            SignExtendInt8(SignExtendInt8(remainder) - minutes * 60)
        );
    }
}

} // namespace SkillReuseHelper

namespace {

constexpr int kAttributeWeaponMaximum[9][2]{
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

constexpr int kAttributeArmorMaximum[9][2]{
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

void ResetAttributeEnchantFromEffect(User& user, unsigned int messageId) {
    user.SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
    runtime::raw_atomic::ExchangeI32(&user.m_lWaitVariation, 0);
    user.SendSystemMessage(messageId);
    user.GetSocket()->Send("chd", 0xfe, 0x62, 2);
    user.nAttributeCount = 0;
    user.nAttributeFailCount = 0;
}

int SelectEnchantBonusLevel(
    double roll, int level1, int level2, int level3, double probability1, double probability2, double probability3
) {
    int result = 0;
    if (roll <= probability1) {
        result = level1;
    } else if (roll <= probability1 + probability2) {
        result = level2;
    } else if (roll <= probability1 + probability2 + probability3) {
        result = level3;
    }
    return std::max(result, 0);
}

} // namespace

bool CSkillEffect_i_soul_blow::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
) {
    // Current L2Server 0x00a949b4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }
    return ApplyBlowEffect(creature, target, skillInfo, skillAction, distance, true);
}

bool CSkillEffect_i_Enchant_Weapon::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a87f44.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    if (!target->IsItem() || enchantItem == nullptr) {
        return false;
    }
    enchantItem->EnchantItem(static_cast<CItem*>(target), user, m_Type, m_Value, 1);
    return true;
}

bool CSkillEffect_i_Enchant_Armor::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8803c.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    if (!target->IsItem() || enchantItem == nullptr) {
        return false;
    }
    enchantItem->EnchantItem(static_cast<CItem*>(target), user, m_Type, m_Value, 1);
    return true;
}

bool CSkillEffect_i_Enchant_Weapon_Rate::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a88b60.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (!target->IsItem() || !target->IsWeapon() || enchantItem == nullptr || probabilityItem == nullptr) {
        return false;
    }

    user->m_EnchantBonusOptions[0] = std::max(m_nRate, 0);
    return true;
}

bool CSkillEffect_i_Enchant_Armor_Rate::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a88c64.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (!target->IsItem() || !target->IsArmor() || enchantItem == nullptr || probabilityItem == nullptr) {
        return false;
    }

    user->m_EnchantBonusOptions[0] = std::max(m_nRate, 0);
    return true;
}

bool CSkillEffect_i_Enchant_Item_MultiLevel::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a88134.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    if (!target->IsItem() || enchantItem == nullptr) {
        return false;
    }

    const double roll = ::genrand64_real1() * 100.0;
    int enchantLevel = 1;
    if (roll > m_Level1Prob) {
        if (roll <= m_Level1Prob + m_Level2Prob) {
            enchantLevel = 2;
        } else if (roll <= m_Level1Prob + m_Level2Prob + m_Level3Prob) {
            enchantLevel = 3;
        }
    }
    enchantItem->EnchantItem(static_cast<CItem*>(target), user, m_Type, m_Value, enchantLevel);
    return true;
}

bool CSkillEffect_i_Enchant_Attribute::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8827c.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT);
    if (!target->IsItem() || enchantItem == nullptr) {
        ResetAttributeEnchantFromEffect(*user, 0x861);
        return false;
    }

    CItem* const targetItem = static_cast<CItem*>(target);
    if (target->IsWeapon()) {
        const AttributeTypeEnum currentType = static_cast<AttributeTypeEnum>(targetItem->m_itemBaseAttribute.m_nAttackType);
        if ((currentType != ATTRT_NONE && currentType != m_eAttackAttrType)) {
            ResetAttributeEnchantFromEffect(*user, 0x866);
            return false;
        }
        if (targetItem->m_itemBaseAttribute.m_nAttackValue >= kAttributeWeaponMaximum[m_nGrade - 1][1]) {
            ResetAttributeEnchantFromEffect(*user, 0x861);
            return false;
        }
        enchantItem->EnchantAttributeItemWeapon(
            targetItem, user, m_eAttackAttrType, m_nGrade, m_nAdditionalProb, m_nAdditionalValue
        );
        return true;
    }

    if (target->IsArmor()) {
        const int opposite = CItem::GetOppositeAttribute(m_eDefendAttrType);
        if (targetItem->m_itemBaseAttribute.m_nDefendValue[opposite] > 0) {
            ResetAttributeEnchantFromEffect(*user, 0xc2d);
            return false;
        }
        if (targetItem->m_itemBaseAttribute.m_nDefendValue[m_eDefendAttrType] >= kAttributeArmorMaximum[m_nGrade - 1][1]) {
            ResetAttributeEnchantFromEffect(*user, 0x861);
            return false;
        }
        enchantItem->EnchantAttributeItemArmor(
            targetItem, user, m_eDefendAttrType, m_nGrade, m_nAdditionalProb, m_nAdditionalValue
        );
        return true;
    }

    ResetAttributeEnchantFromEffect(*user, 0x861);
    return false;
}

bool CSkillEffect_i_Enchant_Attribution_Item::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a886e4.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT);
    if (!target->IsItem() || enchantItem == nullptr) {
        ResetAttributeEnchantFromEffect(*user, 0x861);
        return false;
    }

    CItem* const targetItem = static_cast<CItem*>(target);
    if (target->IsWeapon()) {
        const AttributeTypeEnum currentType = static_cast<AttributeTypeEnum>(targetItem->m_itemBaseAttribute.m_nAttackType);
        if ((currentType != ATTRT_NONE && currentType != m_eAttackAttrType)) {
            ResetAttributeEnchantFromEffect(*user, 0x866);
            return false;
        }
        if (targetItem->m_itemBaseAttribute.m_nAttackValue >= kAttributeWeaponMaximum[m_nGrade - 1][1]) {
            ResetAttributeEnchantFromEffect(*user, 0x861);
            return false;
        }
        enchantItem->EnchantAttributeItemWeapon(
            targetItem, user, m_eAttackAttrType, m_nGrade, m_nAdditionalProb, m_nAdditionalValue
        );
        return true;
    }

    if (target->IsArmor()) {
        const int opposite = CItem::GetOppositeAttribute(m_eDefendAttrType);
        if (targetItem->m_itemBaseAttribute.m_nDefendValue[opposite] > 0) {
            ResetAttributeEnchantFromEffect(*user, 0xc2d);
            return false;
        }
        if (targetItem->m_itemBaseAttribute.m_nDefendValue[m_eDefendAttrType] >= kAttributeArmorMaximum[m_nGrade - 1][1]) {
            ResetAttributeEnchantFromEffect(*user, 0x861);
            return false;
        }
        enchantItem->EnchantAttributeItemArmor(
            targetItem, user, m_eDefendAttrType, m_nGrade, m_nAdditionalProb, m_nAdditionalValue
        );
        return true;
    }

    ResetAttributeEnchantFromEffect(*user, 0x861);
    return false;
}

bool CSkillEffect_i_add_enchant_weapon_level_inc::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a88d68.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (!target->IsItem() || !target->IsWeapon() || enchantItem == nullptr || probabilityItem == nullptr) {
        return false;
    }

    user->m_EnchantBonusOptions[2] = SelectEnchantBonusLevel(
        ::genrand64_real1() * 100.0, m_nLevel1, m_nLevel2, m_nLevel3, m_Level1Prob, m_Level2Prob, m_Level3Prob
    );
    return true;
}

bool CSkillEffect_i_add_enchant_armor_level_inc::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a88ec0.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (!target->IsItem() || (!target->IsArmor() && !target->IsAccessary()) || enchantItem == nullptr ||
        probabilityItem == nullptr) {
        return false;
    }

    user->m_EnchantBonusOptions[2] = SelectEnchantBonusLevel(
        ::genrand64_real1() * 100.0, m_nLevel1, m_nLevel2, m_nLevel3, m_Level1Prob, m_Level2Prob, m_Level3Prob
    );
    return true;
}

bool CSkillEffect_i_sub_enchant_weapon_level_dec::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a89028.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (!target->IsItem() || !target->IsWeapon() || enchantItem == nullptr || probabilityItem == nullptr) {
        return false;
    }

    user->m_EnchantBonusOptions[1] = m_nSubLevel;
    return true;
}

bool CSkillEffect_i_sub_enchant_armor_level_dec::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a89128.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<User*>(creature);
    CItem* const enchantItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    CItem* const probabilityItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (!target->IsItem() || (!target->IsArmor() && !target->IsAccessary()) || enchantItem == nullptr ||
        probabilityItem == nullptr) {
        return false;
    }

    user->m_EnchantBonusOptions[1] = m_nSubLevel;
    return true;
}

bool CSkillEffect_i_change_face::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8f228.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (user != nullptr) {
        target->CastCreature()->StopMove();
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            user->m_data->m_nHairColor, // Current face field +0xb8.
            nType, 0x14
        );
        db.SendSaveCharacterInfo(user, false, true);
        user->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    }
    return user != nullptr;
}

bool CSkillEffect_i_change_hair_style::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8f380.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (user != nullptr) {
        target->CastCreature()->StopMove();
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            user->m_data->m_nFace, // Current hair-style field +0xb0.
            nType, 0x12
        );
        db.SendSaveCharacterInfo(user, false, true);
        user->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    }
    return user != nullptr;
}

bool CSkillEffect_i_change_hair_color::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8f4d8.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (user != nullptr) {
        target->CastCreature()->StopMove();
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            user->m_data->m_nHairShape, // Current hair-color field +0xb4.
            nType, 0x13
        );
        db.SendSaveCharacterInfo(user, false, true);
        user->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    }
    return user != nullptr;
}

bool CSkillEffect_i_change_class::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8d200.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || m_SubJobID < 0 || m_SubJobID >= 4) {
        return false;
    }

    User* const user = creature->CastUser();
    int classId = 0;
    switch (m_SubJobID) {
        case 0:
            classId = user->m_data->m_nSubJob0Class;
            break;
        case 1:
            classId = user->m_data->m_nSubJob1Class;
            break;
        case 2:
            classId = user->m_data->m_nSubJob2Class;
            break;
        case 3:
            classId = user->m_data->m_nSubJob3Class;
            break;
        default:
            break;
    }

    if (!user->CheckSubjobCommon(classId, true)) {
        return false;
    }
    if (user->IsPetSummoning()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::PET_SUMMON_UNSUMMON_OR_RIDING_IN_PROGRESS);
        return false;
    }

    user->m_nJobOnDespawnPetCompleteArgument = m_SubJobID;
    user->DespawnPet(true, true, DESPAWN_PET_SUBJOB_CHANGE, false);
    return true;
}

bool CSkillEffect_i_get_exp::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a92aa4.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }

    std::int64_t amount = nAmount;
    if (targetCreature->Level() < nStandardLevel) {
        amount = runtime::Win64CvttDoubleToInt64(nRatio / 100.0 * static_cast<double>(nAmount));
    }
    targetCreature->IncrementParam(PARAM_EXP, amount);

    if (targetCreature->IsPC()) {
        User* const user = targetCreature->CastUser();
        db.SendSaveCharacterInfo(user, false, true);
        targetCreature->CastUser()->SendUserInfo(nullptr, nullptr);
    }
    return true;
}

bool CSkillEffect_i_pk_count::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00aab1cc.
    L2_GUARD_FUNCTION();
    if (!target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    (void)CCursedWeaponMgr::inst();
    const bool hasCursedWeapon = user != nullptr && user->m_nCursedWeaponClassId > 0;
    if (hasCursedWeapon) {
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        CSharedCreatureData* const data = user->m_data;
        const int updatedPkCount =
            runtime::Int32FromBits(static_cast<std::uint32_t>(data->m_nPKCount) + static_cast<std::uint32_t>(nDelta));
        data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(data->m_nPKCount, updatedPkCount, 0x1e);
        if (data->m_nPKCount < 0) {
            data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(data->m_nPKCount, 0, 0x1e);
        }
    }

    db.SendSaveCharacterInfo(user, false, true);
    user->m_nParamChanged |= PCB_USER;
    return true;
}

bool CSkillEffect_i_agathion_energy::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8f630.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    CItem* const item = creature->m_inv.GetByIndex(creature->m_data->m_nSlotIndex[ST_LBRACELET]);
    if (item == nullptr) {
        return false;
    }
    if (item->m_const->m_dAgathionMaxEnergy <= 0.0) {
        creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_RECHARGE_ENERGY_REQUIREMENTS_NOT_MET);
        return false;
    }

    const double oldEnergy = item->GetAgathionEnergy();
    int newEnergy = 0;
    if (m_Sfct == SFCT_PER) {
        newEnergy = runtime::Win64CvttDoubleToInt32(oldEnergy * (m_dAmount + 100.0) / 100.0);
    } else if (m_Sfct == SFCT_DIFF) {
        newEnergy = runtime::Win64CvttDoubleToInt32(oldEnergy + m_dAmount);
    }

    item->SetAgathionEnergy(static_cast<double>(newEnergy));
    creature->CastUser()->SendBrAgathionEnergyInfo();
    db.RequestSaveItemAgathionEnergy(
        creature->CastUser(), static_cast<int>(item->m_data->m_nId), static_cast<int>(item->GetDBID()),
        item->GetAgathionEnergy()
    );

    const int gained = newEnergy - runtime::Win64CvttDoubleToInt32(oldEnergy);
    if (gained > 0) {
        creature->CastUser()->SendSystemMessage_d(SystemMessageId::ENERGY_RECHARGED, gained);
    }
    return true;
}

bool CSkillEffect_i_confuse::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa5078.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsNPC()) {
        return false;
    }

    CNPC* const targetNpc = static_cast<CNPC*>(target);
    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, targetNpc, skillInfo);
    if (genrand64_real1() * 100.0 >= probability) {
        creature->SendSystemMessage(SystemMessageId::CONFUSION_FAILED);
        return false;
    }

    class TLMFilter final : public CTLMFilterDist {
    public:
        TLMFilter(const int inZoneId, const FVector& position, const int excludedTargetId) :
            CTLMFilterDist(inZoneId, position, 500.0), m_excludedTargetId(excludedTargetId) {}

        bool CanDo(CObject* object) override {
            if (!CTLMFilterDist::CanDo(object)) {
                return false;
            }
            CCreature* const candidate = object->CastCreature();
            return candidate != nullptr && candidate->IsAlive() &&
                static_cast<int>(candidate->m_nId) != m_excludedTargetId && !candidate->m_data->m_bIsFakeDeath &&
                (!candidate->IsPC() || !candidate->IsGMInvisible());
        }

    private:
        int m_excludedTargetId;
    };

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(std::vector<CSafePointer2<CCreature>>& targets) : m_targets(targets) {}

        bool CanDo(CObject* object, double) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                m_targets.push_back(CSafePointer2<CCreature>(object->CastCreature()));
            }
            return true;
        }

    private:
        std::vector<CSafePointer2<CCreature>>& m_targets;
    };

    std::vector<CSafePointer2<CCreature>> targets;
    TLMFilter filter(
        static_cast<int>(creature->GetInZoneID()), targetNpc->m_data->m_Pos, static_cast<int>(targetNpc->m_nId)
    );
    TLMFunc collector(targets);
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &collector);

    if (!targets.empty()) {
        const int random = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 1000.0 + 1.0);
        const int index = random % static_cast<int>(targets.size());
        CSPointer<CCreature> selected = runtime::CheckedVectorAt(targets, index).FindObjectSP();
        CCreature* const selectedCreature = selected ? selected->CastCreature() : nullptr;
        selected.Release();
        if (selectedCreature != nullptr && selectedCreature->IsCreature()) {
            g_npcserver.Send(
                "cddddd", 0x2c, static_cast<int>(targetNpc->m_nId), targetNpc->GetIndex(),
                static_cast<int>(selectedCreature->m_nId), selectedCreature->GetIndex(), nTime
            );
        }
    }
    return true;
}

bool CSkillEffect_i_distrust::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa5990.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || target == nullptr || !target->IsCreature()) {
        return false;
    }

    CNPC* const targetNpc = static_cast<CNPC*>(target);
    if (targetNpc->m_data->m_bUndying) {
        return false;
    }

    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, targetNpc, skillInfo);
    if (genrand64_real1() * 100.0 < probability) {
        class TLMFilter final : public CTLMFilterDist {
        public:
            TLMFilter(const int inZoneId, const FVector& position, const unsigned int excludedNpcId, CCreature* skillUser) :
                CTLMFilterDist(inZoneId, position, 500.0), m_excludedNpcId(excludedNpcId), m_skillUser(skillUser) {}

            bool CanDo(CObject* object) override {
                return CTLMFilterDist::CanDo(object) && object->IsAlive() && object->m_nId != m_excludedNpcId &&
                    !object->IsEnemyTo(m_skillUser);
            }

        private:
            unsigned int m_excludedNpcId;
            CCreature* m_skillUser;
        };

        class TLMFunc final : public CTLMFunc {
        public:
            explicit TLMFunc(std::vector<CSafePointer2<CCreature>>& targets) : m_targets(targets) {}

            bool CanDo(CObject* object, double) override {
                if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                    m_targets.push_back(CSafePointer2<CCreature>(object->CastCreature()));
                }
                return true;
            }

        private:
            std::vector<CSafePointer2<CCreature>>& m_targets;
        };

        std::vector<CSafePointer2<CCreature>> targets;
        TLMFilter filter(static_cast<int>(creature->GetInZoneID()), targetNpc->m_data->m_Pos, targetNpc->m_nId, creature);
        TLMFunc collector(targets);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &collector);

        if (!targets.empty()) {
            const int random = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 1000.0 + 1.0);
            const int index = random % static_cast<int>(targets.size());
            CCreature* const selectedCreature = runtime::CheckedVectorAt(targets, index).FindObjectSP()->CastCreature();
            if (selectedCreature != nullptr && selectedCreature->IsCreature()) {
                g_npcserver.Send(
                    "cddddd", 0x2c, static_cast<int>(targetNpc->m_nId), targetNpc->GetIndex(),
                    static_cast<int>(selectedCreature->m_nId), selectedCreature->GetIndex(), nTime
                );
            }
        }
        creature->SendSystemMessage(SystemMessageId::ENEMY_CONFUSION_SUCCEEDED);
    }
    return true;
}

bool CSkillEffect_i_call_party::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00aa2400.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const caster = creature->CastUser();
    CParty* const party = caster->GetParty();
    if (party == nullptr) {
        return false;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        User::CallPCResult result = User::CPR_ETC;
        if (member == nullptr || member->GetServerId() == caster->GetServerId() ||
            !User::IsCallPCEnable(caster, member, false, result)) {
            continue;
        }

        {
            FOR_ALL_SUMMON_IN_SUMMON_LIST(
                member, summon,
                // current 0x00aa2400 creates in_code before the race test.
                if (summon->m_data->m_Race == RT_SIEGE_WEAPON) { member->DespawnSummon(summon, true, false); }
            );
        }

        if (member->m_data->m_TrainingState == 1) {
            creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
            continue;
        }

        member->DoNothing();
        const int z = runtime::Win64CvttDoubleToInt32(caster->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(caster->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(caster->GetPos().x);
        member->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::tst_i_call_party, nullptr, 0);
    }
    return true;
}

bool CSkillEffect_i_call_target_party::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa27ec.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const targetUser = target->CastUser();
    if (targetUser == nullptr || !L2Common::IsAlivePC(targetUser)) {
        return false;
    }

    CParty* const party = targetUser->GetParty();
    if (party != nullptr) {
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            User::CallPCResult result = User::CPR_ETC;
            if (member == nullptr || !User::IsCallPCEnable(targetUser, member, false, result)) {
                continue;
            }

            {
                FOR_ALL_SUMMON_IN_SUMMON_LIST(
                    member, summon,
                    // current 0x00aa27ec creates in_code before the race test.
                    if (summon->m_data->m_Race == RT_SIEGE_WEAPON) { member->DespawnSummon(summon, true, false); }
                );
            }

            if (member->m_data->m_TrainingState == 1) {
                creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
                continue;
            }

            member->DoNothing();
            const int z = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().z);
            const int y = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().y);
            const int x = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().x);
            member->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::tst_i_call_target_party, nullptr, 0);
        }
        return true;
    }

    {
        FOR_ALL_SUMMON_IN_SUMMON_LIST(
            targetUser, summon,
            // The second expansion at current 0x00aa27ec has the same
            // in_code timing.
            if (summon->m_data->m_Race == RT_SIEGE_WEAPON) { targetUser->DespawnSummon(summon, true, false); }
        );
    }

    if (targetUser->m_data->m_TrainingState == 1) {
        creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
    } else {
        targetUser->DoNothing();
        const int z = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().x);
        targetUser->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::tst_i_call_target_party, nullptr, 0);
    }
    return false;
}

bool CSkillEffect_i_Fishing_instantkill::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a92988.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    const int roll = static_cast<int>(::genrand64_real2() * 101.0 + 0.0);
    if (roll < user->m_SkillMod.m_nFishingInstantP + m_nProb) {
        user->m_pFishingController->m_nFishHP = 0;
        user->m_pFishingController->m_bInstantKillFlg = true;
        user->SendSystemMessage(SystemMessageId::LETHAL_STRIKE);
    }
    return true;
}

CSkillEffect_i_knockback::CSkillEffect_i_knockback(
    const int distance, const int speed, const int acceleration, const int mode
) :
    m_Dist(distance), m_Speed(speed), m_Acceleration(acceleration), m_Mode(mode == 1 ? FTLT_KNOCKDOWN : FTLT_KNOCKBACK) {
    // Current L2Server 0x00ca4c0c.
}

bool CSkillEffect_i_dispel_by_category::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8fc58. Ctor 0x00ca48f8.
    L2_GUARD_FUNCTION();
    const bool isCreature = target->IsCreature();
    if (isCreature) {
        static_cast<CCreature*>(target)->DispelByCategory(eDeBuffType, creature, nProb, skillInfo, nMaxSlot);
    }
    return isCreature;
}

bool CSkillEffect_i_fly_away::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a9b7ac. Ctors are compiled by parser actions
    // 0x1a8/0x1a9.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr || (target->IsNPC() && targetCreature->m_data->m_nExCrtEffect == 1) ||
        targetCreature->CheckBlockDebuff(creature)) {
        return false;
    }

    if (skillInfo != nullptr) {
        const SkillOperateType operateType = skillInfo->m_OperateType;
        const bool infectionRequired = operateType == SKILL_SELFDURATION || operateType == SKILL_DURATION ||
            operateType == SKILL_CHARGE_DURATION || operateType == SKILL_AURA_ACTIVITY ||
            operateType == SKILL_AURA_ACTIVITY2 || operateType == SKILL_DURATION_LEVEL_UP ||
            operateType == SKILL_BACK_STEP_DURATION || operateType == SKILL_PARTY_INSTANT ||
            operateType == SKILL_DUAL_PUMP1 || operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
        if (infectionRequired && skillAction != nullptr && !skillAction->m_Infected) {
            return false;
        }
    }

    const FVector targetPosition = targetCreature->GetPos();
    const FVector sourcePosition = creature->GetPos();
    FVector direction{
        targetPosition.x - sourcePosition.x, targetPosition.y - sourcePosition.y, targetPosition.z - sourcePosition.z};
    const double distance = direction.Size();

    if (target->IsPC()) {
        target->CastUser()->StopStore(true, false);
    }

    const FLY_TO_LOCATION_TYPE flyType = bIsCurve ? FTLT_PARABOLA : FTLT_STRAIGHT;
    CWorld& world = CWorld::Inst();
    for (int divisor = 1; divisor < 7; ++divisor) {
        const int movementDistance = nDist / divisor;
        FVector unit{1.0, 1.0, 1.0};
        if (distance <= -0.00001 || 0.00001 <= distance) {
            unit.x = direction.x / distance;
            unit.y = direction.y / distance;
            unit.z = direction.z / distance;
        }

        const double signedDistance = static_cast<double>(bIsPush ? movementDistance : -movementDistance);
        FVector destination{targetPosition.x + signedDistance * unit.x, targetPosition.y + signedDistance * unit.y,
            targetPosition.z + signedDistance * unit.z};
        const int verticalAllowance = 300 / divisor;
        destination.z = static_cast<double>(runtime::Win64CvttDoubleToInt32(targetPosition.z) + verticalAllowance);

        const int ground = static_cast<std::int16_t>(world.GetGeo()->FindGround(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z)
        ));
        const int targetZ = runtime::Win64CvttDoubleToInt32(targetPosition.z);
        if (ground < targetZ - verticalAllowance || ground > targetZ + verticalAllowance) {
            continue;
        }
        destination.z = static_cast<double>(ground);

        const bool floating =
            static_cast<int>(targetCreature->m_data->m_moveType) == CMT_FLY || targetCreature->m_AreaMod.m_bIsUnderWater;
        if (!world.FindAcceptableDestZPos(targetPosition, destination, floating)) {
            continue;
        }

        FVector collisionPosition{};
        if (g_WorldPlaneCollision.CheckCollision(
                targetPosition, destination, 0.0, collisionPosition, static_cast<int>(target->GetInZoneID()), false
            )) {
            direction.NormalizeCube();
            destination.x = collisionPosition.x - direction.x * 10.0;
            destination.y = collisionPosition.y - direction.y * 10.0;
            destination.z = collisionPosition.z - direction.z * 10.0;
        }

        if (!world.IsValidPosition(
                runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y)
            ) ||
            !world.GetGeo()->MoveStraightTest(
                targetPosition, destination, static_cast<double>(movementDistance), false, 0
            )) {
            continue;
        }

        targetCreature->GetController()->DoNothing(true);
        targetCreature->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), flyType, 0, 0, 0
        );
        world.MoveTo(targetCreature, destination, false);
        targetCreature->m_data->SetPos(destination);
        targetCreature->ValidateLocation();
        return true;
    }

    targetCreature->GetController()->DoNothing(true);
    targetCreature->BroadcastFlyToLocation(
        runtime::Win64CvttDoubleToInt32(targetPosition.x), runtime::Win64CvttDoubleToInt32(targetPosition.y),
        runtime::Win64CvttDoubleToInt32(targetPosition.z), flyType, 0, 0, 0
    );
    return true;
}

bool CSkillEffect_i_hp::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a86dbc.
    L2_GUARD_FUNCTION();
    return Instant(nullptr, target, skillInfo, nullptr, 0.0);
}

bool CSkillEffect_i_hp::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a86e88. Ctor 0x00ca3d48.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr || !targetCreature->m_SkillMod.CanGetDamageHP(creature, targetCreature)) {
        return false;
    }

    double amount = 0.0;
    if (m_Sfct == SFCT_PER) {
        amount = targetCreature->m_data->m_dOrgHP * m_dAmount / 100.0;
        amount = static_cast<double>(
            CSkillEffect_p_potion_bonus::CalcHealBonus(creature, runtime::Win64CvttDoubleToInt32(amount))
        );
    } else if (m_Sfct == SFCT_DIFF) {
        amount = static_cast<double>(
            CSkillEffect_p_potion_bonus::CalcHealBonus(creature, runtime::Win64CvttDoubleToInt32(m_dAmount))
        );
    }

    const unsigned int sourceId = creature == nullptr ? 0U : creature->m_nId;
    const double applied = L2SkillFunc::IncreaseHP(targetCreature, sourceId, SFCT_DIFF, amount, 1, true);

    if (!targetCreature->IsPC()) {
        return true;
    }

    User* const targetUser = targetCreature->CastUser();
    if (applied >= 0.0) {
        L2SkillFunc::SendHpMpChangedSystemMessage(
            creature, *targetCreature, runtime::Win64CvttDoubleToInt32(applied), SystemMessageId::HP_RESTORED
        );
        return true;
    }
    if (creature == nullptr) {
        return true;
    }

    CUserSocket* const targetSocket = targetUser->GetSocket();
    const int damage = runtime::Win64CvttDoubleToInt32(-applied);
    if (creature->IsNPC()) {
        targetSocket->Send(
            "chccdcd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_HP, 2, 2, creature->GetClassId(), 1, damage
        );
    } else if (creature->IsPC()) {
        User* const sourceUser = creature->CastUser();
        targetSocket->Send(
            "chccScd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_HP, 2, 0x0c, sourceUser->GetName2(targetUser), 1, damage
        );
    }
    return true;
}

bool CSkillEffect_i_hp_special::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a87168. Ctor 0x00ca3d74.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageHP(creature, targetCreature)) {
        return false;
    }

    double amount = 0.0;
    if (m_Sfct == SFCT_PER) {
        amount = m_dAmount / 100.0 * targetCreature->m_data->m_dOrgHP;
        amount = static_cast<double>(
            CSkillEffect_p_potion_bonus::CalcHealBonus(creature, runtime::Win64CvttDoubleToInt32(amount))
        );
    } else if (m_Sfct == SFCT_DIFF) {
        amount = static_cast<double>(targetCreature->m_SkillDurationMod.m_dAddedHPRecovery) + m_dAmount;
        amount = static_cast<double>(
            CSkillEffect_p_potion_bonus::CalcHealBonus(creature, runtime::Win64CvttDoubleToInt32(amount))
        );
    }

    const unsigned int sourceId = creature == nullptr ? 0U : creature->m_nId;
    const double appliedHp = L2SkillFunc::IncreaseHP(targetCreature, sourceId, SFCT_DIFF, amount, 1, true);

    double appliedCp = 0.0;
    if (amount - appliedHp > 0.0) {
        appliedCp = L2SkillFunc::IncreaseCP(targetCreature, sourceId, SFCT_DIFF, amount - appliedHp, 2);
    }

    if (!targetCreature->IsPC()) {
        return true;
    }

    User* const targetUser = targetCreature->CastUser();
    if (appliedHp >= 0.0) {
        if (appliedHp >= 1.0) {
            L2SkillFunc::SendHpMpChangedSystemMessage(
                creature, *targetCreature, runtime::Win64CvttDoubleToInt32(appliedHp), SystemMessageId::HP_RESTORED
            );
        }
        if (appliedCp >= 1.0) {
            L2SkillFunc::SendHpMpChangedSystemMessage(
                creature, *targetCreature, runtime::Win64CvttDoubleToInt32(appliedCp), SystemMessageId::CP_RESTORED
            );
        }
        return true;
    }
    if (creature == nullptr) {
        return true;
    }

    CUserSocket* const targetSocket = targetUser->GetSocket();
    const int damage = runtime::Win64CvttDoubleToInt32(-(appliedHp + appliedCp));
    if (creature->IsNPC()) {
        targetSocket->Send(
            "chccdcd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_HP, 2, 2, creature->GetClassId(), 1, damage
        );
    } else if (creature->IsPC()) {
        User* const sourceUser = creature->CastUser();
        targetSocket->Send(
            "chccScd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_HP, 2, 0x0c, sourceUser->GetName2(targetUser), 1, damage
        );
    }
    return true;
}

CSkillEffect_i_mp::CSkillEffect_i_mp(double amount, SkillFxChangeType changeType) : m_dAmount(amount), m_Sfct(changeType) {
    // Current L2Server 0x00ca3da0.
}

bool CSkillEffect_i_mp::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a875b4.
    L2_GUARD_FUNCTION();
    return Instant(nullptr, target, skillInfo, nullptr, 0.0);
}

bool CSkillEffect_i_mp::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a87680.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    double amount = 0.0;
    if (m_Sfct == SFCT_PER) {
        amount = m_dAmount / 100.0;
    } else if (m_Sfct == SFCT_DIFF) {
        amount = m_dAmount;
    }

    const unsigned int sourceId = creature == nullptr ? 0U : creature->m_nId;
    const double applied = L2SkillFunc::IncreaseMP(targetCreature, sourceId, m_Sfct, amount);

    if (!targetCreature->IsPC()) {
        if (targetCreature->IsSummonOrPet()) {
            targetCreature->CastSummon()->SendSummonStatusToMaster(3);
        }
        return true;
    }

    User* const targetUser = targetCreature->CastUser();
    if (applied >= 0.0) {
        L2SkillFunc::SendHpMpChangedSystemMessage(
            creature, *targetCreature, runtime::Win64CvttDoubleToInt32(applied), SystemMessageId::MP_RESTORED
        );
        return true;
    }
    if (creature == nullptr) {
        return true;
    }

    CUserSocket* const targetSocket = targetUser->GetSocket();
    const int damage = runtime::Win64CvttDoubleToInt32(applied * -1.0);
    if (creature->IsNPC()) {
        targetSocket->Send(
            "chccdcd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_MP, 2, 2, creature->GetClassId(), 1, damage
        );
    } else if (creature->IsPC()) {
        User* const sourceUser = creature->CastUser();
        targetSocket->Send(
            "chccScd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_MP, 2, 0x0c, sourceUser->GetName2(targetUser), 1, damage
        );
    }
    return true;
}

bool CSkillEffect_i_mp_by_hp::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8795c. Ctor 0x00ca3dcc.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature *pSkillUser] is nullptr",
        //     L"SkillFxFunc_instant.cpp",
        //     0x5f0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature *pSkillUser] is nullptr",
        //     L"SkillFxFunc_instant.cpp",
        //     0x5f0)
        L2_LOG_ERROR(Log, "Cannot apply i_mp_by_hp: the caster is unavailable; the effect fails.");
        return false;
    }
    if (!creature->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    double amount = 0.0;
    if (m_Sfct == SFCT_PER) {
        amount = m_dAmount / 100.0;
    } else if (m_Sfct == SFCT_DIFF) {
        amount = m_dAmount;
    }
    amount *= (0.8 - creature->m_data->m_dHP / creature->m_data->m_dOrgHP) + 1.0;

    const double applied = L2SkillFunc::IncreaseMP(targetCreature, creature->m_nId, m_Sfct, amount);

    if (!targetCreature->IsPC()) {
        if (targetCreature->IsSummonOrPet()) {
            targetCreature->CastSummon()->SendSummonStatusToMaster(3);
        }
        return true;
    }

    User* const targetUser = targetCreature->CastUser();
    if (applied >= 0.0) {
        L2SkillFunc::SendHpMpChangedSystemMessage(
            creature, *targetCreature, runtime::Win64CvttDoubleToInt32(applied), SystemMessageId::MP_RESTORED
        );
        return true;
    }

    CUserSocket* const targetSocket = targetUser->GetSocket();
    const int damage = runtime::Win64CvttDoubleToInt32(-applied);
    if (creature->IsNPC()) {
        targetSocket->Send(
            "chccdcd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_MP, 2, 2, creature->GetClassId(), 1, damage
        );
    } else if (creature->IsPC()) {
        User* const sourceUser = creature->CastUser();
        targetSocket->Send(
            "chccScd", 0x62, SystemMessageId::CHARACTER_DRAINED_YOUR_MP, 2, 0x0c, sourceUser->GetName2(targetUser), 1, damage
        );
    }
    return true;
}

bool CSkillEffect_i_cp::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a8f850.
    L2_GUARD_FUNCTION();
    return Instant(nullptr, target, skillInfo, nullptr, 0.0);
}

bool CSkillEffect_i_cp::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8f91c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const targetUser = target->CastUser();
    if (!targetUser->m_SkillMod.CanGetDamageHP(creature, targetUser)) {
        return false;
    }

    double amount = 0.0;
    if (m_sfct == SFCT_PER) {
        amount = static_cast<double>(m_nAmount) / 100.0;
    } else if (m_sfct == SFCT_DIFF) {
        amount = static_cast<double>(m_nAmount);
    }

    const unsigned int sourceId = creature == nullptr ? 0U : creature->m_nId;
    const double applied = L2SkillFunc::IncreaseCP(targetUser, sourceId, m_sfct, amount, 2);
    L2SkillFunc::SendHpMpChangedSystemMessage(
        creature, *targetUser, runtime::Win64CvttDoubleToInt32(applied), SystemMessageId::CP_RESTORED
    );
    return true;
}

bool CSkillEffect_i_sp::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8fa70.
    L2_GUARD_FUNCTION();
    if (!target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    std::int64_t amount = 0;
    if (m_sfct == SFCT_PER) {
        amount = runtime::Win64CvttDoubleToInt64(
            static_cast<double>(m_nAmount) / 100.0 * static_cast<double>(user->m_data->m_nSP)
        );
    } else if (m_sfct == SFCT_DIFF) {
        amount = static_cast<std::int64_t>(m_nAmount);
    }

    user->SPInc(amount);
    user->m_nParamChanged |= std::int64_t{PCB_USER};
    db.SendSaveCharacterInfo(user, false, true);
    return true;
}

bool CSkillEffect_i_inform::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a95244.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC() && target != nullptr && target->IsCreature()) {
        User* const user = static_cast<User*>(creature);
        CCreature* const targetCreature = static_cast<CCreature*>(target);

        wchar_t name[25]{};
        std::wcsncpy(name, Utf16ToWide(targetCreature->GetName()).c_str(), 24);
        name[24] = L'\0';

        char16_t message[1000]{};
        FormatUtf16To(message, 1000, u" Name[%ls] ", name);
        user->SendSystemMessage(u"[Inform]", message);

        const int classId = targetCreature->m_data->m_nClass;
        if (classId >= 0 && classId < 192) {
            FormatUtf16To(message, 1000, u" class[%d]  Level[%d]", classId, targetCreature->m_data->m_nLevel);
        } else {
            FormatUtf16To(message, 1000, u" class[%ls]  Level[%d]", u"unknown", targetCreature->m_data->m_nLevel);
        }
        user->SendSystemMessage(u"[Inform]", message);

        FormatUtf16To(
            message, 1000, u" Int[%d] Str[%d] Con[%d]", targetCreature->m_data->m_Stat.m_N[0],
            targetCreature->m_data->m_Stat.m_N[1], targetCreature->m_data->m_Stat.m_N[2]
        );
        user->SendSystemMessage(u"[Inform]", message);

        FormatUtf16To(
            message, 1000, u" Men[%d] Dex[%d] Wit[%d]", targetCreature->m_data->m_Stat.m_N[3],
            targetCreature->m_data->m_Stat.m_N[4], targetCreature->m_data->m_Stat.m_N[5]
        );
        user->SendSystemMessage(u"[Inform]", message);

        FormatUtf16To(
            message, 1000, u" Physical attack[%lf] Physical defned[%lf]",
            targetCreature->m_data->m_dParam[VALIDATE::PATTACK], targetCreature->m_data->m_dParam[VALIDATE::PDEFEND]
        );
        user->SendSystemMessage(u"[Inform]", message);

        FormatUtf16To(
            message, 1000, u" Magical attack[%lf] Magical defned[%lf]", targetCreature->m_data->m_dParam[VALIDATE::MATTACK],
            targetCreature->m_data->m_dParam[VALIDATE::MDEFEND]
        );
        user->SendSystemMessage(u"[Inform]", message);
    }
    return true;
}

CSkillEffect_i_show_reuse_delay::CSkillEffect_i_show_reuse_delay(std::vector<std::int64_t>& skillIds) {
    // Current L2Server 0x00aa44a0.
    L2_GUARD_FUNCTION();
    for (const std::int64_t skillId : skillIds) {
        m_SkillIds.push_back(runtime::Int32FromBits(static_cast<std::uint32_t>(skillId)));
    }
}

bool CSkillEffect_i_show_reuse_delay::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9df90.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user != nullptr) {
        for (const int skillId : m_SkillIds) {
            const int seconds = runtime::Win64CvttDoubleToInt32(user->GetRemainReuseDelaySec(skillId, 1, 0));
            SkillReuseHelper::SendSystemMsg(
                *user, seconds, 0, SystemMessageId::AGATHION_GIFT_RESUPPLY_MINUTES_SECONDS,
                SystemMessageId::AGATHION_GIFT_RESUPPLY_SECONDS
            );
        }
    }
    return true;
}

bool CSkillEffect_i_karma_count::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00aab34c.
    L2_GUARD_FUNCTION();
    if (!target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    static_cast<void>(CCursedWeaponMgr::inst());
    if (user != nullptr && user->m_nCursedWeaponClassId > 0) {
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        if (m_sfct == SFCT_RESET) {
            user->SetKarma(0);
        } else {
            user->ChangeKarmaByDelta(m_nDelta, false);
        }
    }

    db.SendSaveCharacterInfo(user, false, true);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_SKILL});
    user->SendRelationChanged(nullptr);
    return true;
}

bool CSkillEffect_i_despawn::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a91094.
    L2_GUARD_FUNCTION();
    const double roll = ::genrand64_real1() * 100.0 + 0.0;
    if (!(roll <= m_dProbability)) {
        return false;
    }

    if (target != nullptr && target->IsPC()) {
        User* const user = target->CastUser();
        if (user->HaveSummon()) {
            user->DespawnAllSummon(true, false);
        }
        if (user->GetPet() != nullptr) {
            target->CastUser()->DespawnPet(true, true, DESPAWN_PET_NONE, false);
        }
    }
    return true;
}

bool CSkillEffect_i_food_for_pet::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a912b8.
    L2_GUARD_FUNCTION();
    if (creature->IsPet()) {
        creature->CastPet()->EatFood(nDelta);
        return true;
    }

    if (creature->IsPC() && creature->IsRidingOnGround()) {
        creature->CastUser()->EatStriderFood(nUserDelta);
        return true;
    }
    if (creature->IsPC() && creature->IsRidingWyvern()) {
        creature->CastUser()->EatStriderFood(nWyvernDelta);
    }
    return true;
}

bool CSkillEffect_i_mount_for_event::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a98f8c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_npcName.c_str(), false);
    if (npcClassId < 0) {
        // const std::wstring npcName = target_utf16::ToHostCodeUnits(m_npcName.c_str())
        const std::string npcName = Utf16ToUtf8(m_npcName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] invalid npc name [%S]",
        //     "SkillFxFunc_instant.cpp",
        //     0x2e0c,
        //     npcName.c_str())
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] invalid npc name [%S]",
        //     "SkillFxFunc_instant.cpp",
        //     0x2e0c,
        //     npcName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot apply i_mount_for_event: NPC name '%s' resolves to negative class ID %d; the effect fails.",
            npcName.c_str(), npcClassId
        );
        return false;
    }
    return target->CastUser()->RideForEvent(npcClassId, 0, 0);
}

bool CSkillEffect_i_dismount_for_event::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a990e4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_npcName.c_str(), false);
    if (npcClassId < 0) {
        // const std::wstring npcName = target_utf16::ToHostCodeUnits(m_npcName.c_str())
        const std::string npcName = Utf16ToUtf8(m_npcName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] invalid npc name [%S]",
        //     "SkillFxFunc_instant.cpp",
        //     0x2e27,
        //     npcName.c_str())
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] invalid npc name [%S]",
        //     "SkillFxFunc_instant.cpp",
        //     0x2e27,
        //     npcName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot apply i_dismount_for_event: NPC name '%s' resolves to negative class ID %d; the effect fails.",
            npcName.c_str(), npcClassId
        );
        return false;
    }

    User* const user = target->CastUser();
    if (!user->IsRidingForEvent() || !user->IsRidingOf(COND_CLASS, npcClassId)) {
        return false;
    }
    static_cast<void>(user->TryDismounting(true, false));
    return true;
}

bool CSkillEffect_i_add_max_entrance_inzone::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a91d18.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }
    return target->CastUser()->AddInZoneMaxEntrance(m_groupId, m_delta);
}

bool CSkillEffect_i_restore_vital_point::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a925e8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    int consumeItem = 0;
    int consumeItemCount = 0;
    if (skillInfo != nullptr) {
        consumeItem = skillInfo->m_nConsumeItem;
        consumeItemCount = runtime::Int32FromBits(static_cast<std::uint32_t>(skillInfo->m_nConsumeItemCount));
    }

    User* const user = target->CastUser();
    const bool restored = user->RestoreVitalPoint(m_RestoreAmount, m_nType, consumeItem, consumeItemCount);
    if (restored) {
        user->SendVitalityEffectInfo(nullptr);
        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_VP_GET_ITEM, 0, m_RestoreAmount);
    }
    return restored;
}

bool CSkillEffect_i_summon_trap::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a96944.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    if (m_PCTrapClassId == -1) {
        m_PCTrapClassId = g_ObjectDB.GetClassIdFromName(m_PCTrapName.c_str(), false);
        if (m_PCTrapClassId == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"PCTrap Class Id isn't valid.")
            // L2_LOG_ERROR(Log,
            //     L"PCTrap Class Id isn't valid.")
            L2_LOG_ERROR(
                Log, "Cannot apply i_summon_trap: trap name '%s' resolves to class ID -1; no trap is summoned.",
                Utf16ToUtf8(m_PCTrapName.c_str()).c_str()
            );
            return false;
        }
    }

    creature->CastUser()->SummonPCTrap(m_PCTrapClassId);
    return true;
}

bool CSkillEffect_i_summon_npc::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a96a60.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(xwsNPCName.c_str(), false);
    static_cast<void>(user->SummonNpc(npcClassId, nCount, skillInfo, NST_NOT_EVENT, 0));
    return true;
}

bool CSkillEffect_i_summon_npc_position::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a96b70.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !target->IsCreature()) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(xwsNPCName.c_str(), false);
    static_cast<void>(creature->CastUser()->SummonNpc(npcClassId, 1, skillInfo, NST_SUMMON, 0));
    return true;
}

bool CSkillEffect_i_summon_npc_near::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a96c98.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(xwsNPCName.c_str(), false);
    static_cast<void>(creature->CastUser()->SummonNpc(npcClassId, nCount, skillInfo, NST_NOTHING, nDistance));
    return true;
}

bool CSkillEffect_i_summon_npc_dying::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a96da0.
    L2_GUARD_FUNCTION();
    if (L2Common::IsAlivePC(creature)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(xwsNPCName.c_str(), false);
    static_cast<void>(creature->CastUser()->SummonNpc(npcClassId, nCount, skillInfo, NST_NOTHING, 0));
    return true;
}

bool CSkillEffect_i_call_pc::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a91810.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || target == nullptr || !target->IsPC()) {
        return false;
    }

    if (creature->CastUser()->m_data->m_TrainingState == 1) {
        creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    if (target->CastUser()->m_data->m_TrainingState == 1) {
        creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
        return false;
    }

    CPledgeGameField* const field = target->CastUser()->m_PledgeGameUser.GetField();
    if (field != nullptr && field->m_bIsUnRegist) {
        return false;
    }

    const bool includeDead = skillInfo == nullptr || skillInfo->m_bCondIncludeDead;
    target->CastUser()->CallPCTry(creature->CastUser(), m_nItemClassId, m_nItemCount, includeDead);
    return true;
}

CSkillEffect_i_summon::CSkillEffect_i_summon(
    const char16_t* npcName, int masterExpRatio, const char16_t* itemName, int itemCount, int duration
) :
    m_sNPCName(npcName == nullptr ? u"" : npcName),
    m_nMasterExpRatio(masterExpRatio),
    m_sItemName(itemName == nullptr ? u"" : itemName),
    m_nItemCount(itemCount),
    m_nDuration(duration) {
    // Current L2Server 0x00ca3a5c.
}

bool CSkillEffect_i_summon::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a94e30.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillEffect_i_summon::Instant failed. not a pc used")
        // L2_LOG_ERROR(Log,
        //     L"CSkillEffect_i_summon::Instant failed. not a pc used")
        L2_LOG_ERROR(Log, "Cannot apply i_summon: the caster is not a player; the summon request is rejected.");
        return false;
    }

    creature->CastUser()->DespawnAllSummon(true, false);
    const std::wstring npcName = target_utf16::ToHostCodeUnits(m_sNPCName.c_str());
    const std::wstring itemName = target_utf16::ToHostCodeUnits(m_sItemName.c_str());
    static_cast<void>(creature->CastUser()->SummonCreature(
        npcName.c_str(), m_nMasterExpRatio, itemName.c_str(), static_cast<std::int64_t>(m_nItemCount), m_nDuration,
        m_nDuration, const_cast<CSkillInfo*>(skillInfo)
    ));
    return true;
}

bool CSkillEffect_i_summon_multi::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a94f84.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillEffect_i_summon_multi::Instant failed. "
        //     L"not a pc used")
        // L2_LOG_ERROR(Log,
        //     L"CSkillEffect_i_summon_multi::Instant failed. "
        //     L"not a pc used")
        L2_LOG_ERROR(Log, "Cannot apply i_summon_multi: the caster is not a player; the summon request is rejected.");
        return false;
    }

    std::array<wchar_t, 25> npcName{};
    const std::wstring hostNpcName = target_utf16::ToHostCodeUnits(m_NpcName.c_str());
    if (hostNpcName.size() < npcName.size()) {
        std::copy(hostNpcName.begin(), hostNpcName.end(), npcName.begin());
    }

    if (creature->CastUser()->GetOneSummon(User::SUMMON_SINGLE) != nullptr) {
        return false;
    }

    const std::wstring consumeItemName = target_utf16::ToHostCodeUnits(m_ConsumeItemName.c_str());
    static_cast<void>(creature->CastUser()->SummonCreature(
        npcName.data(), m_nMasterExpRatio, consumeItemName.c_str(), static_cast<std::int64_t>(m_nConsumeItemCount), 0, 0,
        const_cast<CSkillInfo*>(skillInfo)
    ));
    return true;
}

bool CSkillEffect_i_summon_pledge::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a95108.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillEffect_i_summon_pledge::Instant failed. "
        //     L"Only PC can do this.")
        // L2_LOG_ERROR(Log,
        //     L"CSkillEffect_i_summon_pledge::Instant failed. "
        //     L"Only PC can do this.")
        L2_LOG_ERROR(Log, "Cannot apply i_summon_pledge: the caster is not a player; the summon request is rejected.");
        return false;
    }

    if (creature->CastUser()->m_data->m_nPledgeID == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillEffect_i_summon_pledge::Instant failed. "
        //     L"Only member of pledge can do this.")
        // L2_LOG_ERROR(Log,
        //     L"CSkillEffect_i_summon_pledge::Instant failed. "
        //     L"Only member of pledge can do this.")
        L2_LOG_WARN(Log, "Cannot apply i_summon_pledge: the caster has no clan; the summon request is rejected.");
        return false;
    }

    const std::wstring npcName = target_utf16::ToHostCodeUnits(m_NpcName.c_str());
    static_cast<void>(creature->CastUser()->SummonNPCRelatedToPledge(npcName.c_str(), const_cast<CSkillInfo*>(skillInfo)));
    return true;
}

bool CSkillEffect_i_summon_unique_npc::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a9939c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_NpcName.c_str(), false);
    if (npcClassId <= 1000000) {
        return false;
    }

    CBoss* const boss = g_UniqNPCDB.GetBossNPC(0, npcClassId);
    if (boss != nullptr) {
        boss->Despawn(false, true);
    }
    static_cast<void>(creature->CastUser()->SummonNpc(npcClassId, 1, skillInfo, NST_UNIQUE, 0));
    return true;
}

bool CSkillEffect_i_teleport_to_npc::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a9968c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    if (creature->CastUser()->m_PledgeGameUser.IsPlayer()) {
        creature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_NpcName.c_str(), false);
    if (npcClassId == -1) {
        return false;
    }

    CNPC* const npc = creature->CastUser()->GetSummonedNPC(npcClassId);
    if (npc != nullptr) {
        const FVector destination = npc->GetPos();
        creature->CastUser()->TeleportToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), 0, 0x10000, TeleportSourceType::tst_i_teleport_to_npc,
            nullptr, 0
        );
    }
    return true;
}

bool CSkillEffect_i_party_teleport_to_npc::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa2fd0. The low byte of the machine return value
    // is cleared on every path, including the successful teleport path.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    if (creature->CastUser()->m_PledgeGameUser.IsPlayer()) {
        creature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
        return false;
    }

    const int npcClassId = g_ObjectDB.GetClassIdFromName(m_NpcName.c_str(), false);
    if (npcClassId == -1) {
        return false;
    }

    CNPC* const npc = creature->CastUser()->GetSummonedNPC(npcClassId);
    if (npc == nullptr) {
        return false;
    }

    const FVector destination = npc->GetPos();
    User* const caster = creature->CastUser();
    const FVector casterOrigin = caster->GetPos();
    creature->CastUser()->TeleportToLocation(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), 0, 0x10000, TeleportSourceType::tst_i_teleport_to_npc, nullptr, 0
    );

    CParty* const party = caster->GetParty();
    if (party != nullptr) {
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            User::CallPCResult result = User::CPR_ETC;
            if (member == nullptr || member->GetServerId() == caster->GetServerId() ||
                !User::IsCallPCEnable(caster, member, true, result)) {
                continue;
            }

            const FVector memberPosition = member->GetPos();
            const double x = casterOrigin.x - memberPosition.x;
            const double y = casterOrigin.y - memberPosition.y;
            const double z = casterOrigin.z - memberPosition.z;
            if (x * x + y * y + z * z > 2250000.0) {
                continue;
            }

            {
                FOR_ALL_SUMMON_IN_SUMMON_LIST(
                    member, summon,
                    // current 0x00aa2fd0 creates in_code before the race test.
                    if (summon->m_data->m_Race == RT_SIEGE_WEAPON) { member->DespawnSummon(summon, true, false); }
                );
            }

            member->DoNothing();
            member->TeleportToLocation(
                runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
                runtime::Win64CvttDoubleToInt32(destination.z), 0, 0x10000,
                TeleportSourceType::tst_i_party_teleport_to_npc, nullptr, 0
            );
        }
    }
    return false;
}

bool CSkillEffect_i_blink_party::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa3490.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !creature->IsPC() || target == nullptr || !target->IsCreature() ||
        skillInfo == nullptr || skillInfo->m_TargetType != STGT_SELF) {
        return false;
    }

    User* const caster = creature->CastUser();
    if (caster == nullptr) {
        return false;
    }

    L2SkillFunc::CancelAllActionForMove(caster->CastCreature());
    const FVector casterOrigin = caster->GetPos();
    FVector destination{};
    if (!caster->GetBlinkDestPos(m_Distance, m_Direction, destination)) {
        caster->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        return false;
    }

    if (!CWorld::Inst().MoveTo(caster, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_blink_party")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_blink_party")
        L2_LOG_ERROR(
            Log, "i_blink_party could not move object %u to (%.17g, %.17g, %.17g). Party movement processing continues.",
            static_cast<unsigned>(caster->Id()), destination.x, destination.y, destination.z
        );
    } else {
        caster->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
        );
        caster->CastCreature()->ValidateLocation();
    }

    FVector direction{destination.x - casterOrigin.x, destination.y - casterOrigin.y, destination.z - casterOrigin.z};
    direction.NormalizeCube();

    CParty* const party = caster->GetParty();
    if (party == nullptr) {
        return false;
    }

    std::vector<std::uint32_t> memberIds;
    party->GetAllMemberId(memberIds);
    const double effectRangeSquared = m_EffectRange * m_EffectRange;
    for (const std::uint32_t memberId : memberIds) {
        if (memberId == static_cast<std::uint32_t>(caster->m_nId)) {
            continue;
        }

        CSPointer<User> member = g_UserMap.FindObject(static_cast<int>(memberId));
        if (!member || !member->IsAlive()) {
            continue;
        }

        const FVector memberPosition = member->GetPos();
        const double x = memberPosition.x - casterOrigin.x;
        const double y = memberPosition.y - casterOrigin.y;
        if (x * x + y * y > effectRangeSquared) {
            continue;
        }

        L2SkillFunc::CancelAllActionForMove(member->CastCreature());
        FVector memberDestination{};
        if (!member->GetBlinkDestPos(m_Distance, direction, memberDestination)) {
            continue;
        }

        if (!CWorld::Inst().MoveTo(member.get(), memberDestination, false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CWorld::Inst().MoveTo Failed in i_blink_party")
            // L2_LOG_ERROR(Log,
            //     L"CWorld::Inst().MoveTo Failed in i_blink_party")
            L2_LOG_ERROR(
                Log,
                "i_blink_party could not move object %u to (%.17g, %.17g, %.17g). Other party members are still processed.",
                static_cast<unsigned>(member->Id()), memberDestination.x, memberDestination.y, memberDestination.z
            );
        } else {
            member->BroadcastFlyToLocation(
                runtime::Win64CvttDoubleToInt32(memberDestination.x),
                runtime::Win64CvttDoubleToInt32(memberDestination.y),
                runtime::Win64CvttDoubleToInt32(memberDestination.z), FTLT_BLINK, 0, 0, 0
            );
            member->CastCreature()->ValidateLocation();
        }
    }
    return true;
}

bool CSkillEffect_i_fly_self::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8d7f4.
    L2_GUARD_FUNCTION();
    FVector zero{};
    if (creature->m_vMoveSkillDest == zero || creature->m_vMoveSkillDest == creature->m_vSrcPos) {
        return false;
    }

    const FVector destination = creature->m_vMoveSkillDest;
    creature->m_vMoveSkillDest = FVector{};
    if (CWorld::Inst().IsValidPosition(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y)
        )) {
        creature->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
        );
        CWorld::Inst().MoveTo(creature, destination, false);
    }
    return true;
}

bool CSkillEffect_i_blink::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8d9a0. Ctor is parser action 302.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    CCreature* const targetCreature = creature->CastUser();
    FVector destination{};
    if (!targetCreature->GetBlinkDestPos(m_Dist, m_Direction, destination)) {
        targetCreature->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        return false;
    }

    if (!CWorld::Inst().MoveTo(targetCreature, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_blink")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_blink")
        L2_LOG_ERROR(
            Log, "i_blink could not move object %u to (%.17g, %.17g, %.17g). The effect still reports success.",
            static_cast<unsigned>(targetCreature->Id()), destination.x, destination.y, destination.z
        );
    } else {
        targetCreature->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
        );
        creature->ValidateLocation();
    }
    return true;
}

bool CSkillEffect_i_teleport_to_target::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8c190.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !target->IsCreature()) {
        return false;
    }

    User* const user = creature->CastUser();
    CCreature* const targetCreature = target->CastCreature();
    FVector destination{};
    if (!user->GetTargetBackPos(targetCreature, destination)) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        return false;
    }

    if (user->HasFlagOfSiege()) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
        return false;
    }

    if (!CWorld::Inst().MoveTo(user, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_teleport_to_target")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_teleport_to_target")
        L2_LOG_ERROR(
            Log, "i_teleport_to_target could not move object %u to (%.17g, %.17g, %.17g). The effect still reports success.",
            static_cast<unsigned>(user->Id()), destination.x, destination.y, destination.z
        );
    } else {
        user->TurnDirToTarget(*target);
        user->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
        );
        creature->ValidateLocation();
    }
    return true;
}

bool CSkillEffect_i_random_target::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa6fec.
    L2_GUARD_FUNCTION();
    if (target->IsNPC() || target->IsBoss() || !target->IsVisible()) {
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(target);
    CCreature::SkillTargetData targetData;
    const int randomLimit = runtime::Win64CvttDoubleToInt32(
                                genrand64_real2() * static_cast<double>(skillInfo->m_nAffectLimitRandom + 1) + 0.0
                            ) +
        skillInfo->m_nAffectLimitBase;
    targetCreature->GatherNeighborCreature(
        targetCreature->m_data->m_Pos, 0, randomLimit, false, targetData, skillInfo->m_nAffectRange,
        skillInfo->m_nAffectScopeMinZ, skillInfo->m_nAffectScopeMaxZ, skillInfo->m_AffectObjectType, creature, creature,
        true, false
    );

    const int index = std::rand() % static_cast<int>(targetData.c.size());
    CSPointer<CCreature> selected = runtime::CheckedVectorAt(targetData.c, index).FindObjectSP();
    CCreature* const selectedCreature = selected.get();
    CCreature* const currentTarget = targetCreature->m_spTarget.GetCreature();
    if (currentTarget == nullptr || selectedCreature->m_nId != currentTarget->m_nId) {
        targetCreature->Action(selectedCreature, targetCreature->m_AreaMod.m_bIsInBattleField, false);
        return true;
    }
    return false;
}

bool CSkillEffect_i_set_reuse_delay::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8cad4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (m_nUpdateStyle != 2) {
        const double remaining = targetCreature->GetRemainReuseDelaySec(m_nSkillId, 1, 0);
        if (remaining > 0.0 &&
            (m_nUpdateStyle == 0 || (m_nUpdateStyle == 1 && remaining > static_cast<double>(m_nReuseTimeSec)))) {
            return false;
        }
    }

    const std::uint32_t delayMilliseconds = static_cast<std::uint32_t>(m_nReuseTimeSec) * 1000U;
    targetCreature->m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(m_nSkillId, 4, delayMilliseconds, 0);
    return true;
}

bool CSkillEffect_i_reuse_skill_reset::Init(
    const std::vector<std::int64_t>& skillIds, double probability, int delayLimitMSec
) {
    // Current L2Server 0x00aa4634.
    L2_GUARD_FUNCTION();
    m_resetSkillList.reserve(skillIds.size());
    for (const std::int64_t skillId : skillIds) {
        m_resetSkillList.push_back(runtime::Int32FromBits(static_cast<std::uint32_t>(skillId)));
    }
    m_dProb = probability;
    m_nDelayLimitMSec = delayLimitMSec;
    return true;
}

bool CSkillEffect_i_reuse_skill_reset::Instant(
    CCreature*, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8cc0c.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return false;
    }
    if (skillInfo->m_TargetType != STGT_SELF) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"i_reuse_skill_reset: target type is not self "
        //     L"[id:%d][TargetType:%d]",
        //     skillInfo->m_nSkillId,
        //     static_cast<int>(skillInfo->m_TargetType))
        // L2_LOG_ERROR(Log,
        //     L"i_reuse_skill_reset: target type is not self "
        //     L"[id:%d][TargetType:%d]",
        //     skillInfo->m_nSkillId,
        //     static_cast<int>(skillInfo->m_TargetType))
        L2_LOG_ERROR(
            Log,
            "Cannot apply i_reuse_skill_reset for skill %d: target type %d is not self; reuse times are left unchanged.",
            skillInfo->m_nSkillId, static_cast<int>(skillInfo->m_TargetType)
        );
        return false;
    }
    if (!L2Common::IsAlivePC(target) || std::fabs(m_dProb) < kReuseSkillProbabilityEpsilon || m_nDelayLimitMSec < 0) {
        return false;
    }

    const double roll = genrand64_real1() * 100.0;
    if (std::fabs(m_dProb - 100.0) < kReuseSkillProbabilityEpsilon || roll < m_dProb) {
        User* const user = target->CastUser();
        user->SkillEffectResetSkillReuseDelay(m_resetSkillList, m_nDelayLimitMSec);
        user->SendCoolTime();
    }
    return true;
}

bool CSkillEffect_i_reuse_skill_reduce::Init(
    const std::vector<std::int64_t>& skillIds, double probability, int reduceMSec, int delayLimitMSec
) {
    // Current L2Server 0x00aa4728.
    L2_GUARD_FUNCTION();
    for (const std::int64_t skillId : skillIds) {
        m_reduceSkillList.push_back(runtime::Int32FromBits(static_cast<std::uint32_t>(skillId)));
    }
    m_dProb = probability;
    m_nReduceMSec = reduceMSec;
    m_nDelayLimitMSec = delayLimitMSec;
    return true;
}

bool CSkillEffect_i_reuse_skill_reduce::Instant(
    CCreature*, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8cdac.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return false;
    }
    if (skillInfo->m_TargetType != STGT_SELF) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"i_reuse_skill_reduce : target type is not self "
        //     L"[id:%d][TargetType:%d]",
        //     skillInfo->m_nSkillId,
        //     static_cast<int>(skillInfo->m_TargetType))
        // L2_LOG_ERROR(Log,
        //     L"i_reuse_skill_reduce : target type is not self "
        //     L"[id:%d][TargetType:%d]",
        //     skillInfo->m_nSkillId,
        //     static_cast<int>(skillInfo->m_TargetType))
        L2_LOG_ERROR(
            Log,
            "Cannot apply i_reuse_skill_reduce for skill %d: target type %d is not self; reuse times are left unchanged.",
            skillInfo->m_nSkillId, static_cast<int>(skillInfo->m_TargetType)
        );
        return false;
    }
    if (!L2Common::IsAlivePC(target) || std::fabs(m_dProb) < kReuseSkillProbabilityEpsilon || m_nReduceMSec < 0 ||
        m_nDelayLimitMSec < 0) {
        return false;
    }

    const double roll = genrand64_real1() * 100.0;
    if (std::fabs(m_dProb - 100.0) < kReuseSkillProbabilityEpsilon || roll < m_dProb) {
        User* const user = target->CastUser();
        user->SkillEffectReduceSkillReuseDelay(m_reduceSkillList, m_nReduceMSec, m_nDelayLimitMSec);
        user->SendCoolTime();
    }
    return true;
}

bool CSkillEffect_i_reset_quest::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a994d8.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    for (const std::int64_t questIdValue : m_QuestIds) {
        const std::uint32_t questIdBits = static_cast<std::uint32_t>(questIdValue);
        const int questId = runtime::Int32FromBits(questIdBits);
        if (questIdBits < 0x100U || questIdBits - 0x280fU <= 0x700U) {
            {
                L2_GUARD(*user->m_lock);
                user->m_data->SetOneTimeFlag(questId, false);
                db.SaveQuestInfo(user);
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "cannot reset onetime quest id (%d) at "
            //     "file[%s], line[%d]",
            //     questId,
            //     "SkillFxFunc_instant.cpp",
            //     0x3033)
            // L2_LOG_ERROR(Log,
            //     "cannot reset onetime quest id (%d) at "
            //     "file[%s], line[%d]",
            //     questId,
            //     "SkillFxFunc_instant.cpp",
            //     0x3033)
            L2_LOG_ERROR(
                Log,
                "Cannot reset one-time quest %d from input %lld: its low 32-bit ID is outside 0..255 and 10255..12047; this "
                "entry is skipped.",
                questId, static_cast<long long>(questIdValue)
            );
        }
    }
    return true;
}

bool CSkillEffect_i_betray::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a9c1cc.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsBoss() || targetCreature->IsZzoldagu()) {
        return false;
    }

    const double probability =
        L2SkillFunc::CalculateProbability(static_cast<double>(m_nProb), creature, targetCreature, skillInfo);
    if (!(genrand64_real1() * 100.0 < probability)) {
        return false;
    }

    const int bossId = targetCreature->m_data->m_BossID;
    if (bossId == 0) {
        return false;
    }

    CSPointer<CNPC> boss = g_NPCMap.FindObject(bossId);
    if (boss && boss->IsNPC()) {
        g_npcserver.Send(
            "cddddd", 0x2c, static_cast<int>(targetCreature->m_nId), targetCreature->GetIndex(),
            static_cast<int>(boss->m_nId), boss->GetIndex(), m_nDuration
        );
    }
    return true;
}

bool CSkillEffect_i_sidestep_to_target::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a93b8c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    CCreature* const targetCreature = target->CastCreature();
    FVector destination{};
    if (!user->GetSidestepPos(targetCreature, destination, m_eDirection)) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        return false;
    }
    if (user->HasFlagOfSiege()) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
        return false;
    }

    if (!CWorld::Inst().MoveTo(user, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_sidestep_to_target")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_sidestep_to_target")
        L2_LOG_ERROR(
            Log, "i_sidestep_to_target could not move object %u to (%.17g, %.17g, %.17g). The effect still reports success.",
            static_cast<unsigned>(user->Id()), destination.x, destination.y, destination.z
        );
    } else {
        user->TurnDirToTarget(*target);
        user->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_CHARGE, 0, 0, 0
        );
        creature->ValidateLocation();
    }
    return true;
}

bool CSkillEffect_i_goto_target::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a98064.
    L2_GUARD_FUNCTION();
    if (!creature->IsCreature() || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user->HasFlagOfSiege()) {
            user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
            return false;
        }
        if (user->m_PledgeGameUser.IsPlayer()) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
            return false;
        }
        if (user->IsRidingWyvern() || user->IsInAirShip() || static_cast<int>(user->m_data->m_moveType) == CMT_FLY) {
            return false;
        }
        if (user->IsInNoCallArea()) {
            user->SendSystemMessage(SystemMessageId::CURRENT_LOCATION_BLOCKS_SUMMON_OR_TELEPORT);
            return false;
        }
    }

    if (targetCreature->IsPC()) {
        User* const targetUser = targetCreature->CastUser();
        if (targetUser->m_PledgeGameUser.IsPlayer() || targetUser->IsRidingWyvern() || targetUser->IsInAirShip() ||
            static_cast<int>(targetUser->m_data->m_moveType) == CMT_FLY) {
            return false;
        }

        CUserSocket* const socket = creature->GetSocket();
        if (!targetUser->IsAlive()) {
            if (socket != nullptr) {
                socket->SendSystemMessage_S(
                    SystemMessageId::CANNOT_SUMMON_OR_TELEPORT_DEAD_CHARACTER, targetUser->GetName()
                );
            }
            return false;
        }
        if (targetUser->IsInNoCallArea()) {
            if (socket != nullptr) {
                socket->SendSystemMessage_S(
                    SystemMessageId::TARGET_LOCATION_BLOCKS_SUMMON_OR_TELEPORT, targetUser->GetName()
                );
            }
            return false;
        }
    }

    const FVector destination = targetCreature->GetPos();
    creature->TeleportToLocation(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), targetCreature->GetInZoneID(), 0x10000,
        TeleportSourceType::tst_i_teleport, creature, 0
    );
    return true;
}

bool CSkillEffect_i_teleport_to_summon::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8c370.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->m_PledgeGameUser.IsPlayer()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
        return false;
    }
    if (user->GetSummonCount() != 1) {
        return false;
    }

    CSummon* const summon = user->GetOneSummon(User::SUMMON_ANY);
    if (summon == nullptr) {
        return false;
    }
    CCreature* const summonCreature = summon->CastCreature();
    FVector destination{};
    if (!user->GetTargetBackPos(summonCreature, destination)) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        return false;
    }
    if (user->HasFlagOfSiege()) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
        return false;
    }

    const FVector summonPosition = summonCreature->GetPos();
    const FVector userPosition = user->GetPos();
    const double x = userPosition.x - summonPosition.x;
    const double y = userPosition.y - summonPosition.y;
    const double z = userPosition.z - summonPosition.z;
    const int distanceSquared =
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_Distance) * static_cast<std::uint32_t>(m_Distance));
    if (x * x + y * y + z * z > static_cast<double>(distanceSquared)) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_TOO_FAR);
        return false;
    }

    if (!CWorld::Inst().MoveTo(user, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_teleport_to_summon")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_teleport_to_summon")
        L2_LOG_ERROR(
            Log, "i_teleport_to_summon could not move object %u to (%.17g, %.17g, %.17g). The effect still reports success.",
            static_cast<unsigned>(user->Id()), destination.x, destination.y, destination.z
        );
    } else {
        user->TurnDirToTarget(*target);
        user->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
        );
        creature->ValidateLocation();
    }
    return true;
}

bool CSkillEffect_i_bookmark_teleport::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9925c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const user = target->CastUser();
    if (user == nullptr) {
        return false;
    }
    if (user->HasFlagOfSiege()) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
        return false;
    }
    if (user->IsRidingWyvern() || user->IsInAirShip() || static_cast<int>(user->m_data->m_moveType) == CMT_FLY) {
        return false;
    }
    return user->m_bookmark.TeleportTo(user);
}

bool CSkillEffect_i_ensoul::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a9407c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    const double roll = ::genrand64_real1() * 100.0 + 0.0;
    if (targetCreature == nullptr || creature->GetSocket() == nullptr) {
        return true;
    }
    return roll <= creature->m_SkillMod.m_dEnsoulAddProb + m_dProb;
}

bool CSkillEffect_i_convert_item::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a90b20.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const user = target->CastUser();
    CSharedItemData* const weapon = user->GetEquipedWeapon();
    if (weapon == nullptr) {
        return false;
    }
    if (weapon->m_bIsVariation) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CONVERT_AUGMENTED_ITEM);
        return false;
    }

    const int convertedItemClassId = g_ConvertItemDB.GetConvertItem(weapon->m_nClassID);
    if (convertedItemClassId == 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_CONVERTED);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr || user->GetPendingUseETCItem(PUIT_ONE) != nullptr ||
        user->GetPendingUseETCItem(PUIT_TWO) != nullptr || user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr) {
        return false;
    }

    user->m_bConvertingItem = true;
    db.RequestConvertItem(user, weapon->m_nDBID, convertedItemClassId);
    return true;
}

bool CSkillEffect_i_ev_ink_party::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa3ad0.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    const auto updateUser = [this, skillInfo](User* member) {
        if (member == nullptr) {
            return;
        }
        member->UpdateInkEnergy(m_nSkillInkEnergy, false);
        if (skillInfo->m_nLevel == 1) {
            member->SendSystemMessage(SystemMessageId::SMALL_BARD_INK_REPLENISHED_BY_INK_HERB);
        } else if (skillInfo->m_nLevel == 2) {
            member->SendSystemMessage(SystemMessageId::SMALL_BARD_INK_REPLENISHED_BY_GREAT_INK_HERB);
        }
    };

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        updateUser(user);
        return true;
    }
    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        updateUser(member);
    }
    return true;
}

bool CSkillEffect_i_create_item_random::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8efc0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    CObject* const object = g_ObjectDB.GetObjectW(skillInfo->m_nConsumeItem, false);
    if (object == nullptr ||
        static_cast<CItem*>(object)->m_const->m_enumDefaultAction != ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) {
        return false;
    }

    if (user->CastUser()->m_inv.HaveItemByClassId(skillInfo->m_nConsumeItem, skillInfo->m_nConsumeItemCount, false, false)) {
        return true;
    }

    user->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
    return false;
}

CSkillEffect_i_shape_shift_item_auto_matching::CSkillEffect_i_shape_shift_item_auto_matching() :
    m_nPeriod(-1) {}

CSkillEffect_i_shape_shift_item_auto_matching::CSkillEffect_i_shape_shift_item_auto_matching(const int period) :
    m_nPeriod(period) {}

CSkillEffect_i_shape_shift_item_auto_matching::~CSkillEffect_i_shape_shift_item_auto_matching() = default;

ShapeShiftAutoMatchingType::type CSkillEffect_i_shape_shift_item_auto_matching::FindType(CItem* item) {
    // Current L2Server 0x00a99cc0.
    L2_GUARD_FUNCTION();
    using namespace ShapeShiftAutoMatchingType;

    if (item == nullptr || !item->IsItem()) {
        return SSAMT_MAX;
    }

    const std::int64_t slot = item->m_data->m_Slot;
    if (item->IsWeapon()) {
        switch (item->m_const->m_AttackType) {
            case AT_RAPIER:
                if (slot != 0x80) {
                    break;
                }
                if (item->m_const->m_bIsMagicWeapon && !m_vExtractionItems[SSAMT_ONE_HAND_MAGIC_SWORD].empty()) {
                    return SSAMT_ONE_HAND_MAGIC_SWORD;
                }
                return SSAMT_ONE_HAND_SWORD;
            case AT_SWORD:
                return SSAMT_ONE_HAND_SWORD;
            case AT_BUSTER:
                if (!m_vExtractionItems[SSAMT_ONE_HAND_MAGIC_SWORD].empty()) {
                    return SSAMT_ONE_HAND_MAGIC_SWORD;
                }
                return SSAMT_ONE_HAND_SWORD;
            case AT_DAGGER:
                if (slot == 0x80) {
                    return SSAMT_DAGGER;
                }
                break;
            case AT_TWOHANDSWORD:
                return SSAMT_TWO_HAND_SWORD;
            case AT_ANCIENTSWORD:
                if (slot == 0x4000) {
                    return SSAMT_TWO_HAND_SWORD;
                }
                break;
            case AT_DUALFIST:
                if (slot == 0x4000) {
                    return SSAMT_DUAL_FIST;
                }
                break;
            case AT_POLE:
                if (slot == 0x4000) {
                    return SSAMT_POLE;
                }
                break;
            case AT_BOW:
                if (slot == 0x4000) {
                    return SSAMT_BOW;
                }
                break;
            case AT_CROSSBOW:
            case AT_TWOHANDCROSSBOW:
                if (slot == 0x4000) {
                    return SSAMT_CROSSBOW;
                }
                break;
            case AT_BLUNT:
                return SSAMT_ONE_HAND_BLUNT;
            case AT_TWOHANDBLUNT:
                return SSAMT_TWO_HAND_BLUNT;
            case AT_STAFF:
                return SSAMT_ONE_HAND_MAGIC_BLUNT;
            case AT_TWOHANDSTAFF:
                return SSAMT_TWO_HAND_MAGIC_BLUNT;
            case AT_DUAL:
                if (slot == 0x4000) {
                    return SSAMT_DUAL_SWORD;
                }
                break;
            case AT_DUALDAGGER:
                if (slot == 0x4000) {
                    return SSAMT_DUAL_DAGGER;
                }
                break;
            case AT_DUALBLUNT:
                if (slot == 0x4000) {
                    return SSAMT_DUAL_BLUNT;
                }
                break;
            default:
                break;
        }
        return SSAMT_MAX;
    }

    if (!item->IsArmor()) {
        return SSAMT_MAX;
    }

    switch (item->m_const->m_ArmorType) {
        case AMT_NONE:
            return slot == 0x100 ? SSAMT_SHIELD : SSAMT_MAX;
        case AMT_SIGIL:
            return slot == 0x100 ? SSAMT_SIGIL : SSAMT_MAX;
        case AMT_HEAVY:
            switch (slot) {
                case 0x400:
                    return SSAMT_HEAVY_CHEST;
                case 0x200:
                    return SSAMT_HEAVY_GLOVES;
                case 0x800:
                    return SSAMT_HEAVY_LEGS;
                case 0x1000:
                    return SSAMT_HEAVY_FEET;
                case 0x8000:
                    return SSAMT_HEAVY_ONEPIECE;
                case 0x20000:
                    return SSAMT_HEAVY_ALLDRESS;
                default:
                    return SSAMT_MAX;
            }
        case AMT_LIGHT:
            switch (slot) {
                case 0x400:
                    return SSAMT_LIGHT_CHEST;
                case 0x200:
                    return SSAMT_LIGHT_GLOVES;
                case 0x800:
                    return SSAMT_LIGHT_LEGS;
                case 0x1000:
                    return SSAMT_LIGHT_FEET;
                case 0x8000:
                    return SSAMT_LIGHT_ONEPIECE;
                case 0x20000:
                    return SSAMT_LIGHT_ALLDRESS;
                default:
                    return SSAMT_MAX;
            }
        case AMT_MAGIC:
            switch (slot) {
                case 0x400:
                    return SSAMT_MAGIC_CHEST;
                case 0x200:
                    return SSAMT_MAGIC_GLOVES;
                case 0x800:
                    return SSAMT_MAGIC_LEGS;
                case 0x1000:
                    return SSAMT_MAGIC_FEET;
                case 0x8000:
                    return SSAMT_MAGIC_ONEPIECE;
                case 0x20000:
                    return SSAMT_MAGIC_ALLDRESS;
                default:
                    return SSAMT_MAX;
            }
        default:
            return SSAMT_MAX;
    }
}

bool CSkillEffect_i_shape_shift_item_auto_matching::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9ce0c. Lifecycle
    // 0x00aab634..0x00aab7dc.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    int shapeShiftClassId = 0;
    const CItem* const targetItem = target != nullptr && target->IsItem() ? static_cast<CItem*>(target) : nullptr;
    const ShapeShiftAutoMatchingType::type type = FindType(const_cast<CItem*>(targetItem));
    if (type != ShapeShiftAutoMatchingType::SSAMT_MAX) {
        const int randomValue = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 101.0);
        int cumulativeProbability = 0;
        for (const FixedShapeShiftExtractionItemInfo& info : m_vExtractionItems[type]) {
            cumulativeProbability += info.nProb;
            if (randomValue < cumulativeProbability) {
                shapeShiftClassId = g_ObjectDB.GetClassIdFromName(info.xwsItemName.c_str(), false);
                break;
            }
        }
    }

    if (targetItem != nullptr && extractionItem != nullptr && shapeShiftClassId != 0) {
        return extractionItem->ShapeShiftItem(user, const_cast<CItem*>(targetItem), shapeShiftClassId, 0, m_nPeriod);
    }

    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    user->GetSocket()->Send(
        "chdddd", 0xfe, 300,
        // 2,
        ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

CSkillEffect_i_shape_shift_wp_fixed::CSkillEffect_i_shape_shift_wp_fixed() : m_nPeriod(-1) {}

CSkillEffect_i_shape_shift_wp_fixed::CSkillEffect_i_shape_shift_wp_fixed(int period) :
    m_nPeriod(period) {}

bool CSkillEffect_i_shape_shift_wp_fixed::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a99838. Ctors 0x00aab4b4/0x00aab514.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    int shapeShiftClassId = 0;
    int cumulativeProbability = 0;
    const int randomValue = static_cast<int>(::genrand64_real2() * 101.0 + 0.0);
    for (const FixedShapeShiftExtractionItemInfo& info : m_vExtractionItems) {
        cumulativeProbability += info.nProb;
        if (randomValue < cumulativeProbability) {
            shapeShiftClassId = g_ObjectDB.GetClassIdFromName(info.xwsItemName.c_str(), false);
            break;
        }
    }

    if (target != nullptr && target->IsItem() && extractionItem != nullptr && shapeShiftClassId != 0) {
        return extractionItem->ShapeShiftItem(user, static_cast<CItem*>(target), shapeShiftClassId, 0, m_nPeriod);
    }

    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    user->GetSocket()->Send(
        "chdddd", 0xfe, 300,
        // 2,
        ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

CSkillEffect_i_shape_shift_am_fixed::CSkillEffect_i_shape_shift_am_fixed() : m_nPeriod(-1) {}

CSkillEffect_i_shape_shift_am_fixed::CSkillEffect_i_shape_shift_am_fixed(int period) :
    m_nPeriod(period) {}

bool CSkillEffect_i_shape_shift_am_fixed::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a99a7c. Ctors 0x00aab574/0x00aab5d4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    int shapeShiftClassId = 0;
    int cumulativeProbability = 0;
    const int randomValue = static_cast<int>(::genrand64_real2() * 101.0 + 0.0);
    for (const FixedShapeShiftExtractionItemInfo& info : m_vExtractionItems) {
        cumulativeProbability += info.nProb;
        if (randomValue < cumulativeProbability) {
            shapeShiftClassId = g_ObjectDB.GetClassIdFromName(info.xwsItemName.c_str(), false);
            break;
        }
    }

    if (target != nullptr && target->IsItem() && extractionItem != nullptr && shapeShiftClassId != 0) {
        return extractionItem->ShapeShiftItem(user, static_cast<CItem*>(target), shapeShiftClassId, 0, m_nPeriod);
    }

    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    user->GetSocket()->Send(
        "chdddd", 0xfe, 300,
        // 2,
        ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

bool CSkillEffect_i_shape_shift_wp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a92c0c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    CItem* const shapeShiftItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING_EXTRACTION);
    User* const user = creature->CastUser();
    if (user != nullptr) {
        if (target != nullptr && target->IsItem() && extractionItem != nullptr && shapeShiftItem != nullptr) {
            const std::int64_t amount = user->CalcShapeShiftingFee(extractionItem, static_cast<CItem*>(target));
            return extractionItem->ShapeShiftItem(
                user, static_cast<CItem*>(target), shapeShiftItem, false, amount, m_nPeriod
            );
        }

        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        user->GetSocket()->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    }
    return false;
}

bool CSkillEffect_i_bless_shape_shift_wp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a92dd8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    CItem* const shapeShiftItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING_EXTRACTION);
    User* const user = creature->CastUser();
    if (user != nullptr) {
        if (target != nullptr && target->IsItem() && extractionItem != nullptr && shapeShiftItem != nullptr) {
            return extractionItem->ShapeShiftItem(user, static_cast<CItem*>(target), shapeShiftItem, true, 0, m_nPeriod);
        }

        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        user->GetSocket()->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    }
    return false;
}

bool CSkillEffect_i_restore_shape_shift_item::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a92f94.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    User* const user = creature->CastUser();
    if (user != nullptr) {
        if (target != nullptr && target->IsItem() && extractionItem != nullptr) {
            return extractionItem->RestoreShapeShiftItem(user, static_cast<CItem*>(target));
        }

        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        user->GetSocket()->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    }
    return false;
}

bool CSkillEffect_i_shape_shift_am::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a93108.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    CItem* const shapeShiftItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING_EXTRACTION);
    User* const user = creature->CastUser();
    if (user != nullptr) {
        if (target != nullptr && target->IsItem() && extractionItem != nullptr && shapeShiftItem != nullptr) {
            const std::int64_t amount = user->CalcShapeShiftingFee(extractionItem, static_cast<CItem*>(target));
            return extractionItem->ShapeShiftItem(
                user, static_cast<CItem*>(target), shapeShiftItem, false, amount, m_nPeriod
            );
        }

        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        user->GetSocket()->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    }
    return false;
}

bool CSkillEffect_i_bless_shape_shift_am::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a932d4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CItem* const extractionItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    CItem* const shapeShiftItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING_EXTRACTION);
    User* const user = creature->CastUser();
    if (user != nullptr) {
        if (target != nullptr && target->IsItem() && extractionItem != nullptr && shapeShiftItem != nullptr) {
            return extractionItem->ShapeShiftItem(user, static_cast<CItem*>(target), shapeShiftItem, true, 0, m_nPeriod);
        }

        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        user->GetSocket()->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    }
    return false;
}

bool CSkillEffect_i_position_change::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9c3cc.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr || creature->GetInZoneID() != target->GetInZoneID() ||
        ::genrand64_real1() * 100.0 > m_dProbability) {
        return false;
    }

    if (target->IsNPC() && target->CastNPC()->m_data->m_nExCrtEffect == 1) {
        return false;
    }

    User* const user = creature->CastUser();
    const FVector userPosition = user->GetPos();
    const FVector targetPosition = target->GetPos();
    if (targetCreature->IsEnemyTo(creature) && targetCreature->CheckBlockDebuff(creature)) {
        return false;
    }

    CWorld& world = CWorld::Inst();
    if (!world.MoveTo(user, targetPosition, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_position_change")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_position_change")
        L2_LOG_ERROR(
            Log,
            "i_position_change (caster) could not move object %u to (%.17g, %.17g, %.17g). Target movement is still "
            "attempted.",
            static_cast<unsigned>(user->Id()), targetPosition.x, targetPosition.y, targetPosition.z
        );
    } else {
        user->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(targetPosition.x), runtime::Win64CvttDoubleToInt32(targetPosition.y),
            runtime::Win64CvttDoubleToInt32(targetPosition.z), FTLT_BLINK, 0, 0, 0
        );
    }

    if (!world.MoveTo(targetCreature, userPosition, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_position_change")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in i_position_change")
        L2_LOG_ERROR(
            Log,
            "i_position_change (target) could not move object %u to (%.17g, %.17g, %.17g). No position rollback is "
            "attempted; the effect still reports success.",
            static_cast<unsigned>(targetCreature->Id()), userPosition.x, userPosition.y, userPosition.z
        );
    } else {
        targetCreature->GetController()->DoNothing(true);
        targetCreature->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(userPosition.x), runtime::Win64CvttDoubleToInt32(userPosition.y),
            runtime::Win64CvttDoubleToInt32(userPosition.z), FTLT_BLINK, 0, 0, 0
        );
    }
    return true;
}

bool CSkillEffect_i_knockback::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a9b298.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (creature->m_nId == target->m_nId || targetCreature->m_bzDefendingTraitPerfect[TRAIT_KNOCKBACK] ||
        targetCreature->CheckBlockDebuff(creature)) {
        return false;
    }

    if (skillInfo != nullptr) {
        const SkillOperateType operateType = skillInfo->m_OperateType;
        const bool infectionRequired = operateType == SKILL_DURATION || operateType == SKILL_DURATION_LEVEL_UP ||
            operateType == SKILL_SELFDURATION || operateType == SKILL_CHARGE_DURATION ||
            operateType == SKILL_AURA_ACTIVITY || operateType == SKILL_BACK_STEP_DURATION ||
            operateType == SKILL_AURA_ACTIVITY2 || operateType == SKILL_PARTY_INSTANT || operateType == SKILL_DUAL_PUMP1 ||
            operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
        if (infectionRequired && skillAction != nullptr && !skillAction->m_Infected) {
            return false;
        }
    }

    const FVector creaturePosition = creature->GetPos();
    const FVector targetPosition = target->GetPos();
    FVector movement{};
    if (!creature->m_ChargeData.IsZero()) {
        movement = creature->m_ChargeData.GetDir();
        movement.x *= static_cast<double>(m_Dist);
        movement.y *= static_cast<double>(m_Dist);
        movement.z *= static_cast<double>(m_Dist);
    } else {
        const double separation = std::sqrt(creaturePosition.DistCube(targetPosition));
        FVector direction{1.0, 1.0, 1.0};
        if (separation <= -0.00001 || 0.00001 <= separation) {
            direction.x = (targetPosition.x - creaturePosition.x) / separation;
            direction.y = (targetPosition.y - creaturePosition.y) / separation;
            direction.z = (targetPosition.z - creaturePosition.z) / separation;
        }
        movement.x = direction.x * static_cast<double>(m_Dist);
        movement.y = direction.y * static_cast<double>(m_Dist);
        movement.z = direction.z * static_cast<double>(m_Dist);
    }

    FVector destination{targetPosition.x + movement.x, targetPosition.y + movement.y, targetPosition.z + movement.z};
    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(
            targetPosition, destination, 0.0, collisionPosition, static_cast<int>(target->GetInZoneID()), false
        )) {
        movement.NormalizeCube();
        destination.x = collisionPosition.x - movement.x * 10.0;
        destination.y = collisionPosition.y - movement.y * 10.0;
        destination.z = collisionPosition.z - movement.z * 10.0;
    }

    bool distanceExhausted = true;
    double travelledDistance = 0.0;
    CWorld& world = CWorld::Inst();
    if (!world.CanGo(
            targetPosition, destination, collisionPosition, distanceExhausted, travelledDistance,
            static_cast<int>(creature->GetInZoneID()), false
        )) {
        destination = collisionPosition;
    }
    if (!world.IsValidPosition(static_cast<int>(destination.x), static_cast<int>(destination.y))) {
        return false;
    }

    targetCreature->GetController()->DoNothing(true);
    const double radians = std::atan2(destination.y, destination.x);
    targetCreature->m_data->SetDir(FAngle{static_cast<int>(radians * 65535.0 / 6.283185307179586)});
    targetCreature->BroadcastFlyToLocation(
        static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z), m_Mode, 0,
        m_Speed, m_Acceleration
    );
    world.MoveTo(targetCreature, destination, false);
    return true;
}

bool CSkillEffect_i_pull::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a9c9c0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    if (skillInfo != nullptr) {
        const SkillOperateType operateType = skillInfo->m_OperateType;
        const bool infectionRequired = operateType == SKILL_DURATION || operateType == SKILL_DURATION_LEVEL_UP ||
            operateType == SKILL_SELFDURATION || operateType == SKILL_CHARGE_DURATION ||
            operateType == SKILL_AURA_ACTIVITY || operateType == SKILL_BACK_STEP_DURATION ||
            operateType == SKILL_AURA_ACTIVITY2 || operateType == SKILL_PARTY_INSTANT || operateType == SKILL_DUAL_PUMP1 ||
            operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
        if (infectionRequired && skillAction != nullptr && !skillAction->m_Infected) {
            return false;
        }
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->m_data->m_nExCrtEffect != 0) {
        return false;
    }

    if (skillInfo->m_AffectObjectType == SAFOT_NOT_FRIEND && creature->IsSummonedNpc() &&
        targetCreature->IsPCOrPCRelatedNPC()) {
        User* targetOwner = nullptr;
        if (targetCreature->IsPC()) {
            targetOwner = targetCreature->CastUser();
        } else if (targetCreature->IsNPC()) {
            targetOwner = targetCreature->CastNPC()->GetMaster();
        }
        if (targetOwner != nullptr && targetOwner == creature->CastNPC()->GetMaster()) {
            return false;
        }
    }

    const FVector creaturePosition = creature->GetPos();
    const FVector targetPosition = targetCreature->GetPos();
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
        creaturePosition.x + unitX * 40.0, creaturePosition.y + unitY * 40.0, creaturePosition.z + unitZ * 40.0};
    FVector collisionPosition{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    CWorld& world = CWorld::Inst();
    if (!world.CanGo(
            targetPosition, destination, collisionPosition, distanceExhausted, travelledDistance,
            static_cast<int>(creature->GetInZoneID()), false
        ) ||
        !world.IsValidPosition(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y)
        )) {
        return false;
    }

    targetCreature->GetController()->DoNothing(true);
    const double radians = std::atan2(creaturePosition.y - targetPosition.y, creaturePosition.x - targetPosition.x);
    targetCreature->m_data->SetDir(FAngle{runtime::Win64CvttDoubleToInt32(radians * 65535.0 / 6.283185307179586)});
    targetCreature->BroadcastFlyToLocation(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), FTLT_PULL, 0, m_Speed, m_Acceleration
    );
    world.MoveTo(targetCreature, destination, false);
    return true;
}

bool CSkillEffect_i_t_hp_critical::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a92260.
    L2_GUARD_FUNCTION();
    if ((skillInfo != nullptr && (skillInfo->m_nIsMagic == 0 || skillInfo->m_nIsMagic == 6)) ||
        !L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }
    if (targetCreature->CastNPC() != nullptr &&
        (targetCreature->m_nClassId == 0x0f772f || targetCreature->m_nClassId == 0x0f7730)) {
        return false;
    }

    const double probability =
        L2SkillFunc::GetMagicCriticalProb(*creature, *targetCreature, skillInfo, creature->WitBonusF());
    if (genrand64_real1() * 100.0 <= probability) {
        if (!creature->IsPCOrPCRelatedNPC() || !targetCreature->IsPC() || m_Amount >= 0) {
            L2SkillFunc::IncreaseHP(targetCreature, 0, SFCT_DIFF, static_cast<double>(m_Amount), 0, true);
        } else if (targetCreature->m_SkillMod.CanGetDamageHP(creature, targetCreature)) {
            L2SkillFunc::DecreaseCP(targetCreature, creature->m_nId, SFCT_DIFF, static_cast<double>(-m_Amount), 0);
        }

        CCreature::SendMagicAttckInfo(
            creature, targetCreature, m_Amount <= 0 ? MagicAttackInfo::Critical : MagicAttackInfo::HealCritical
        );
        creature->SendSystemMessage(SystemMessageId::MAGIC_CRITICAL_HIT);
    }
    return true;
}

bool CSkillEffect_i_target_cancel::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8fe14.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsBoss() || targetCreature->IsZzoldagu() ||
        (targetCreature->IsNPC() && targetCreature->m_data->m_nExCrtEffect == 1)) {
        return false;
    }

    const double probability =
        L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, targetCreature, skillInfo);
    if (genrand64_real1() * 100.0 < probability && targetCreature->m_data->m_TargetIndex != 0) {
        targetCreature->DoNothing();
        targetCreature->ChangeTarget(nullptr, CTR_BY_SKILL);
        targetCreature->DoNothing();
    }
    return true;
}

bool CSkillEffect_i_target_me::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8ffb4.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsBoss() || targetCreature->IsZzoldagu() ||
        (targetCreature->IsNPC() && targetCreature->m_data->m_nExCrtEffect == 1)) {
        return false;
    }
    if (targetCreature->IsPC()) {
        CCreature* const selected = targetCreature->m_spTarget.GetCreature();
        if (selected != nullptr && selected->m_nId == creature->m_nId) {
            return false;
        }
    }

    const double probability =
        L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, targetCreature, skillInfo);
    if (probability > genrand64_real1() * 100.0 || bIgnoreProb) {
        targetCreature->Action(creature, targetCreature->m_AreaMod.m_bIsInBattleField, false);
    }
    return true;
}

double CSkillEffect_i_death::CalculateProb(CCreature* creature, CCreature* target, double probability) const {
    // Current L2Server 0x00a97910.
    const double levelDifference = std::max(static_cast<double>(target->m_data->m_nLevel - creature->m_data->m_nLevel), 1.0);
    return (1.0 - std::pow((levelDifference - 1.0) / levelDifference, 2.35) +
               ((levelDifference - 1.0) * levelDifference * (levelDifference - 2.0)) /
                   std::pow(levelDifference + 1.0, 6.0)) *
        probability;
}

bool CSkillEffect_i_death::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a979f8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsBoss() || targetCreature->IsZzoldagu() ||
        (targetCreature->IsNPC() && targetCreature->m_data->m_nExCrtEffect == 1) ||
        skillInfo->m_nMagicLevel < targetCreature->m_data->m_nLevel - 6 ||
        !targetCreature->m_SkillMod.CanGetDamageHP(creature, targetCreature) || targetCreature->m_SkillMod.m_bIsPKProtect) {
        return false;
    }

    const auto instantKillResist = [targetCreature]() {
        double resistance = targetCreature->m_SkillDurationMod.m_dInstantKillResist;
        if (resistance < 0.0) {
            resistance = 0.0;
        } else if (resistance > 100.0) {
            resistance = 100.0;
        }
        return resistance;
    };
    const auto succeeds = [&](double probability) {
        const double adjustedProbability = CalculateProb(creature, targetCreature, probability) *
            targetCreature->GetSkillAttributeBonus(targetCreature, skillInfo, false) *
            ((100.0 - instantKillResist()) / 100.0);
        return adjustedProbability > genrand64_real1() * 100.0;
    };

    double contribution = 0.0;
    if (succeeds(m_Prob)) {
        creature->SendSystemMessage(SystemMessageId::LETHAL_STRIKE);
        if (targetCreature->IsPC()) {
            targetCreature->SendSystemMessage(SystemMessageId::RECEIVED_LETHAL_STRIKE);
            contribution = (targetCreature->m_data->m_dCP - 1.0) + (targetCreature->m_data->m_dHP - 1.0);
            targetCreature->m_data->SetCP(1.0);
            targetCreature->m_data->SetHP(1.0);
            targetCreature->SendStatusUpdate(VCP_CP, 4, 0);
            targetCreature->SendStatusUpdate(VCP_HP, 4, 0);
            targetCreature->m_data->m_NeedUpdatePacketBitSet.ClearBit(13);
        } else {
            contribution = targetCreature->m_data->m_dHP - 1.0;
            targetCreature->m_data->SetHP(1.0);
            targetCreature->SendStatusUpdate(VCP_HP, 4, 0);
        }
        targetCreature->m_data->m_NeedUpdatePacketBitSet.ClearBit(11);
    } else if (succeeds(m_Prob2)) {
        creature->SendSystemMessage(SystemMessageId::HALF_KILL);
        if (targetCreature->IsPC()) {
            targetCreature->SendSystemMessage(SystemMessageId::CP_DRAINED_BY_HALF_KILL);
            contribution = targetCreature->m_data->m_dCP - 1.0;
            targetCreature->m_data->SetCP(1.0);
            targetCreature->SendStatusUpdate(VCP_CP, 4, 0);
            targetCreature->m_data->m_NeedUpdatePacketBitSet.ClearBit(13);
        } else {
            contribution = targetCreature->m_data->m_dHP * 0.5;
            targetCreature->m_data->SetHP(contribution);
            targetCreature->SendStatusUpdate(VCP_HP, 4, 0);
            targetCreature->m_data->m_NeedUpdatePacketBitSet.ClearBit(11);
        }
    }

    targetCreature->SafeAccumulateContribute(creature, contribution);
    return true;
}

CSkillEffect_i_float_away::CSkillEffect_i_float_away(const int flyType, const int flyHeight) :
    m_nFlyType(flyType), m_nFlyHeight(flyHeight) {
    // Current L2Server 0x00ca2dc4.
}

bool CSkillEffect_i_float_away::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a939d8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr || (m_nFlyType != FTLT_FLOAT_STAND && m_nFlyType != FTLT_FLOAT_LAYDOWN) ||
        m_nFlyHeight < 1 || m_nFlyHeight > 999 || (target->IsNPC() && targetCreature->m_data->m_nExCrtEffect == 1)) {
        return false;
    }

    L2SkillFunc::CancelAllActionForMove(targetCreature);
    FVector position = targetCreature->GetPos();
    position.z += static_cast<double>(m_nFlyHeight);
    targetCreature->BroadcastFlyToLocation(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), static_cast<FLY_TO_LOCATION_TYPE>(m_nFlyType), 0, 0, 0
    );
    return true;
}

CSkillEffect_i_npc_kill::CSkillEffect_i_npc_kill(const double probability) : dProb(probability) {
    // Current L2Server 0x00ca49f4.
}

bool CSkillEffect_i_npc_kill::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a90288.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature() || !target->IsNPC() || !target->IsAlive() || !target->IsSummonOrPet()) {
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(target);
    double skillAttributeBonus;
    int targetLevel;
    {
        L2_GUARD(*targetCreature->m_lock);
        skillAttributeBonus = creature->GetSkillAttributeBonus(targetCreature, skillInfo, false);
        targetLevel = targetCreature->Level();
    }

    if ((skillInfo->m_nMagicLevel >= 1 && targetLevel - 9 > skillInfo->m_nMagicLevel) ||
        genrand64_real1() * 100.0 + 0.0 > skillAttributeBonus * dProb) {
        return false;
    }

    CSummon* const summon = target->CastSummon();
    User* const master = summon->GetMaster();
    if (master != nullptr) {
        if (!target->IsSummon()) {
            master->DespawnPet(true, true, DESPAWN_PET_NONE, false);
        } else {
            master->DespawnSummon(target->CastSummon(), true, false);
        }
    }
    return true;
}

CSkillEffect_i_teleport::CSkillEffect_i_teleport(FVector position) : vPos(position) {
    // Current L2Server 0x00ca497c.
}

bool CSkillEffect_i_teleport::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a97ea8.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->IsPC()) {
        User* const user = targetCreature->CastUser();
        if (user->HasFlagOfSiege()) {
            user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
            return false;
        }
        if (user->m_PledgeGameUser.IsPlayer()) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
            return false;
        }
        if (user->IsRidingWyvern() || user->IsInAirShip() || static_cast<int>(user->m_data->m_moveType) == CMT_FLY) {
            return false;
        }
    }

    targetCreature->TeleportToLocation(
        runtime::Win64CvttDoubleToInt32(vPos.x), runtime::Win64CvttDoubleToInt32(vPos.y),
        runtime::Win64CvttDoubleToInt32(vPos.z), 0, 0x10000, TeleportSourceType::tst_i_teleport, creature, 0
    );
    return true;
}

CSkillRandomUnit::CSkillRandomUnit(std::u16string name, double probability) : xwsSkillName(name), dProb(probability) {
    // Current L2Server 0x00ca19cc copies the by-value string.
}

CSkillEffect_i_call_my_skill_list::CSkillEffect_i_call_my_skill_list(const std::vector<std::int64_t>& skillIds) {
    // Current L2Server 0x00ca5a18 copies the low target int from every
    // parser-supplied 64-bit semantic value.
    for (const std::int64_t skillId : skillIds) {
        m_calledSkillIdList.push_back(runtime::Int32FromBits(static_cast<std::uint32_t>(skillId)));
    }
}

bool CSkillEffect_i_call_my_skill_list::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa5cec.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if ((skillInfo->m_nMagicLevel != -2 || skillInfo->m_eDebuff == DB_GOOD) &&
        (!L2Common::IsAliveCreature(creature) || targetCreature->m_data->m_bIsAlive == 0)) {
        return false;
    }

    std::vector<const CSkillInfo*> calledSkills;
    for (const int calledSkillId : m_calledSkillIdList) {
        const int level = creature->GetAcquiredSkillLevel(calledSkillId);
        if (level == -1) {
            creature->SendSystemMessage(SystemMessageId::SKILL_USE_FAILED);
            return false;
        }
        const int subLevel = creature->GetAcquiredSkillSubLevel(calledSkillId);
        const CSkillInfo* const calledSkill = g_SkillDB.GetSkillInfo(CSkillKey(calledSkillId, level, subLevel), nullptr);

        if (calledSkill->m_TargetType == STGT_SELF || calledSkill->m_TargetType == STGT_GROUND) {
            if (skillInfo->CheckOperateCond(creature, creature, CSkillInfo::OCT_BOTH) != -2) {
                creature->SendSystemMessage(SystemMessageId::SKILL_USE_FAILED);
                return false;
            }
        } else if (!skillInfo->IsValidTarget(creature, targetCreature, false) ||
            skillInfo->CheckOperateCond(creature, targetCreature, CSkillInfo::OCT_BOTH) != -2) {
            creature->SendSystemMessage(SystemMessageId::SKILL_USE_FAILED);
            return false;
        }

        const std::uint32_t usedTime = creature->m_SkillReuseDelay.GetSkillUsedTime(calledSkillId, level, subLevel);
        if (runtime::GetTickCount() < usedTime) {
            creature->SendSystemMessage(SystemMessageId::SKILL_USE_FAILED);
            return false;
        }
        calledSkills.push_back(calledSkill);
    }

    for (const CSkillInfo* const calledSkill : calledSkills) {
        calledSkill->ActivateSkill(creature, targetCreature, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        creature->BroadcastCubicSkillUse(*targetCreature, calledSkill, 0, 0);
        g_npcserver.Send(
            "cddddddd", 0x0e, static_cast<int>(creature->m_nId), creature->GetIndex(),
            static_cast<int>(targetCreature->m_nId), calledSkill->m_nSkillId, calledSkill->m_nLevel,
            calledSkill->m_nSubLevel, calledSkill->m_nNpcNotice
        );
    }
    return true;
}

CSkillEffect_i_call_random_skill_consume_item::CSkillEffect_i_call_random_skill_consume_item() {
    // Current L2Server 0x00ca2ecc does not initialize
    // m_vSkillRandomUnit. Parser action 0x3a3 immediately supplies it.
}

void CSkillEffect_i_call_random_skill_consume_item::SetRandomUnitList(std::vector<CSkillRandomUnit*>* randomUnits) {
    m_vSkillRandomUnit = randomUnits;
}

void CSkillEffect_i_call_random_skill_consume_item::OnPostLoad() {
    // Current L2Server 0x00aa6180.
    L2_GUARD_FUNCTION();
    for (const CSkillRandomUnit* const randomUnit : *m_vSkillRandomUnit) {
        const std::wstring skillName = target_utf16::ToHostCodeUnits(randomUnit->xwsSkillName.c_str());
        const CSkillInfo* const loadedSkill = g_SkillDB.GetSkillInfoByName(skillName.c_str());
        if (loadedSkill == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"i_call_random_skill_consume_item has problem. "
            //     L"cannot find skill!")
            // L2_LOG_ERROR(Log,
            //     L"i_call_random_skill_consume_item has problem. "
            //     L"cannot find skill!")
            L2_LOG_ERROR(
                Log,
                "i_call_random_skill_consume_item cannot resolve skill '%s'; a null entry is retained in the selection "
                "list.",
                Utf16ToUtf8(randomUnit->xwsSkillName.c_str()).c_str()
            );
        }
        m_vSkillList.push_back(loadedSkill);
    }
}

bool CSkillEffect_i_call_random_skill_consume_item::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aaa924.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || target == nullptr || !target->IsCreature()) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature->m_data->m_bIsAlive == 0) {
        return false;
    }

    double dice = genrand64_real1() * 100.0 + 0.0;
    for (std::size_t index = 0; index < m_vSkillRandomUnit->size(); ++index) {
        const CSkillRandomUnit* const randomUnit = runtime::CheckedVectorAt((*m_vSkillRandomUnit), index);
        if (dice >= randomUnit->dProb) {
            dice -= randomUnit->dProb;
            continue;
        }

        const CSkillInfo* const selectedSkill = runtime::CheckedVectorAt(m_vSkillList, index);
        if (selectedSkill == nullptr) {
            break;
        }

        User* const user = creature->CastUser();
        if (user->IsNowTrade()) {
            user->TradeCancel(false);
        }

        int consumedItemClassId = skillInfo->m_nCapsuleReduceItemID;
        int consumedItemCount = -1;
        if (consumedItemClassId != -1) {
            std::array<ItemManip, 100> requests{};
            requests[0].mClassId = consumedItemClassId;
            requests[0].mCount = -1;

            bool executeSelectedSkill = false;
            {
                L2_GUARD(user->m_ManipulateQueueLock);
                CPacketHelperEx<20480> packet;
                User::etcMsg message = user->SaveEtcMsg(
                    false, false, static_cast<int>(user->m_nId), static_cast<int>(targetCreature->m_nId), 0,
                    target_utf16::FromHostCodeUnits(selectedSkill->m_pszName), std::u16string(), 0
                );
                User::ManipulateQueue queue = user->SaveManipulateQueue(
                    ManipulateType::MANI_SKILL_EFFECT_INSTANT, 5, 1, requests.data(), nullptr, 0, packet, false,
                    ItemGetReason::Etc, message
                );
                const User::ManiItemResultType result = user->_SetManiItemJob(queue);
                if (result == User::MRT_NOJOB) {
                    if (!user->_HandleItemManipulation(queue)) {
                        user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
                        return false;
                    }
                    executeSelectedSkill = true;
                } else if (result == User::MRT_JOBMAP) {
                    user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
                    return false;
                } else if (result == User::MRT_JOBNOTMAP) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                    //     L"SkillFxFunc_instant.cpp",
                    //     0x3711)
                    // Noise: MRT_JOBNOTMAP means the request was queued behind an active item operation. Normal waiting is
                    // not an error. L2_LOG_ERROR(Log,
                    //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                    //     L"SkillFxFunc_instant.cpp",
                    //     0x3711)
                    return true;
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                    //     L"SkillFxFunc_instant.cpp",
                    //     0x3716)
                    // L2_LOG_ERROR(Log,
                    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                    //     L"SkillFxFunc_instant.cpp",
                    //     0x3716)
                    L2_LOG_ERROR(
                        Log,
                        "i_call_random_skill_consume_item could not schedule item changes: unexpected queue result %d; the "
                        "effect fails.",
                        static_cast<int>(result)
                    );
                    return false;
                }
            }
            if (!executeSelectedSkill) {
                return false;
            }
        } else {
            if (skillInfo->m_nConsumeItem < 1 || skillInfo->m_nConsumeItemCount < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"i_call_random_skill_consume_item, consume item or "
                //     L"item amount setting has problem!!")
                // L2_LOG_ERROR(Log,
                //     L"i_call_random_skill_consume_item, consume item or "
                //     L"item amount setting has problem!!")
                L2_LOG_ERROR(
                    Log,
                    "i_call_random_skill_consume_item has nonpositive consumed item ID %d or count %lld; the effect fails.",
                    skillInfo->m_nConsumeItem, static_cast<long long>(skillInfo->m_nConsumeItemCount)
                );
                return false;
            }
            CObject* const consumeObject = g_ObjectDB.GetObjectW(skillInfo->m_nConsumeItem, false);
            if (consumeObject == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"i_call_random_skill_consume_item, cannot find "
                //     L"consume item")
                // L2_LOG_ERROR(Log,
                //     L"i_call_random_skill_consume_item, cannot find "
                //     L"consume item")
                L2_LOG_ERROR(
                    Log, "i_call_random_skill_consume_item cannot find consumed item prototype %d; the effect fails.",
                    skillInfo->m_nConsumeItem
                );
                return false;
            }
            if (static_cast<CItem*>(consumeObject)->m_const->m_enumDefaultAction != ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"i_call_random_skill_consume_item, default action "
                //     L"of consume item is not "
                //     L"ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS")
                // L2_LOG_ERROR(Log,
                //     L"i_call_random_skill_consume_item, default action "
                //     L"of consume item is not "
                //     L"ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS")
                L2_LOG_ERROR(
                    Log,
                    "i_call_random_skill_consume_item requires consumed item %d to use REDUCE_ON_SKILL_SUCCESS; its default "
                    "action does not match and the effect fails.",
                    skillInfo->m_nConsumeItem
                );
                return false;
            }
            if (!user->m_inv.HaveItemByClassId(skillInfo->m_nConsumeItem, skillInfo->m_nConsumeItemCount, false, false)) {
                user->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
            consumedItemClassId = skillInfo->m_nConsumeItem;
            consumedItemCount = runtime::NegateInt32Wrapping(static_cast<int>(skillInfo->m_nConsumeItemCount));
        }

        if (selectedSkill->ActivateSkill(user, targetCreature, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false)) {
            CLogFormatter formatter;
            formatter.WriteLog1_1(
                0x233c, user, nullptr, nullptr, nullptr, nullptr, consumedItemClassId, consumedItemCount,
                selectedSkill->m_nSkillId, 0, 0, 0, 0
            );
            user->BroadcastCubicSkillUse(*targetCreature, selectedSkill, 0, 0);
            g_npcserver.Send(
                "cddddddd", 0x0e, static_cast<int>(user->m_nId), user->GetIndex(), static_cast<int>(targetCreature->m_nId),
                selectedSkill->m_nSkillId, selectedSkill->m_nLevel, selectedSkill->m_nSubLevel, selectedSkill->m_nNpcNotice
            );
            formatter.WriteLog1_1(
                0x233c, user, nullptr, nullptr, nullptr, nullptr, consumedItemClassId, consumedItemCount,
                selectedSkill->m_nSkillId, 0, 0, 0, 0
            );
        }
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"i_call_random_skill_consume_item cannot select skill, "
    //     L"dice has problem")
    // L2_LOG_ERROR(Log,
    //     L"i_call_random_skill_consume_item cannot select skill, "
    //     L"dice has problem")
    L2_LOG_ERROR(
        Log,
        "i_call_random_skill_consume_item obtained no skill: remaining roll %.17g across %llu entries; probabilities were "
        "exhausted or the selected definition is missing. The effect fails.",
        dice, static_cast<unsigned long long>(m_vSkillRandomUnit->size())
    );
    return false;
}

CSkillEffect_i_random_call_skill::CSkillEffect_i_random_call_skill() {
    // Current L2Server 0x00ca833c.
}

bool CSkillEffect_i_random_call_skill::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a9a374.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    if ((skillInfo->m_nMagicLevel != -2 || skillInfo->m_eDebuff == DB_GOOD) &&
        (!L2Common::IsAliveCreature(creature) || targetCreature->m_data->m_bIsAlive == 0)) {
        return false;
    }

    const int skillCount = static_cast<int>(m_vRandomSkillNameList.size());
    if (skillCount < 0) {
        return false;
    }

    const double dice = genrand64_real1() * 100.0 + 0.0;
    double accumulatedProbability = 0.0;
    for (int index = 0; index < skillCount; ++index) {
        accumulatedProbability += runtime::CheckedVectorAt(m_nRandomSkillProbList, static_cast<std::size_t>(index));
        if (dice >= accumulatedProbability) {
            continue;
        }

        const std::wstring skillName = target_utf16::ToHostCodeUnits(
            runtime::CheckedVectorAt(m_vRandomSkillNameList, static_cast<std::size_t>(index)).c_str()
        );
        const CSkillInfo* const selectedSkill = g_SkillDB.GetSkillInfoByName(skillName.c_str());
        if (selectedSkill != nullptr) {
            selectedSkill->ActivateSkill(creature, targetCreature, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            creature->BroadcastCubicSkillUse(*targetCreature, selectedSkill, 0, 0);
            return true;
        }
    }

    creature->SendSystemMessage(SystemMessageId::SKILL_USE_FAILED);
    return true;
}

bool CSkillEffect_i_change_skill_level::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a98afc.
    L2_GUARD_FUNCTION();
    if (!target->IsPC()) {
        return false;
    }

    User* const targetUser = target->CastUser();
    const int oldLevel = targetUser->GetAcquiredSkillLevel(nTargetSkillID);
    const int subLevel = targetUser->GetAcquiredSkillSubLevel(nTargetSkillID);
    if (oldLevel == -1 || nDownLevel >= 0) {
        return false;
    }
    const int newLevel = oldLevel + nDownLevel;

    std::uint32_t actorDbId = 0;
    std::uint32_t actorAccountId = 0;
    std::wstring actorName;
    std::wstring actorAccountName;
    if (creature->IsNPC()) {
        actorDbId = static_cast<std::uint32_t>(creature->CastNPC()->GetNPCId());
        actorAccountId = static_cast<std::uint32_t>(creature->CastNPC()->GetNPCId());
        actorName = std::to_wstring(actorDbId);
        actorAccountName = std::to_wstring(actorAccountId);
    } else if (creature->IsPC()) {
        actorDbId = creature->CastUser()->GetDBID();
        actorAccountId = creature->CastUser()->GetAccountID();
        actorName = Utf16ToWide(creature->GetName());
        actorAccountName = target_utf16::ToHostCodeUnits(creature->CastUser()->m_sAccountName.data());
    }

    const FVector position = targetUser->GetPos();
    if (newLevel < 1) {
        {
            L2_GUARD(targetUser->m_AcquiredSkillsSect);
            targetUser->DeleteAcquireSkill(nTargetSkillID);
            targetUser->SendDeletedSkillToDB(nTargetSkillID);
        }
        targetUser->ValidateChanged(VB_MAX);
        targetUser->SendSystemMessage(SystemMessageId::SHILEN_BREATH_PURIFIED);
    } else {
        {
            L2_GUARD(targetUser->m_AcquiredSkillsSect);
            if (targetUser->AcquireSkill(nTargetSkillID, newLevel, subLevel, true, true)) {
                targetUser->SendAcquiredSkillToDB(nTargetSkillID, newLevel, subLevel, true);
            }
        }
        targetUser->ValidateChanged(VB_MAX);
        targetUser->GetSocket()->SendSystemMessage_d(SystemMessageId::AFFLICTED_BY_SHILEN_BREATH_LEVEL, newLevel);
    }

    targetUser->SetMaxMagicLevel();
    targetUser->m_nParamChanged |= std::int64_t{PCB_USER};
    targetUser->SendSkillList(nullptr, false, 0);

    const std::wstring targetAccountName = target_utf16::ToHostCodeUnits(targetUser->m_sAccountName.data());
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%s,%s,%s,%s,",
    //     static_cast<std::int64_t>(0x371),
    //     static_cast<std::int64_t>(actorDbId),
    //     static_cast<std::int64_t>(actorAccountId),
    //     static_cast<std::int64_t>(targetUser->GetDBID()),
    //     static_cast<std::int64_t>(targetUser->GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(targetUser->m_data->m_Race),
    //     static_cast<std::int64_t>(targetUser->m_data->m_Sex),
    //     static_cast<std::int64_t>(targetUser->m_data->m_nClass),
    //     static_cast<std::int64_t>(targetUser->m_data->m_nLevel),
    //     static_cast<std::int64_t>(nTargetSkillID),
    //     static_cast<std::int64_t>(oldLevel),
    //     static_cast<std::int64_t>(newLevel),
    //     actorName.c_str(),
    //     actorAccountName.c_str(),
    //     targetUser->GetName(),
    //     targetAccountName.c_str())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%s,%s,%s,%s,",
        static_cast<std::int64_t>(0x371), static_cast<std::int64_t>(actorDbId), static_cast<std::int64_t>(actorAccountId),
        static_cast<std::int64_t>(targetUser->GetDBID()), static_cast<std::int64_t>(targetUser->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(targetUser->m_data->m_Race), static_cast<std::int64_t>(targetUser->m_data->m_Sex),
        static_cast<std::int64_t>(targetUser->m_data->m_nClass), static_cast<std::int64_t>(targetUser->m_data->m_nLevel),
        static_cast<std::int64_t>(nTargetSkillID), static_cast<std::int64_t>(oldLevel), static_cast<std::int64_t>(newLevel),
        actorName.c_str(), actorAccountName.c_str(), targetUser->GetName(), targetAccountName.c_str()
    );
    return true;
}

CSkillEffect_i_call_skill::CSkillEffect_i_call_skill(std::u16string skillName) :
    m_pSkillInfo(nullptr), m_SkillName(std::move(skillName)) {
    // Current L2Server 0x00ca4064.
}

void CSkillEffect_i_call_skill::OnPostLoad() {
    // Current L2Server 0x00a9bf00.
    const std::wstring skillName = target_utf16::ToHostCodeUnits(m_SkillName.c_str());
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    m_SkillName.assign(std::u16string_view{});
}

bool CSkillEffect_i_call_skill::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e40c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!((skillInfo->m_nMagicLevel == -2 && skillInfo->m_eDebuff != DB_GOOD) ||
            (L2Common::IsAliveCreature(creature) && targetCreature->m_data->m_bIsAlive != 0))) {
        return false;
    }

    m_pSkillInfo->ActivateSkill(creature, targetCreature, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    creature->BroadcastCubicSkillUse(*targetCreature, m_pSkillInfo, 0, 0);
    g_npcserver.Send(
        "cddddddd", 0x0e, static_cast<int>(creature->m_nId), creature->GetIndex(), static_cast<int>(targetCreature->m_nId),
        m_pSkillInfo->m_nSkillId, m_pSkillInfo->m_nLevel, m_pSkillInfo->m_nSubLevel, m_pSkillInfo->m_nNpcNotice
    );
    return true;
}

CSkillEffect_i_call_skill_by_target::CSkillEffect_i_call_skill_by_target(std::u16string skillName) :
    m_pSkillInfo(nullptr), m_SkillName(std::move(skillName)) {
    // Current L2Server 0x00ca4198.
}

void CSkillEffect_i_call_skill_by_target::OnPostLoad() {
    // Current L2Server 0x00a9bf00.
    const std::wstring skillName = target_utf16::ToHostCodeUnits(m_SkillName.c_str());
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    m_SkillName.assign(std::u16string_view{});
}

bool CSkillEffect_i_call_skill_by_target::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e5d4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || target == nullptr || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (user == nullptr || user->m_data->m_bIsAlive == 0) {
        return false;
    }

    m_pSkillInfo->ActivateSkill(user, user, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    user->BroadcastCubicSkillUse(*user, m_pSkillInfo, 0, 0);
    return true;
}

CSkillEffect_i_call_skill_by_target_divide::CSkillEffect_i_call_skill_by_target_divide(
    std::u16string firstSkillName, std::u16string secondSkillName
) :
    m_pSkillInfo{nullptr, nullptr}, m_SkillName{std::move(secondSkillName), std::move(firstSkillName)} {
    // Current L2Server 0x00ca4240. The current constructor deliberately
    // places the second source argument in slot zero and the first in slot
    // one. CheckPremiumType below indexes those slots directly.
}

void CSkillEffect_i_call_skill_by_target_divide::OnPostLoad() {
    // Current L2Server 0x00a9bf50.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < 2; ++index) {
        const std::wstring skillName = target_utf16::ToHostCodeUnits(m_SkillName[index].c_str());
        m_pSkillInfo[index] = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
        m_SkillName[index].assign(std::u16string_view{});
    }
}

bool CSkillEffect_i_call_skill_by_target_divide::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e708.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || target == nullptr || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (user == nullptr || user->m_data->m_bIsAlive == 0) {
        return false;
    }

    CSkillInfo* const selectedSkill =
        user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? m_pSkillInfo[0] : m_pSkillInfo[1];
    selectedSkill->ActivateSkill(user, user, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    return true;
}

CSkillEffect_i_call_skill_by_abnormal_slot::CSkillEffect_i_call_skill_by_abnormal_slot(
    std::set<AbnormalStatusSlotType> abnormalSlotTypes, std::u16string skillName
) :
    m_pSkillInfo(nullptr), m_SkillName(std::move(skillName)), m_setAbnormalSlotType(std::move(abnormalSlotTypes)) {
    // Current L2Server 0x00ca63e8.
}

void CSkillEffect_i_call_skill_by_abnormal_slot::OnPostLoad() {
    // Current L2Server 0x00a9bf00 (ICF with i_call_skill and
    // i_call_skill_by_target).
    const std::wstring skillName = target_utf16::ToHostCodeUnits(m_SkillName.c_str());
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    m_SkillName.assign(std::u16string_view{});
}

bool CSkillEffect_i_call_skill_by_abnormal_slot::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9c038.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    std::size_t matchedSlotCount = 0;
    for (const AbnormalStatusSlotType slot : m_setAbnormalSlotType) {
        if (targetCreature->m_data->IsAbnormalType(slot)) {
            ++matchedSlotCount;
        }
    }

    if (matchedSlotCount == m_setAbnormalSlotType.size()) {
        m_pSkillInfo->ActivateSkill(creature, targetCreature, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        creature->BroadcastCubicSkillUse(*targetCreature, m_pSkillInfo, 0, 0);
    }
    return true;
}

CSkillEffect_i_call_skill_by_force::CSkillEffect_i_call_skill_by_force() {
    // Current L2Server 0x00ca6c20.
}

void CSkillEffect_i_call_skill_by_force::AddForceSkill(
    int energyLevel, int activateRatio, std::u16string skillName, SkillTargetTypeEnum targetType
) {
    // Current L2Server 0x00aa2e04.
    L2_GUARD_FUNCTION();
    const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName.c_str());
    CSkillInfo* const skillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(hostSkillName.c_str()));
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid skill name[%s] on loading "
        //     L"i_call_skill_by_force",
        //     L"SkillFxFunc_instant.cpp",
        //     0x3078,
        //     hostSkillName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid skill name[%s] on loading "
        //     L"i_call_skill_by_force",
        //     L"SkillFxFunc_instant.cpp",
        //     0x3078,
        //     hostSkillName.c_str())
        L2_LOG_ERROR(
            Log, "i_call_skill_by_force cannot resolve skill '%s' for energy level %d; this mapping is omitted.",
            Utf16ToUtf8(skillName.c_str()).c_str(), energyLevel
        );
        return;
    }

    const ForSkillData data{
        activateRatio,
        0,
        skillInfo,
        targetType,
        0,
    };
    const auto inserted = m_ForceSkillMap.emplace(energyLevel, data);
    if (!inserted.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated energy level[%d] on loading "
        //     L"i_call_skill_by_force",
        //     L"SkillFxFunc_instant.cpp",
        //     0x3081,
        //     energyLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] duplicated energy level[%d] on loading "
        //     L"i_call_skill_by_force",
        //     L"SkillFxFunc_instant.cpp",
        //     0x3081,
        //     energyLevel)
        L2_LOG_ERROR(
            Log, "i_call_skill_by_force already has an energy-level %d mapping; the existing mapping is retained.",
            energyLevel
        );
    }
}

bool CSkillEffect_i_call_skill_by_force::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9e738.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }

    const auto forceSkill = m_ForceSkillMap.find(creature->m_nEnergyCount);
    if (forceSkill == m_ForceSkillMap.end()) {
        return false;
    }

    const int roll = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 101.0);
    const ForSkillData& data = forceSkill->second;
    if (roll <= data.activateRatio) {
        data.pSkillInfo->ActivateTriggerSkill(*creature, target, data.targetType);
    }
    return true;
}

CSkillEffect_i_call_skill_by_targetdirection::CSkillEffect_i_call_skill_by_targetdirection(
    std::u16string skillName, int direction, int range
) :
    m_SkillName(std::move(skillName)), m_nDirection(direction), m_nRange(range), m_pSkillInfo(nullptr) {
    // Current L2Server 0x00ca2c1c.
}

void CSkillEffect_i_call_skill_by_targetdirection::OnPostLoad() {
    // Current L2Server 0x00a9d070.
    const std::wstring skillName = target_utf16::ToHostCodeUnits(m_SkillName.c_str());
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
    m_SkillName.assign(std::u16string_view{});
}

bool CSkillEffect_i_call_skill_by_targetdirection::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9a0f4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const FVector targetPosition = target->GetPos();
    const FVector creaturePosition = creature->GetPos();
    FVector direction{
        creaturePosition.x - targetPosition.x,
        creaturePosition.y - targetPosition.y,
        creaturePosition.z - targetPosition.z,
    };
    direction.NormalizeSquare();

    constexpr double kTargetTwoPi = 6.283185307179586;
    FAngle sourceAngle{};
    sourceAngle.Yaw = runtime::Win64CvttDoubleToInt32(std::atan2(direction.y, direction.x) * 65535.0 / kTargetTwoPi);

    const int targetYaw = target->GetDir().Yaw;
    const int centerYaw = targetYaw + m_nDirection * 0xffff / 0x168;
    const int halfRange = m_nRange * 0xffff / 0x2d0;
    FAngle beginAngle{};
    beginAngle.Yaw = centerYaw - halfRange;
    FAngle endAngle{};
    endAngle.Yaw = centerYaw + halfRange;
    if (!sourceAngle.IsBetween(beginAngle, endAngle)) {
        return false;
    }

    m_pSkillInfo->ActivateSkill(creature, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    creature->BroadcastCubicSkillUse(*target, m_pSkillInfo, 0, 0);
    g_npcserver.Send(
        "cddddddd", 0x0e, static_cast<int>(creature->m_nId), creature->GetIndex(), static_cast<int>(target->m_nId),
        m_pSkillInfo->m_nSkillId, m_pSkillInfo->m_nLevel, m_pSkillInfo->m_nSubLevel, m_pSkillInfo->m_nNpcNotice
    );
    return true;
}

bool CSkillEffect_cub_m_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8d38c.
    L2_GUARD_FUNCTION();

    CCreature* targetCreature = nullptr;
    CStaticObject* targetStaticObject = nullptr;
    double magicalDefense = 0.0;

    if (target->IsCreature()) {
        targetCreature = static_cast<CCreature*>(target);
        {
            L2_GUARD(*targetCreature->m_lock);
            magicalDefense = targetCreature->m_data->m_dParam[VALIDATE::MDEFEND];
        }
        magicalDefense *= creature->m_SkillMod.m_dMagicalPenetrateFactor;
    } else if (target->IsStaticObject()) {
        targetStaticObject = target->CastStaticObject();
        {
            L2_GUARD(*targetStaticObject->m_lock);
            magicalDefense = targetStaticObject->GetMDefend();
        }
    } else {
        return false;
    }

    const double damage = m_dPower * 91.0 / magicalDefense;
    if (targetCreature != nullptr && target->IsCreature()) {
        targetCreature->GotDamageBy(creature, damage, false, DAMAGE_NORMAL, true);
    } else if (targetStaticObject != nullptr && target->IsStaticObject()) {
        targetStaticObject->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_cub_hp_drain::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8d584.
    L2_GUARD_FUNCTION();

    auto* const targetCreature = dynamic_cast<CCreature*>(target);
    const double magicalDefense = [&]() -> double {
        L2_GUARD(*targetCreature->m_lock);
        const double magicalDefense = targetCreature->m_data->m_dParam[VALIDATE::MDEFEND];
        return magicalDefense;
    }();

    const double damage = targetCreature->GotDamageBy(
        creature, m_dPower * 91.0 / (magicalDefense * creature->m_SkillMod.m_dMagicalPenetrateFactor), false, DAMAGE_NORMAL,
        true
    );
    creature->IncreaseHP_diff(damage * m_dRatio / 100.0, true, 1, creature->m_nId, false);
    return true;
}

bool CSkillEffect_cub_heal::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8d6dc.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    if (target->IsAlive()) {
        auto* const targetCreature = dynamic_cast<CCreature*>(target);
        targetCreature->IncreaseHP_diff(m_nHealHP, true, 1, creature->m_nId, false);
        if (target->IsPC()) {
            targetCreature->SendSystemMessage(static_cast<unsigned int>(SystemMessageId::HP_RECOVERED));
        }
    }
    return true;
}

CSkillEffect_i_p_attack_abnormal::CSkillEffect_i_p_attack_abnormal(
    double power, int specialProb, int ignoreDefence, int physicalProb, char16_t* slotType, double additionalBonus
) :
    m_Power(power),
    m_SpecialProb(specialProb),
    m_IgnoreDefence(ignoreDefence),
    m_PhysicalProb(physicalProb),
    m_nAbnormalType(GetAbnormalStatusSlotTypeByName(slotType)),
    m_dAdditionalBonus(additionalBonus) {
    // Current L2Server 0x00ca2e14.
}

bool CSkillEffect_i_p_attack_abnormal::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a93d70.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }

    const double power = targetCreature->HasAbnormalStatusSlot(m_nAbnormalType) ? m_Power + m_dAdditionalBonus : m_Power;
    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, power, 1.0, m_SpecialProb, m_IgnoreDefence, m_PhysicalProb, false, 0
    );
    const double damage = L2SkillFunc::CalculateDamage(parameters, creature->IsPC() || creature->IsSummonOrPet());

    if (target->IsCreature()) {
        const double appliedDamage = target->CastCreature()->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        target->CastCreature()->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

CSkillEffect_i_p_attack_save_hp::CSkillEffect_i_p_attack_save_hp(
    double power, int specialProb, int ignoreDefence, int physicalProb, int saveHP
) :
    m_Power(power),
    m_SpecialProb(specialProb),
    m_IgnoreDefence(ignoreDefence),
    m_PhysicalProb(physicalProb),
    m_SaveHP(static_cast<double>(saveHP)) {
    // Current L2Server 0x00ca2988.
}

bool CSkillEffect_i_p_attack_save_hp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a927e0.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, m_Power, 1.0, m_SpecialProb, m_IgnoreDefence, m_PhysicalProb, false,
        runtime::Win64CvttDoubleToInt32(m_SaveHP)
    );
    const double damage = L2SkillFunc::CalculateDamage(parameters, true);

    if (target->IsCreature()) {
        target->CastCreature()->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
    } else if (target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

CSkillEffect_i_physical_attack_hp_link::CSkillEffect_i_physical_attack_hp_link(
    double power, int specialProb, int ignoreDefence, int physicalProb
) :
    m_dPower(power), m_nSpecialProb(specialProb), m_nIgnoreDefence(ignoreDefence), m_PhysicalProb(physicalProb) {
    // Current L2Server 0x00ca489c.
}

bool CSkillEffect_i_physical_attack_hp_link::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a96ea4.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    double physicalAttack = 0.0;
    double randomDamage = 0.0;
    {
        L2_GUARD(*creature->m_lock);
        physicalAttack = creature->m_data->m_dParam[VALIDATE::PATTACK];
        randomDamage = creature->GetWeaponRandomDamage();
    }

    CCreature* targetCreature = nullptr;
    CStaticObject* targetStaticObject = nullptr;
    double physicalDefence = 1.0;
    double shieldDefence = 0.0;
    bool perfectShieldDefence = false;
    if (target->IsCreature()) {
        targetCreature = target->CastCreature();
        L2_GUARD(*targetCreature->m_lock);
        physicalDefence = targetCreature->GetPDefend();
        if (m_nIgnoreDefence == 2 || m_nIgnoreDefence == 3) {
            physicalDefence -= physicalDefence * static_cast<double>(m_PhysicalProb) / 100.0;
        }
        if (m_nIgnoreDefence == 0 || m_nIgnoreDefence == 3) {
            perfectShieldDefence = L2SkillFunc::ApplyShieldDefense(
                *creature, targetCreature, 1.0, shieldDefence,
                ShouldNotifyPhysicalShieldDefence(skillInfo, skillAction != nullptr && skillAction->m_Infected)
            );
        }
        physicalDefence *= creature->m_SkillMod.m_dArmorPenetrateFactor;
        shieldDefence *= creature->m_SkillMod.m_dShieldPenetrateFactor;
    } else if (target->IsStaticObject()) {
        targetStaticObject = target->CastStaticObject();
        L2_GUARD(*targetStaticObject->m_lock);
        physicalDefence = targetStaticObject->GetPDefend();
    }

    double weaponTraitBonus = 1.0;
    double skillAttributeBonus = 1.0;
    if (targetCreature != nullptr) {
        weaponTraitBonus = targetCreature->GetWeaponTraitBonus(creature->m_data->m_AttackType);
        skillAttributeBonus = creature->GetSkillAttributeBonus(targetCreature, skillInfo, true);
    }

    double soulShotBonus = creature->GetBonusAndTurnNextSoulShotState() + 1.0;
    if (soulShotBonus > 1.0) {
        soulShotBonus *= creature->GetSoulShotExtraBonus() + 1.0;
    }

    double damage = 1.0;
    if (!perfectShieldDefence) {
        const AttackType attackType = creature->GetAttackType();
        const bool bowAttack = attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW;
        const double additionalBowPower = bowAttack ? soulShotBonus * physicalAttack : 0.0;

        double positionBonusRate = 0.0;
        double criticalDamagePositionBonusPer = 1.0;
        double criticalDamagePositionBonusDiff = 0.0;
        CAttackAction::SetPositionBonus(
            creature, target, positionBonusRate, criticalDamagePositionBonusPer, criticalDamagePositionBonusDiff
        );
        const double positionDamage = (soulShotBonus * physicalAttack + m_dPower) * randomDamage * positionBonusRate;

        const double rawHpLink =
            (100.0 - (creature->m_data->m_dHP / creature->m_data->m_dBaseHP * 100.0 - 100.0) * 2.0) / 100.0;
        double hpLink = rawHpLink;
        if (hpLink < 0.0) {
            hpLink = 0.0;
        } else if (hpLink > 2.5) {
            hpLink = 2.5;
        }

        const double damageConstant = bowAttack ? 70.0 : 77.0;
        damage = (creature->LevelBonus() * physicalAttack + m_dPower + additionalBowPower + positionDamage) * soulShotBonus *
            randomDamage * damageConstant / (shieldDefence + physicalDefence) * weaponTraitBonus * skillAttributeBonus *
            hpLink;

        if (target->IsPCOrSummon()) {
            damage *= creature->m_SkillMod.m_dPvpPhysicalSkillBonusPer;
            User* const user = target->GetUserOrMaster();
            if (user != nullptr) {
                damage *= user->m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer;
            }
        } else if (targetCreature != nullptr && targetCreature->IsBoss()) {
            damage *= creature->m_SkillMod.m_dPvBossPhysicalSkillBonusPer;
        } else {
            damage *= creature->m_SkillMod.m_dPvePhysicalSkillBonusPer;
        }

        damage = L2SkillFunc::CalcCriticalPhysicalAttack(*creature, skillAction, damage, m_nSpecialProb);
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
    }

    if (targetCreature != nullptr && target->IsCreature()) {
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        targetCreature->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (targetStaticObject != nullptr && target->IsStaticObject()) {
        targetStaticObject->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_i_p_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a859c4.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, m_Power, 1.0, m_SpecialProb, m_IgnoreDefence, m_PhysicalProb, false, 0
    );
    const double damage = L2SkillFunc::CalculateDamage(parameters, creature->IsPC() || creature->IsSummonOrPet());
    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        target->CastCreature()->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_i_p_attack_by_dist::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
) {
    // Current L2Server 0x00a919ec.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, m_Power, 1.0, m_SpecialProb, m_IgnoreDefence, m_PhysicalProb, false, 0
    );
    double damage = L2SkillFunc::CalculateDamage(parameters, true);
    const double rawRatio = std::sqrt(distance) / (static_cast<double>(skillInfo->m_nAffectRange) + 0.1);
    const double cappedRatio = rawRatio > 0.7 ? 0.7 : rawRatio;
    damage *= 1.0 - cappedRatio;

    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        target->CastCreature()->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (skillInfo->m_TargetType == STGT_SHIP_CANNON && target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_i_p_percent_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a936c4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, 0.0, (m_dPowerPercent + 100.0) / 100.0, m_SpecialProb, m_IgnoreDefence,
        m_PhysicalProb, false, 0
    );
    const double damage = L2SkillFunc::CalculateDamage(parameters, false);
    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        target->CastCreature()->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

CSkillEffect_i_p_attack_weapon_bonus::CSkillEffect_i_p_attack_weapon_bonus(
    double power, int specialProb, int ignoreDefence, int physicalProb
) :
    m_Power(power),
    m_SpecialProb(specialProb),
    m_IgnoreDefence(ignoreDefence),
    m_PhysicalProb(physicalProb) {
    // Current L2Server 0x00ca62fc.
}

double CSkillEffect_i_p_attack_weapon_bonus::CalcPower(CCreature* creature) {
    // Current L2Server 0x00a9d320.
    L2_GUARD_FUNCTION();
    double power = m_Power;
    for (const WeaponBonusType& weaponBonus : m_vWeaponBonusTypeList) {
        if (weaponBonus.m_ChangeType == SFCT_PER) {
            for (int index = 0; index < AT_MAX; ++index) {
                if (weaponBonus.m_arrAttackType[creature->GetAttackType()]) {
                    return ((static_cast<double>(weaponBonus.m_nBonus) + 100.0) / 100.0) * m_Power;
                }
            }
        } else if (weaponBonus.m_ChangeType == SFCT_DIFF) {
            for (int index = 0; index < AT_MAX; ++index) {
                if (weaponBonus.m_arrAttackType[creature->GetAttackType()]) {
                    return static_cast<double>(weaponBonus.m_nBonus) + power;
                }
            }
        }
    }
    return power;
}

bool CSkillEffect_i_p_attack_weapon_bonus::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a9d484.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, CalcPower(creature), 1.0, m_SpecialProb, m_IgnoreDefence, m_PhysicalProb,
        false, 0
    );
    const double damage = L2SkillFunc::CalculateDamage(parameters, true);
    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        target->CastCreature()->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_i_energy_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a95890.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    const double physicalAttack = user->m_data->m_dParam[VALIDATE::PATTACK];

    CCreature* targetCreature = nullptr;
    CStaticObject* targetStaticObject = nullptr;
    double physicalDefence = 1.0;
    double shieldDefence = 0.0;
    bool perfectShieldDefence = false;

    if (target->IsCreature()) {
        targetCreature = static_cast<CCreature*>(target);
        L2_GUARD(*targetCreature->m_lock);
        physicalDefence = targetCreature->GetPDefend();
        if (m_nIgnoreDefence == 2 || m_nIgnoreDefence == 3) {
            physicalDefence -= physicalDefence * static_cast<double>(m_PhysicalProb) / 100.0;
        }
        if (m_nIgnoreDefence == 0 || m_nIgnoreDefence == 3) {
            bool notifyShieldDefence = true;
            if (skillInfo != nullptr) {
                const SkillOperateType operateType = skillInfo->m_OperateType;
                const bool infectionRequired = operateType == SKILL_DURATION || operateType == SKILL_DURATION_LEVEL_UP ||
                    operateType == SKILL_SELFDURATION || operateType == SKILL_CHARGE_DURATION ||
                    operateType == SKILL_AURA_ACTIVITY || operateType == SKILL_BACK_STEP_DURATION ||
                    operateType == SKILL_AURA_ACTIVITY2 || operateType == SKILL_PARTY_INSTANT ||
                    operateType == SKILL_DUAL_PUMP1 || operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
                if (infectionRequired && skillAction != nullptr && skillAction->m_Infected) {
                    notifyShieldDefence = false;
                }
            }
            perfectShieldDefence =
                L2SkillFunc::ApplyShieldDefense(*creature, targetCreature, 1.0, shieldDefence, notifyShieldDefence);
        }
    } else if (target->IsStaticObject()) {
        targetStaticObject = target->CastStaticObject();
        L2_GUARD(*targetStaticObject->m_lock);
        physicalDefence = targetStaticObject->GetPDefend();
    }

    if (targetCreature != nullptr) {
        shieldDefence *= creature->m_SkillMod.m_dShieldPenetrateFactor;
        physicalDefence *= creature->m_SkillMod.m_dArmorPenetrateFactor;
    }

    double traitAndAttributeBonus = 1.0;
    if (targetCreature != nullptr) {
        traitAndAttributeBonus = targetCreature->GetWeaponTraitBonus(user->m_data->m_AttackType);
        traitAndAttributeBonus *= user->GetSkillAttributeBonus(targetCreature, skillInfo, true);
    }

    double soulShotBonus = user->GetBonusAndTurnNextSoulShotState() + 1.0;
    if (soulShotBonus > 1.0) {
        soulShotBonus *= user->GetSoulShotExtraBonus() + 1.0;
    }

    double damage = 1.0;
    if (!perfectShieldDefence) {
        const AttackType attackType = static_cast<CCreature*>(user)->GetAttackType();
        const double damageConstant =
            attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW ? 70.0 : 77.0;
        const double energyBonus = static_cast<double>(user->m_UsedEnergyCount - skillInfo->m_nEnergy) * 0.1 + 1.3;
        damage = (user->LevelBonus() * physicalAttack + m_dPower) * soulShotBonus * energyBonus * damageConstant /
            (shieldDefence + physicalDefence) * traitAndAttributeBonus;

        if (target->IsPCOrSummon()) {
            damage *= user->m_SkillMod.m_dPvpPhysicalSkillBonusPer;
            User* const targetUser = target->GetUserOrMaster();
            if (targetUser != nullptr) {
                damage *= targetUser->m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer;
            }
        } else if (targetCreature != nullptr && targetCreature->IsBoss()) {
            damage *= creature->m_SkillMod.m_dPvBossPhysicalSkillBonusPer;
        } else {
            damage *= creature->m_SkillMod.m_dPvePhysicalSkillBonusPer;
        }

        const CCreatureSkillMod::Mod& energyMod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::EnergyAttack];
        damage = damage * energyMod.m_Per + energyMod.m_Diff;
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        damage = L2SkillFunc::CalcCriticalPhysicalAttack(*creature, skillAction, damage, m_nSpecialProb);
    }

    if (targetCreature != nullptr) {
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        targetCreature->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (targetStaticObject != nullptr) {
        targetStaticObject->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_i_add_event_contribute::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9a59c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature != nullptr) {
        int minimumCount = m_nMinCount;
        int maximumCount = m_nMaxCount;
        if (maximumCount < minimumCount) {
            std::swap(minimumCount, maximumCount);
        }

        const int contributionRange = runtime::Int32FromBits(
            static_cast<std::uint32_t>(maximumCount) - static_cast<std::uint32_t>(minimumCount) + 1U
        );
        const int contribution = runtime::Win64CvttDoubleToInt32(
            genrand64_real2() * static_cast<double>(contributionRange) + static_cast<double>(minimumCount)
        );
        targetCreature->m_pContribute->AccumulateEventContribute(creature, contribution);

        if (creature->GetSocket() != nullptr) {
            // Current MSVC evaluates the packet coordinates right-to-left
            // and therefore observes six distinct position snapshots.
            const int secondZ = runtime::Win64CvttDoubleToInt32(creature->GetPos().z);
            const int secondY = runtime::Win64CvttDoubleToInt32(creature->GetPos().y);
            const int secondX = runtime::Win64CvttDoubleToInt32(creature->GetPos().x);
            const int firstZ = runtime::Win64CvttDoubleToInt32(creature->GetPos().z);
            const int firstY = runtime::Win64CvttDoubleToInt32(creature->GetPos().y);
            const int firstX = runtime::Win64CvttDoubleToInt32(creature->GetPos().x);
            const int sourceId = static_cast<int>(creature->m_nId);
            const int encodedContribution = static_cast<int>(static_cast<unsigned int>(contribution) & 0xffffU);
            creature->GetSocket()->Send(
                "cdddddddddddhhddddd", 0x48, 0, sourceId, sourceId, 0x4828, encodedContribution, 500, 0, 1500, firstX,
                firstY, firstZ, 0, 2, secondX, secondY, secondZ, 0, 0
            );
            creature->GetSocket()->Send("cdddddd", 0x54, 0, sourceId, 0x4828, encodedContribution, 1, sourceId);
            creature->GetSocket()->SendSystemMessage_d(SystemMessageId::CURE_RECOVERY_AMOUNT, contribution);
        }

        CLogFormatter formatter;
        formatter.WriteLog1_1(0x152, creature, targetCreature, nullptr, nullptr, nullptr, contribution, 0, 0, 0, 0, 0, 0);
    }
    return true;
}

CSkillEffect_i_restoration::CSkillEffect_i_restoration(std::u16string name, int count, int enchant) :
    xwsItemName(name), nCount(count), nEnchant(enchant) {
    // Current L2Server 0x00ca43b8 copies the by-value string.
}

bool CSkillEffect_i_restoration::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa722c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    const int rewardItemClassId = g_ObjectDB.GetClassIdFromName(xwsItemName.c_str(), false);
    if (rewardItemClassId == -1) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    std::array<ItemManip, 2> requests{};
    int requestCount = 1;

    if (skillInfo->m_nCapsuleReduceItemID == -1) {
        if (skillInfo->m_nConsumeItem > 0 && skillInfo->m_nConsumeItemCount > 0) {
            CObject* const consumeObject = g_ObjectDB.GetObjectW(skillInfo->m_nConsumeItem, false);
            if (consumeObject != nullptr &&
                static_cast<CItem*>(consumeObject)->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS &&
                !user->m_inv.HaveItemByClassId(skillInfo->m_nConsumeItem, skillInfo->m_nConsumeItemCount, false, false)) {
                user->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
        }
        if (user->m_lWaitItemJob == 1) {
            user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
            return false;
        }
        requests[0].mClassId = rewardItemClassId;
        requests[0].mCount = nCount;
        requests[0].mEnchanted = nEnchant;
    } else {
        requestCount = 2;
        requests[0].mClassId = skillInfo->m_nCapsuleReduceItemID;
        requests[0].mCount = -1;
        requests[1].mClassId = rewardItemClassId;
        requests[1].mCount = nCount;
        requests[1].mEnchanted = nEnchant;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_SKILL_EFFECT_INSTANT, 1, requestCount, requests.data(), nullptr, 0, packet, false,
        ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        return user->_HandleItemManipulation(queue);
    }
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     insertLogLine)
        // Noise: MRT_JOBNOTMAP means the request was queued behind an active item operation. Normal waiting is not an error.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     insertLogLine)
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"SkillFxFunc_instant.cpp",
    //     errorLogLine)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"SkillFxFunc_instant.cpp",
    //     errorLogLine)
    L2_LOG_ERROR(
        Log, "i_restoration could not schedule item changes: unexpected queue result %d; the effect fails.",
        static_cast<int>(result)
    );
    return false;
}

bool CSkillEffect_i_restoration_random::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa7e38.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    std::array<ItemManip, 100> requests{};
    int requestCount = 1;
    int consumedItemClassId = 0;
    int consumedItemCount = 0;

    if (skillInfo->m_nCapsuleReduceItemID == -1) {
        requestCount = 0;
        if (skillInfo->m_nConsumeItem > 0 && skillInfo->m_nConsumeItemCount > 0) {
            CObject* const consumeObject = g_ObjectDB.GetObjectW(skillInfo->m_nConsumeItem, false);
            if (consumeObject != nullptr &&
                static_cast<CItem*>(consumeObject)->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS &&
                !user->m_inv.HaveItemByClassId(skillInfo->m_nConsumeItem, skillInfo->m_nConsumeItemCount, false, false)) {
                user->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
            consumedItemClassId = skillInfo->m_nConsumeItem;
            consumedItemCount = runtime::NegateInt32Wrapping(static_cast<int>(skillInfo->m_nConsumeItemCount));
        }
    } else {
        consumedItemClassId = skillInfo->m_nCapsuleReduceItemID;
        consumedItemCount = -1;
        requests[0].mClassId = consumedItemClassId;
        requests[0].mCount = -1;
    }

    int maximumRestorationCount = 0;
    for (const CRestorationUnitList* const list : *m_vRestorationList) {
        const int count = static_cast<int>(list->unitList->size());
        if (maximumRestorationCount < count) {
            maximumRestorationCount = count;
        }
    }
    if (user->GetInventoryLimit() < user->m_inv.GetTotalCount() + maximumRestorationCount) {
        user->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        return false;
    }

    std::u16string restoredItemIds;
    std::u16string restoredItemCounts;
    bool restored = false;
    double accumulatedProbability = 0.0;
    const double selectedProbability = ::genrand64_real1() * 100.0 + 0.0;

    for (const CRestorationUnitList* const list : *m_vRestorationList) {
        accumulatedProbability += list->prob;
        if (accumulatedProbability <= selectedProbability) {
            continue;
        }

        for (const CRestorationUnit* const unit : *list->unitList) {
            const int itemClassId = g_ObjectDB.GetClassIdFromName(unit->xwsItemName.c_str(), false);
            ItemManip& request = requests[requestCount];
            request.mClassId = itemClassId;
            request.mCount = unit->count;
            request.mEnchanted = unit->nEnchant;

            restoredItemIds += IntToXStr(itemClassId);
            restoredItemIds += u" ";
            restoredItemCounts += IntToXStr(static_cast<int>(unit->count));
            restoredItemCounts += u" ";
            ++requestCount;
        }
        restored = true;
        break;
    }

    if (!nConsumeItemName.empty()) {
        const int itemClassId = g_ObjectDB.GetClassIdFromName(nConsumeItemName.c_str(), false);
        if (itemClassId != -1) {
            ItemManip& request = requests[requestCount];
            request.mClassId = itemClassId;
            request.mCount = runtime::NegateInt32Wrapping(nConsumeItemCount);
            ++requestCount;
        }
    }

    if (restored) {
        const std::size_t itemIdsLength = restoredItemIds.size();
        const std::size_t itemCountsLength = restoredItemCounts.size();
        if (itemIdsLength > 0) {
            restoredItemIds.assign(runtime::StringView(restoredItemIds).substr(0, itemIdsLength - 1));
            restoredItemCounts.assign(runtime::StringView(restoredItemCounts).substr(0, itemCountsLength - 1));
        }
        if (itemIdsLength > 200) {
            restoredItemIds.assign(runtime::StringView(restoredItemIds).substr(0, 200));
        }
        if (itemCountsLength > 50) {
            restoredItemCounts.assign(runtime::StringView(restoredItemCounts).substr(0, 50));
        }
    }

    if (user->m_lWaitItemJob == 1) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user->SaveEtcMsg(
        restored, false, skillInfo->m_nSkillId, consumedItemClassId, consumedItemCount, restoredItemIds, restoredItemCounts,
        0
    );
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_SKILL_EFFECT_INSTANT, 2, requestCount, requests.data(), nullptr, 0, packet, false,
        ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2037)
        // Noise: MRT_JOBNOTMAP means the request was queued behind an active item operation. Normal waiting is not an error.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2037)
        return true;
    }
    if (result != User::MRT_NOJOB) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"SkillFxFunc_instant.cpp",
        //     0x203c)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"SkillFxFunc_instant.cpp",
        //     0x203c)
        L2_LOG_ERROR(
            Log, "i_restoration_random could not schedule item changes: unexpected queue result %d; the effect fails.",
            static_cast<int>(result)
        );
        return false;
    }

    const bool handled = user->_HandleItemManipulation(queue);
    if (restored && handled) {
        std::wstring itemIds = target_utf16::ToHostCodeUnits(restoredItemIds.c_str());
        std::wstring itemCounts = target_utf16::ToHostCodeUnits(restoredItemCounts.c_str());
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x3c9, user, nullptr, WideToUtf16(itemIds.data()), WideToUtf16(itemCounts.data()), nullptr,
            skillInfo->m_nSkillId, 0, 0, consumedItemClassId, consumedItemCount, 0, 0
        );
    } else {
        user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
    }
    return true;
}

bool CSkillEffect_i_restoration_AddRandom::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa8ab0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    std::array<ItemManip, 100> requests{};
    int requestCount = 1;
    int consumedItemClassId = 0;
    int consumedItemCount = 0;
    if (skillInfo->m_nCapsuleReduceItemID == -1) {
        requestCount = 0;
        if (skillInfo->m_nConsumeItem > 0 && skillInfo->m_nConsumeItemCount > 0) {
            CObject* const consumeObject = g_ObjectDB.GetObjectW(skillInfo->m_nConsumeItem, false);
            if (consumeObject != nullptr &&
                static_cast<CItem*>(consumeObject)->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS &&
                !user->m_inv.HaveItemByClassId(skillInfo->m_nConsumeItem, skillInfo->m_nConsumeItemCount, false, false)) {
                user->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
            consumedItemClassId = skillInfo->m_nConsumeItem;
            consumedItemCount = runtime::NegateInt32Wrapping(static_cast<int>(skillInfo->m_nConsumeItemCount));
        }
    } else {
        consumedItemClassId = skillInfo->m_nCapsuleReduceItemID;
        consumedItemCount = -1;
        requests[0].mClassId = consumedItemClassId;
        requests[0].mCount = -1;
    }

    // Current consumes this roll before the inventory-capacity early return.
    const double selectedProbability = genrand64_real1() * 100.0 + 0.0;
    int maximumRandomCount = 0;
    for (const CRestorationUnitList* const list : *m_vRestorationList) {
        const int count = static_cast<int>(list->unitList->size());
        if (maximumRandomCount < count) {
            maximumRandomCount = count;
        }
    }
    const int fixedRewardCount = static_cast<int>(m_vRewardItemList->size());
    const int requiredInventoryCount = runtime::Int32FromBits(
        static_cast<std::uint32_t>(user->m_inv.GetTotalCount()) + static_cast<std::uint32_t>(maximumRandomCount) +
        static_cast<std::uint32_t>(fixedRewardCount)
    );
    if (user->GetInventoryLimit() < requiredInventoryCount) {
        user->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        return false;
    }

    std::u16string restoredItemIds;
    std::u16string restoredItemCounts;
    bool restored = false;
    double accumulatedProbability = 0.0;
    for (const CRestorationUnitList* const list : *m_vRestorationList) {
        accumulatedProbability += list->prob;
        if (accumulatedProbability <= selectedProbability) {
            continue;
        }

        for (const CRestorationUnit* const unit : *list->unitList) {
            const int itemClassId = g_ObjectDB.GetClassIdFromName(unit->xwsItemName.c_str(), false);
            ItemManip& request = requests[requestCount];
            request.mClassId = itemClassId;
            request.mCount = unit->count;
            request.mEnchanted = unit->nEnchant;
            restoredItemIds += IntToXStr(itemClassId);
            restoredItemIds += u" ";
            restoredItemCounts += IntToXStr(static_cast<int>(unit->count));
            restoredItemCounts += u" ";
            ++requestCount;
        }
        restored = true;
        break;
    }

    for (const CRestorationUnit* const unit : *m_vRewardItemList) {
        const int itemClassId = g_ObjectDB.GetClassIdFromName(unit->xwsItemName.c_str(), false);
        ItemManip& request = requests[requestCount];
        request.mClassId = itemClassId;
        request.mCount = unit->count;
        request.mEnchanted = unit->nEnchant;
        restoredItemIds += IntToXStr(itemClassId);
        restoredItemIds += u" ";
        restoredItemCounts += IntToXStr(static_cast<int>(unit->count));
        restoredItemCounts += u" ";
        ++requestCount;
        restored = true;
    }

    if (restored) {
        const std::size_t itemIdsLength = restoredItemIds.size();
        const std::size_t itemCountsLength = restoredItemCounts.size();
        if (itemIdsLength > 0) {
            restoredItemIds.assign(runtime::StringView(restoredItemIds).substr(0, itemIdsLength - 1));
            restoredItemCounts.assign(runtime::StringView(restoredItemCounts).substr(0, itemCountsLength - 1));
        }
        if (itemIdsLength > 200) {
            restoredItemIds.assign(runtime::StringView(restoredItemIds).substr(0, 200));
        }
        if (itemCountsLength > 50) {
            restoredItemCounts.assign(runtime::StringView(restoredItemCounts).substr(0, 50));
        }
    }

    if (user->m_lWaitItemJob == 1) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user->SaveEtcMsg(
        restored, false, skillInfo->m_nSkillId, consumedItemClassId, consumedItemCount, restoredItemIds, restoredItemCounts,
        0
    );
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_SKILL_EFFECT_INSTANT, 2, requestCount, requests.data(), nullptr, 0, packet, false,
        ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2122)
        // Noise: MRT_JOBNOTMAP means the request was queued behind an active item operation. Normal waiting is not an error.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2122)
        return true;
    }
    if (result != User::MRT_NOJOB) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2127)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2127)
        L2_LOG_ERROR(
            Log, "i_restoration_AddRandom could not schedule item changes: unexpected queue result %d; the effect fails.",
            static_cast<int>(result)
        );
        return false;
    }

    const bool handled = user->_HandleItemManipulation(queue);
    if (restored && handled) {
        std::wstring itemIds = target_utf16::ToHostCodeUnits(restoredItemIds.c_str());
        std::wstring itemCounts = target_utf16::ToHostCodeUnits(restoredItemCounts.c_str());
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x3c9, user, nullptr, WideToUtf16(itemIds.data()), WideToUtf16(itemCounts.data()), nullptr,
            skillInfo->m_nSkillId, 0, 0, consumedItemClassId, consumedItemCount, 0, 0
        );
    } else {
        user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
    }
    return true;
}

bool CSkillEffect_i_restoration_random_group::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa9ac0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    std::array<ItemManip, 100> requests{};
    int requestCount = 1;
    int consumedItemClassId = 0;
    int consumedItemCount = 0;
    if (skillInfo->m_nCapsuleReduceItemID == -1) {
        requestCount = 0;
        if (skillInfo->m_nConsumeItem > 0 && skillInfo->m_nConsumeItemCount > 0) {
            CObject* const consumeObject = g_ObjectDB.GetObjectW(skillInfo->m_nConsumeItem, false);
            if (consumeObject != nullptr &&
                static_cast<CItem*>(consumeObject)->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS &&
                !user->m_inv.HaveItemByClassId(skillInfo->m_nConsumeItem, skillInfo->m_nConsumeItemCount, false, false)) {
                user->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
            consumedItemClassId = skillInfo->m_nConsumeItem;
            consumedItemCount = runtime::NegateInt32Wrapping(static_cast<int>(skillInfo->m_nConsumeItemCount));
        }
    } else {
        consumedItemClassId = skillInfo->m_nCapsuleReduceItemID;
        consumedItemCount = -1;
        requests[0].mClassId = consumedItemClassId;
        requests[0].mCount = -1;
    }

    int maximumRandomCount = 0;
    for (const CRestorationUnitGroupList* const group : *m_vRestorationGroupList) {
        // Current advances this loop against the selected list's unit count,
        // not against groupList.size(). Preserve that shipped boundary.
        int listIndex = 0;
        if (!group->groupList->empty()) {
            do {
                const CRestorationUnitList* const list =
                    runtime::CheckedVectorAt((*group->groupList), static_cast<std::size_t>(listIndex));
                const int count = static_cast<int>(list->unitList->size());
                if (maximumRandomCount < count) {
                    maximumRandomCount = count;
                }
                ++listIndex;
            } while (listIndex <
                static_cast<int>(
                    runtime::CheckedVectorAt((*group->groupList), static_cast<std::size_t>(listIndex - 1))->unitList->size()
                ));
        }
        const int requiredInventoryCount = runtime::Int32FromBits(
            static_cast<std::uint32_t>(user->m_inv.GetTotalCount()) + static_cast<std::uint32_t>(maximumRandomCount)
        );
        if (user->GetInventoryLimit() < requiredInventoryCount) {
            user->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
            return false;
        }
    }

    std::u16string restoredItemIds;
    std::u16string restoredItemCounts;
    bool restored = false;
    double accumulatedProbability = 0.0;
    const double selectedGroupProbability = genrand64_real1() * 100.0 + 0.0;
    for (const CRestorationUnitGroupList* const group : *m_vRestorationGroupList) {
        if (selectedGroupProbability >= group->prob) {
            continue;
        }

        const double selectedListProbability = genrand64_real1() * 100.0 + 0.0;
        for (const CRestorationUnitList* const list : *group->groupList) {
            accumulatedProbability += list->prob;
            if (accumulatedProbability <= selectedListProbability) {
                continue;
            }

            for (const CRestorationUnit* const unit : *list->unitList) {
                const int itemClassId = g_ObjectDB.GetClassIdFromName(unit->xwsItemName.c_str(), false);
                ItemManip& request = requests[requestCount];
                request.mClassId = itemClassId;
                request.mCount = unit->count;
                request.mEnchanted = unit->nEnchant;
                restoredItemIds += IntToXStr(itemClassId);
                restoredItemIds += u" ";
                restoredItemCounts += IntToXStr(static_cast<int>(unit->count));
                restoredItemCounts += u" ";
                ++requestCount;
            }
            restored = true;
            break;
        }
    }

    if (restored) {
        const std::size_t itemIdsLength = restoredItemIds.size();
        const std::size_t itemCountsLength = restoredItemCounts.size();
        if (itemIdsLength > 0) {
            restoredItemIds.assign(runtime::StringView(restoredItemIds).substr(0, itemIdsLength - 1));
            restoredItemCounts.assign(runtime::StringView(restoredItemCounts).substr(0, itemCountsLength - 1));
        }
        if (itemIdsLength > 200) {
            restoredItemIds.assign(runtime::StringView(restoredItemIds).substr(0, 200));
        }
        if (itemCountsLength > 50) {
            restoredItemCounts.assign(runtime::StringView(restoredItemCounts).substr(0, 50));
        }
    }

    if (user->m_lWaitItemJob == 1) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user->SaveEtcMsg(
        restored, false, skillInfo->m_nSkillId, consumedItemClassId, consumedItemCount, restoredItemIds, restoredItemCounts,
        0
    );
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_SKILL_EFFECT_INSTANT, 4, requestCount, requests.data(), nullptr, 0, packet, false,
        ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2210)
        // Noise: MRT_JOBNOTMAP means the request was queued behind an active item operation. Normal waiting is not an error.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2210)
        return true;
    }
    if (result != User::MRT_NOJOB) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2215)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2215)
        L2_LOG_ERROR(
            Log, "i_restoration_random_group could not schedule item changes: unexpected queue result %d; the effect fails.",
            static_cast<int>(result)
        );
        return false;
    }

    const bool handled = user->_HandleItemManipulation(queue);
    if (restored && handled) {
        std::wstring itemIds = target_utf16::ToHostCodeUnits(restoredItemIds.c_str());
        std::wstring itemCounts = target_utf16::ToHostCodeUnits(restoredItemCounts.c_str());
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x3c9, user, nullptr, WideToUtf16(itemIds.data()), WideToUtf16(itemCounts.data()), nullptr,
            skillInfo->m_nSkillId, 0, 0, consumedItemClassId, consumedItemCount, 0, 0
        );
    } else {
        user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
    }
    return true;
}

bool CSkillEffect_i_event_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a85bbc.
    L2_GUARD_FUNCTION();
    CNPC* const npc = target->CastNPC();
    if (creature->CastUser() != nullptr && npc != nullptr) {
        const double appliedDamage = npc->GotDamageBy(creature, m_dDamage, false, DAMAGE_EVENT, true);
        npc->ApplyGotDamageBySkill(creature, m_dDamage, appliedDamage);
    }
    return true;
}

bool CSkillEffect_i_p_soul_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a85cd0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    const L2SkillFunc::CalculateDamageParam parameters(
        creature, target, skillAction, skillInfo, m_Power, 1.0, m_SpecialProb, m_IgnoreDefence, m_PhysicalProb, true, 0
    );
    const double damage = L2SkillFunc::CalculateDamage(parameters, true);
    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        target->CastCreature()->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    } else if (target->IsStaticObject()) {
        target->CastStaticObject()->GotDamageBy(creature, damage);
    }
    return true;
}

bool CSkillEffect_i_heal::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a86068.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageHP(creature, targetCreature)) {
        return false;
    }

    const double amount = L2SkillFunc::CalcHealingAmount(creature, skillInfo, targetCreature, m_dPower);
    const double appliedAmount = targetCreature->IncreaseHP_diff(amount, true, 1, creature->m_nId, false);
    L2SkillFunc::SendHpMpChangedSystemMessage(
        creature, *targetCreature, static_cast<int>(appliedAmount), SystemMessageId::HP_RESTORED
    );
    return true;
}

CSkillEffect_i_heal_special::CSkillEffect_i_heal_special(double power) : m_dPower(power), m_heal_effect(power) {
    // Current L2Server 0x00ca3940.
}

bool CSkillEffect_i_heal_special::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a86198.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(target);
    if (!targetCreature->m_SkillMod.CanGetDamageHP(creature, targetCreature)) {
        return false;
    }

    const double amount = L2SkillFunc::CalcHealingAmount(creature, skillInfo, targetCreature, m_dPower);
    const double appliedAmount = targetCreature->IncreaseHP_diff(amount, true, 1, creature->m_nId, false);
    const int applied = runtime::Win64CvttDoubleToInt32(appliedAmount);
    L2SkillFunc::SendHpMpChangedSystemMessage(creature, *targetCreature, applied, SystemMessageId::HP_RESTORED);

    if (runtime::Win64CvttDoubleToInt32(amount) != applied && L2Common::IsAlivePC(target)) {
        User* const targetUser = target->CastUser();
        if (targetUser->m_SkillMod.CanGetDamageHP(creature, targetUser)) {
            const double appliedCp = targetUser->IncreaseCP_diff(amount - applied, 4, 0, false);
            L2SkillFunc::SendHpMpChangedSystemMessage(
                creature, *targetUser, runtime::Win64CvttDoubleToInt32(appliedCp), SystemMessageId::CP_RESTORED
            );
        }
    }
    return true;
}

double CSkillEffect_i_heal_link::CalcIHeal(CCreature* creature, const CSkillInfo* skillInfo, CSPointer<CCreature> target) {
    // Current L2Server 0x00a9aa38.
    L2_GUARD_FUNCTION();
    if (target && target->IsNPC()) {
        if (target->CastNPC()->m_nClassId == 0xf772f || target->CastNPC()->m_nClassId == 0xf7730) {
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

    const double magicLevel = static_cast<double>(skillInfo->m_nMagicLevel);
    const double threshold = magicLevel * 0.007 * magicLevel * magicLevel -
        magicLevel * 0.00004 * magicLevel * magicLevel * magicLevel - magicLevel * 0.1923 * magicLevel +
        magicLevel * 3.6064 - 1.0966;
    const double baseLevelBonus = magicLevel * 0.0116 * magicLevel + magicLevel * 0.6371 + 6.4512;
    const double attackPenalty = std::max(threshold - magicAttack, 0.0);
    const double adjustedLevelBonus = std::max(baseLevelBonus - attackPenalty / 5.0, 0.0);

    double amount = (std::sqrt(magicAttack + spiritShotAttackBonus) + m_Power +
                        creature->m_SkillMod.m_Mods[CCreatureSkillMod::HealBonus].m_Diff +
                        adjustedLevelBonus * creature->m_dSpiritShotHealBonus) *
        target->m_dHPRegenModByWeight;
    if (genrand64_real1() * 100.0 <= 5.0) {
        amount *= 3.0;
    }
    return amount;
}

bool CSkillEffect_i_heal_link::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a9d65c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    const bool persistentEffect = skillInfo->m_OperateType == SKILL_DURATION ||
        skillInfo->m_OperateType == SKILL_CHARGE_DURATION || skillInfo->m_nAbnormalTime > 0;

    const auto finishHeal = [creature, skillInfo, persistentEffect](CCreature& healTarget, const double amount) {
        const double appliedAmount = healTarget.IncreaseHP_diff(amount, true, 1, creature->m_nId, false);
        L2SkillFunc::SendHpMpChangedSystemMessage(
            creature, healTarget, runtime::Win64CvttDoubleToInt32(appliedAmount), SystemMessageId::HP_RESTORED
        );
        g_npcserver.Send(
            "cddddddd", 0x0e, static_cast<int>(creature->m_nId), creature->GetIndex(), static_cast<int>(healTarget.m_nId),
            skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, 0
        );

        if (healTarget.IsMakeAttackerGuilty(creature, persistentEffect)) {
            if (!persistentEffect && healTarget.GetGuilty() == 0) {
                creature->m_bIsSkillViolet = true;
            }
            CIOObject::SetTimeStamp(&creature->m_data->m_nPKTimeStamp);
        }
    };

    if (m_Sfct == SFCT_PER) {
        double percent = m_Power;
        for (std::size_t index = 0; index < creature->m_vSkillTarget.c.size(); ++index) {
            CSPointer<CCreature> healTarget = runtime::CheckedVectorAt(creature->m_vSkillTarget.c, index).FindObjectSP();
            if (index != 0) {
                percent -= static_cast<double>(m_DecreaseAmt);
            }
            percent = std::max(percent, 10.0);
            if (!healTarget || !healTarget->m_SkillMod.CanGetDamageHP(creature, healTarget.get())) {
                continue;
            }
            if (healTarget->IsNPC()) {
                if (healTarget->CastNPC()->m_nClassId == 0xf772f || healTarget->CastNPC()->m_nClassId == 0xf7730) {
                    return false;
                }
            }
            const double amount = percent * healTarget->m_data->m_dOrgHP / 100.0 * healTarget->m_dHPRegenModByWeight;
            finishHeal(*healTarget.get(), amount);
        }
    } else if (m_Sfct == SFCT_DIFF) {
        double percent = 100.0;
        for (std::size_t index = 0; index < creature->m_vSkillTarget.c.size(); ++index) {
            CSPointer<CCreature> healTarget = runtime::CheckedVectorAt(creature->m_vSkillTarget.c, index).FindObjectSP();
            const double baseAmount = CalcIHeal(creature, skillInfo, healTarget);
            if (index != 0) {
                percent -= static_cast<double>(m_DecreaseAmt);
            }
            percent = std::max(percent, 10.0);
            if (!healTarget || !healTarget->m_SkillMod.CanGetDamageHP(creature, healTarget.get())) {
                continue;
            }
            if (healTarget->IsNPC()) {
                if (healTarget->CastNPC()->m_nClassId == 0xf772f || healTarget->CastNPC()->m_nClassId == 0xf7730) {
                    return true;
                }
            }
            finishHeal(*healTarget.get(), baseAmount * percent / 100.0);
        }
    }
    return true;
}

bool CSkillEffect_i_hp_drain::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a86368.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
    const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
    const double baseDamage =
        (std::sqrt(attackerInfo.m_MagicPower) * m_dPower * 91.0 / targetInfo.m_MDefend) * targetInfo.m_AttrResist;
    const double damage = L2SkillFunc::CalcFinalDamage(
        *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
    );
    const double appliedDamage = L2SkillFunc::ApplyDamageToTarget(creature, target, damage, false);
    creature->IncreaseHP_diff(appliedDamage * m_dRatio / 100.0, false, 1, creature->m_nId, false);
    return true;
}

bool CSkillEffect_i_hp_per_max::InstantEffect(
    CCreature* creature, CCreature& target, double amount, const CSkillInfo* skillInfo
) {
    // Current L2Server 0x00a8930c.
    L2_GUARD_FUNCTION();
    if (!target.m_SkillMod.CanGetDamageHP(creature, &target)) {
        return false;
    }

    const unsigned int sourceId = creature != nullptr ? creature->m_nId : 0;
    int difference = runtime::Win64CvttDoubleToInt32(amount / 100.0 * target.m_data->m_dBaseHP);
    difference = CSkillEffect_p_potion_bonus::CalcHealBonus(creature, difference);
    const int appliedDifference = runtime::Win64CvttDoubleToInt32(
        target.IncreaseHP_diff(static_cast<double>(difference), false, 1, sourceId, false)
    );

    if (amount <= 0.0) {
        if (creature == nullptr || creature->IsVisible()) {
            if (skillInfo != nullptr && target.IsPC()) {
                // The current area path reaches this branch with a null
                // source and still dereferences it while assembling the
                // packet. Preserve that shipped behavior literally.
                User* const targetUser = target.CastUser();
                const int encodedSkillLevel =
                    (skillInfo->m_nSubLevel << 16) | (static_cast<unsigned int>(skillInfo->m_nLevel) & 0xffffU);
                target.CastUser()->GetSocket()->Send(
                    "chccScddcdcddd", 0x62, SystemMessageId::TARGET_RECEIVED_DAMAGE_FROM_SKILL, 4, 0x0c,
                    targetUser->GetName2(nullptr), 4, skillInfo->m_nSkillId, encodedSkillLevel, 1, -appliedDifference, 0x10,
                    static_cast<int>(target.m_nId), static_cast<int>(creature->m_nId), appliedDifference
                );
            }
        } else {
            CCreature::SendMessageOnDamaged(&target, creature, -appliedDifference, true, true, 0, appliedDifference);
        }
    } else {
        L2SkillFunc::SendHpMpChangedSystemMessage(creature, target, appliedDifference, SystemMessageId::HP_RESTORED);
    }
    return true;
}

bool CSkillEffect_i_hp_per_max::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a89590.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    return targetCreature != nullptr && InstantEffect(nullptr, *targetCreature, m_dAmount, skillInfo);
}

bool CSkillEffect_i_hp_per_max::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a89678.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    return InstantEffect(creature, *target->CastCreature(), m_dAmount, skillInfo);
}

bool CSkillEffect_i_mp_per_max::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a89764.
    L2_GUARD_FUNCTION();
    return Instant(nullptr, target, skillInfo, nullptr, 0.0);
}

bool CSkillEffect_i_mp_per_max::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a89830.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    const unsigned int sourceId = creature != nullptr ? creature->m_nId : 0;
    const int appliedDifference = runtime::Win64CvttDoubleToInt32(
        targetCreature->IncreaseMP_diff(m_dAmount / 100.0 * targetCreature->m_data->m_dBaseMP, 3, sourceId, false)
    );
    L2SkillFunc::SendHpMpChangedSystemMessage(creature, *targetCreature, appliedDifference, SystemMessageId::MP_RESTORED);
    return true;
}

bool CSkillEffect_i_mp_by_level::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8a288.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    const double scaledAmount = static_cast<double>(nAmount) * 1.7;
    const double bonusAmount = static_cast<double>(targetCreature->m_SkillMod.m_nManaChargeBonus + nAmount);
    const double charge = scaledAmount <= bonusAmount ? scaledAmount : bonusAmount;

    const int levelDelta = targetCreature->m_data->m_nLevel - skillInfo->m_nMagicLevel - 5;
    const int nonnegativeDelta = levelDelta < 0 ? 0 : levelDelta;
    int probability = 0;
    if ((10 - nonnegativeDelta) * 10 >= 0) {
        probability = levelDelta < 0 ? 100 : (10 - levelDelta) * 10;
    }

    const unsigned int sourceId = creature == nullptr ? 0U : creature->m_nId;
    const double applied =
        targetCreature->IncreaseMP_diff(static_cast<double>(probability) * charge / 100.0, 3, sourceId, false);
    targetCreature->BroadcastHPMP(3, sourceId);
    L2SkillFunc::SendHpMpChangedSystemMessage(
        creature, *targetCreature, runtime::Win64CvttDoubleToInt32(applied), SystemMessageId::MP_RESTORED
    );
    return true;
}

bool CSkillEffect_i_mp_by_level_self::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8b6dc.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(LOG_ERROR, L"[%s][%d] [CCreature *pSkillUser] is nullptr", L"SkillFxFunc_instant.cpp", 0xe6d)
        // L2_LOG_ERROR(Log, L"[%s][%d] [CCreature *pSkillUser] is nullptr", L"SkillFxFunc_instant.cpp", 0xe6d)
        L2_LOG_ERROR(Log, "Cannot apply i_mp_by_level_self: the caster is unavailable; the effect fails.");
        return false;
    }
    if (!target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    const int levelDelta = creature->m_data->m_nLevel - targetCreature->m_data->m_nLevel;
    const int nonnegativeDelta = levelDelta < 0 ? 0 : levelDelta;
    int probability = 0;
    if ((10 - nonnegativeDelta) * 10 >= 0) {
        probability = (10 - (levelDelta < 0 ? 0 : levelDelta)) * 10;
    }

    const int amount = nAmount * probability / 100;
    creature->IncreaseMP_diff(static_cast<double>(amount), 3, creature->m_nId, false);
    if (creature->IsSummonOrPet()) {
        creature->CastSummon()->SendSummonStatusToMaster(3);
    }
    return true;
}

bool CSkillEffect_i_cp_per_max::InstantEffect(
    CCreature* creature, CCreature& target, double amount, const CSkillInfo* skillInfo
) {
    // Current L2Server 0x00a8996c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(&target)) {
        return false;
    }

    User* const targetUser = target.CastUser();
    if (!targetUser->m_SkillMod.CanGetDamageHP(creature, targetUser)) {
        return false;
    }

    const unsigned int sourceId = creature != nullptr ? creature->m_nId : 0;
    const int appliedDifference = runtime::Win64CvttDoubleToInt32(
        target.IncreaseCP_diff(amount / 100.0 * target.m_data->m_dBaseCP, 2, sourceId, false)
    );

    if (amount <= 0.0) {
        if (creature == nullptr || creature->IsVisible()) {
            if (skillInfo != nullptr && target.IsPC()) {
                // See the matching current HP path above.
                User* const packetUser = target.CastUser();
                const int encodedSkillLevel =
                    (skillInfo->m_nSubLevel << 16) | (static_cast<unsigned int>(skillInfo->m_nLevel) & 0xffffU);
                target.CastUser()->GetSocket()->Send(
                    "chccScddcdcddd", 0x62, SystemMessageId::TARGET_RECEIVED_DAMAGE_FROM_SKILL, 4, 0x0c,
                    packetUser->GetName2(nullptr), 4, skillInfo->m_nSkillId, encodedSkillLevel, 1, -appliedDifference, 0x10,
                    static_cast<int>(target.m_nId), static_cast<int>(creature->m_nId), appliedDifference
                );
            }
        } else {
            CCreature::SendMessageOnDamaged(&target, creature, -appliedDifference, true, true, 0, appliedDifference);
        }
    } else {
        L2SkillFunc::SendHpMpChangedSystemMessage(creature, target, appliedDifference, SystemMessageId::CP_RESTORED);
    }
    return true;
}

bool CSkillEffect_i_cp_per_max::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a89c00.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    return InstantEffect(nullptr, *static_cast<CCreature*>(target), m_dAmount, skillInfo);
}

bool CSkillEffect_i_cp_per_max::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a89cd4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    return InstantEffect(creature, *target->CastCreature(), m_dAmount, skillInfo);
}

CSkillEffect_i_backstab::CSkillEffect_i_backstab(double power, double bonus, int specialProb) :
    m_dPower(power), m_dBonus(bonus), m_nSpecialProb(specialProb) {
    // Current L2Server 0x00ca3f88.
    eEffectType = EFT_BACKSTAB;
}

bool CSkillEffect_i_backstab::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a95d7c.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    double physicalAttack = 0.0;
    double randomDamage = 0.0;
    double fatalBlowBaseProbability = 0.0;
    {
        L2_GUARD(*creature->m_lock);
        physicalAttack = creature->m_data->m_dParam[VALIDATE::PATTACK];
        randomDamage = creature->GetWeaponRandomDamage();
        fatalBlowBaseProbability = creature->DexBonusF() * creature->EquipedWeaponCriticalProb();
    }

    CCreature* const targetCreature = static_cast<CCreature*>(target);
    double physicalDefence = 0.0;
    double shieldDefence = 0.0;
    bool perfectShieldDefence = false;
    double weaponTraitBonus = 1.0;
    double skillAttributeBonus = 1.0;
    std::uint16_t targetYaw = 0;
    {
        L2_GUARD(*targetCreature->m_lock);
        physicalDefence = targetCreature->GetPDefend();
        targetYaw = static_cast<std::uint16_t>(targetCreature->GetDir().Yaw);
        perfectShieldDefence = L2SkillFunc::ApplyShieldDefense(
            *creature, targetCreature, 1.0, shieldDefence,
            ShouldNotifyPhysicalShieldDefence(skillInfo, skillAction != nullptr && skillAction->m_Infected)
        );
        weaponTraitBonus = targetCreature->GetWeaponTraitBonus(creature->m_data->m_AttackType);
        skillAttributeBonus = creature->GetSkillAttributeBonus(targetCreature, skillInfo, true);
    }

    double fatalProbability =
        CAttackAction::CalcFatalBlowSuccessProb(fatalBlowBaseProbability, creature, targetCreature, m_dBonus);
    const FVector creaturePosition = creature->GetPos();
    const FVector targetPosition = targetCreature->GetPos();
    const int attackYaw = runtime::Win64CvttDoubleToInt32(
        std::atan2(targetPosition.y - creaturePosition.y, targetPosition.x - creaturePosition.x) * 65535.0 /
        6.283185307179586
    );
    const std::uint16_t absoluteYawDifference = static_cast<std::uint16_t>(
        std::abs(static_cast<int>(targetYaw) - static_cast<int>(static_cast<std::uint16_t>(attackYaw)))
    );
    const std::uint32_t centeredYawDifference = static_cast<std::uint32_t>(absoluteYawDifference) - 0x4000U;
    const double minimumProbability = centeredYawDifference <= 0x7fffU ? 3.0 : 100.0;
    if (fatalProbability < minimumProbability) {
        fatalProbability = minimumProbability;
    }

    if (genrand64_real1() * 100.0 <= fatalProbability) {
        double soulShotBonus = creature->GetBonusAndTurnNextSoulShotState() + 1.0;
        if (soulShotBonus > 1.0) {
            soulShotBonus *= creature->GetSoulShotExtraBonus() + 1.0;
        }

        double damage = 1.0;
        if (!perfectShieldDefence) {
            double positionBonusRate = 0.0;
            double criticalDamagePositionBonusPer = 1.0;
            double criticalDamagePositionBonusDiff = 0.0;
            CAttackAction::SetPositionBonus(
                creature, target, positionBonusRate, criticalDamagePositionBonusPer, criticalDamagePositionBonusDiff
            );

            physicalDefence *= creature->m_SkillMod.m_dArmorPenetrateFactor;
            shieldDefence *= creature->m_SkillMod.m_dShieldPenetrateFactor;
            const CCreatureSkillMod::Mod& defenceCriticalDamage =
                targetCreature->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceCriticalDamage];
            const double defenceCriticalDamagePer = defenceCriticalDamage.m_Per * 0.5 + 0.5;
            const double defenceCriticalDamageDiff = defenceCriticalDamage.m_Diff * 0.5;

            const AttackType attackType = creature->GetRealAttackType();
            const bool bowAttack = attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW;
            const double damageConstant = bowAttack ? 70.0 : 77.0;
            const double positionDamage = (soulShotBonus * physicalAttack + m_dPower) * randomDamage * positionBonusRate;
            const double attackTerm =
                ((creature->LevelBonus() * physicalAttack + m_dPower) * soulShotBonus / 1.5) * randomDamage;
            const double positionMultiplier = (criticalDamagePositionBonusPer - 1.0) * 0.5 + 1.0;
            const double additiveCriticalDamage =
                (creature->GetCriticalDamageDiff() + defenceCriticalDamageDiff + criticalDamagePositionBonusDiff) * 6.0;
            const double multiplicativeCriticalDamage =
                positionMultiplier * creature->GetCriticalDamagePer() * attackTerm * defenceCriticalDamagePer;
            damage = (additiveCriticalDamage + multiplicativeCriticalDamage + positionDamage) * damageConstant /
                (shieldDefence + physicalDefence) * weaponTraitBonus * skillAttributeBonus;

            if (target->IsPCOrSummon()) {
                damage *= creature->m_SkillMod.m_dPvpPhysicalSkillBonusPer;
                User* const user = target->GetUserOrMaster();
                if (user != nullptr) {
                    damage *= user->m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer;
                }
            } else if (targetCreature->IsBoss()) {
                damage *= creature->m_SkillMod.m_dPvBossPhysicalSkillBonusPer;
            } else {
                damage *= creature->m_SkillMod.m_dPvePhysicalSkillBonusPer;
            }

            const double criticalProbability = L2SkillFunc::CalcPhysicalSkillCritcalBonus(
                *creature, skillInfo->m_nSkillId, true, static_cast<double>(m_nSpecialProb)
            );
            if (genrand64_real1() * 100.0 < criticalProbability) {
                damage = L2SkillFunc::CalcPysicalSkillCriticalDamage(*creature, damage);
                if (skillAction != nullptr) {
                    L2SkillFunc::CheckActionIsCritical(skillAction);
                }
            }
            damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        }

        creature->SendSystemMessage(SystemMessageId::CHARACTER_CRITICAL_HIT);
        creature->TriggerAlterSkill(AlterSkill::Critical, ABNS_INVALID);
        CCreature::SendMagicAttckInfo(creature, targetCreature, MagicAttackInfo::Critical);
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, creature->IsPC(), DAMAGE_OVER_HIT, true);
        targetCreature->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    }
    return true;
}

void CSkillEffect_i_fatal_blow::CalcSuccess(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2* skillAction
) {
    // Current L2Server 0x00a85eb0.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature() || skillAction == nullptr) {
        return;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return;
    }

    double baseProbability = 0.0;
    {
        L2_GUARD(*creature->m_lock);
        baseProbability = creature->DexBonusF() * creature->EquipedWeaponCriticalProb();
    }

    const double probability =
        std::min(CAttackAction::CalcFatalBlowSuccessProb(baseProbability, creature, targetCreature, m_dBonus), 80.0);
    skillAction->m_nFatalBlowSuccess = probability > genrand64_real1() * 100.0 ? 1 : 0;
}

bool CSkillEffect_i_fatal_blow::ApplyBlowEffect(
    CCreature* creature, CObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double, bool useSoul
) {
    // Current L2Server 0x00a94330.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target) || skillAction == nullptr) {
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(target);
    double physicalDefence = 0.0;
    double shieldDefence = 0.0;
    bool perfectShieldDefence = false;
    {
        L2_GUARD(*targetCreature->m_lock);
        physicalDefence = targetCreature->m_data->m_dParam[VALIDATE::PDEFEND];

        bool notifyShieldDefence = true;
        if (skillInfo != nullptr && skillAction->m_Infected) {
            switch (skillInfo->m_OperateType) {
                case SKILL_DURATION:
                case SKILL_DURATION_LEVEL_UP:
                case SKILL_SELFDURATION:
                case SKILL_CHARGE_DURATION:
                case SKILL_AURA_ACTIVITY:
                case SKILL_BACK_STEP_DURATION:
                case SKILL_AURA_ACTIVITY2:
                case SKILL_PARTY_INSTANT:
                case SKILL_DUAL_PUMP1:
                case SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK:
                    notifyShieldDefence = false;
                    break;
                default:
                    break;
            }
        }
        perfectShieldDefence = L2SkillFunc::ApplyShieldDefense(*creature, target, 3.0, shieldDefence, notifyShieldDefence);
    }

    const double weaponTraitBonus = targetCreature->GetWeaponTraitBonus(creature->m_data->m_AttackType);
    const double skillAttributeBonus = creature->GetSkillAttributeBonus(targetCreature, skillInfo, true);

    if (skillAction->m_nFatalBlowSuccess == 1) {
        double physicalAttack = 0.0;
        double randomDamage = 0.0;
        {
            L2_GUARD(*creature->m_lock);
            physicalAttack = creature->m_data->m_dParam[VALIDATE::PATTACK];
            randomDamage = creature->GetWeaponRandomDamage();
        }

        double shotBonus = creature->GetBonusAndTurnNextSoulShotState() + 1.0;
        if (shotBonus > 1.0) {
            shotBonus *= creature->GetSoulShotExtraBonus() + 1.0;
        }

        // Perfect shield defence still applies the current one-point result.
        double damage = 1.0;
        if (!perfectShieldDefence) {
            double positionBonusRate = 0.0;
            double criticalDamagePositionBonusPer = 1.0;
            double criticalDamagePositionBonusDiff = 0.0;
            CAttackAction::SetPositionBonus(
                creature, target, positionBonusRate, criticalDamagePositionBonusPer, criticalDamagePositionBonusDiff
            );

            const double soulBonus = useSoul ? L2SkillFunc::GetSoulBonus(creature, skillInfo) : 1.0;
            const double positionDamage = (shotBonus * physicalAttack + m_dPower) * randomDamage * positionBonusRate;

            physicalDefence *= creature->m_SkillMod.m_dArmorPenetrateFactor;
            shieldDefence *= creature->m_SkillMod.m_dShieldPenetrateFactor;

            const CCreatureSkillMod::Mod& defenceCriticalDamage =
                targetCreature->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceCriticalDamage];
            const double defenceCriticalDamagePer = defenceCriticalDamage.m_Per * 0.5 + 0.5;
            const double defenceCriticalDamageDiff = defenceCriticalDamage.m_Diff * 0.5;

            const AttackType attackType = creature->GetRealAttackType();
            const double damageConstant =
                attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW ? 70.0 : 77.0;
            const double criticalDamagePer = creature->GetCriticalDamagePer();
            const double criticalDamageDiff = creature->GetCriticalDamageDiff();
            const double positionMultiplier = (criticalDamagePositionBonusPer - 1.0) * 0.5 + 1.0;

            double attackTerm = 0.0;
            if (!creature->IsPC() && !creature->IsSummonOrPet()) {
                attackTerm = (shotBonus * physicalAttack + m_dPower) * soulBonus * randomDamage;
            } else {
                attackTerm =
                    ((creature->LevelBonus() * physicalAttack + m_dPower) * shotBonus / 1.5) * soulBonus * randomDamage;
            }

            const double additiveCriticalDamage =
                (criticalDamageDiff + defenceCriticalDamageDiff + criticalDamagePositionBonusDiff) * 6.0;
            const double multiplicativeCriticalDamage =
                positionMultiplier * criticalDamagePer * attackTerm * defenceCriticalDamagePer;
            damage = (additiveCriticalDamage + multiplicativeCriticalDamage + positionDamage) * damageConstant /
                (shieldDefence + physicalDefence);
            damage *= weaponTraitBonus * skillAttributeBonus;

            if (target->IsPCOrSummon()) {
                damage *= creature->m_SkillMod.m_dPvpPhysicalSkillBonusPer;
                User* const user = target->GetUserOrMaster();
                if (user != nullptr) {
                    damage *= user->m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer;
                }
            } else if (target->IsBoss()) {
                damage *= creature->m_SkillMod.m_dPvBossPhysicalSkillBonusPer;
            } else {
                damage *= creature->m_SkillMod.m_dPvePhysicalSkillBonusPer;
            }

            const double criticalProbability = L2SkillFunc::CalcPhysicalSkillCritcalBonus(
                *creature, skillInfo->m_nSkillId, true, static_cast<double>(m_nSpecialProb)
            );
            if (genrand64_real1() * 100.0 < criticalProbability) {
                damage = L2SkillFunc::CalcPysicalSkillCriticalDamage(*creature, damage);
                L2SkillFunc::CheckActionIsCritical(skillAction);
            }
            damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        }

        creature->SendSystemMessage(SystemMessageId::CHARACTER_CRITICAL_HIT);
        CCreature::SendMagicAttckInfo(creature, targetCreature, MagicAttackInfo::Critical);
        const double appliedDamage = targetCreature->GotDamageBy(creature, damage, false, DAMAGE_OVER_HIT, true);
        targetCreature->ApplyGotDamageBySkill(creature, damage, appliedDamage);
    }
    return true;
}

bool CSkillEffect_i_fatal_blow::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
) {
    // Current L2Server 0x00a9a93c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    return ApplyBlowEffect(creature, target, skillInfo, skillAction, distance, false);
}

bool CSkillEffect_i_fatal_blow_abnormal::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
) {
    // Current L2Server 0x00a94ab0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    m_dPower =
        targetCreature->HasAbnormalStatusSlot(m_nAbnormalType) ? m_dDefaultPower + m_dAdditionalBonus : m_dDefaultPower;
    return ApplyBlowEffect(creature, target, skillInfo, skillAction, distance, false);
}

bool CSkillEffect_i_hp_self::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a874e0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }

    creature->IncreaseHP_diff(m_dAmount, true, 4, 0, false);
    return true;
}

bool CSkillEffect_i_set_visible::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a90880.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    target->CastCreature()->SetVisible(true);
    return true;
}

bool CSkillEffect_i_add_hate::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8a470.
    L2_GUARD_FUNCTION();
    if (!target->IsNPC()) {
        return false;
    }

    g_npcserver.Send(
        "cdddddf", 0x20, static_cast<int>(target->Id()), target->GetIndex(), static_cast<int>(creature->Id()),
        creature->GetIndex(), m_nAmount, creature->GetShieldDefense()
    );
    return true;
}

bool CSkillEffect_i_delete_hate::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8a754.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAliveNPC(target)) {
        return false;
    }

    CNPC* const npc = target->CastNPC();
    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, npc, skillInfo);
    if (probability <= genrand64_real1() * 100.0) {
        creature->SendSystemMessage(SystemMessageId::ENMITY_REMOVAL_FAILED);
        return false;
    }

    g_npcserver.Send("cddd", 0x29, static_cast<int>(npc->m_nId), npc->GetIndex(), 0);
    return true;
}

bool CSkillEffect_i_delete_hate_of_me::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8bbd8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || target == nullptr || !target->IsNPC()) {
        return false;
    }

    CNPC* const npc = target->CastNPC();
    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, npc, skillInfo);
    if (probability <= genrand64_real1() * 100.0) {
        creature->SendSystemMessage(SystemMessageId::ENMITY_REMOVAL_FAILED);
    } else {
        g_npcserver.Send(
            "cdddd", 0x2a, static_cast<int>(npc->m_nId), npc->GetIndex(), static_cast<int>(creature->m_nId),
            static_cast<int>(creature->GetSmartID().Sid())
        );
    }
    return true;
}

bool CSkillEffect_i_run_away::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8b218.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || target == nullptr || !target->IsNPC()) {
        return false;
    }

    CNPC* const npc = target->CastNPC();
    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, npc, skillInfo);
    if (genrand64_real1() * 100.0 < probability && npc->m_data->m_nExCrtEffect != 1) {
        g_npcserver.Send(
            "cddddd", 0x2d, static_cast<int>(npc->m_nId), npc->GetIndex(), static_cast<int>(creature->CastUser()->m_nId),
            creature->CastUser()->GetIndex(), nTime
        );
        return true;
    }

    creature->SendSystemMessage(SystemMessageId::FEAR_FAILED);
    return false;
}

bool CSkillEffect_i_delete_topagro::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a920e0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || target == nullptr || !target->IsNPC()) {
        return false;
    }

    CNPC* const npc = target->CastNPC();
    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, npc, skillInfo);
    if (probability <= genrand64_real1() * 100.0) {
        creature->SendSystemMessage(SystemMessageId::ENMITY_REMOVAL_FAILED);
    } else {
        g_npcserver.Send("cdd", 0xac, static_cast<int>(npc->m_nId), npc->GetIndex());
    }
    return true;
}

bool CSkillEffect_i_consume_body::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8b3ec.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsCreature() || !L2Common::IsDeadNPC(target)) {
        return false;
    }

    CNPC* const npc = static_cast<CNPC*>(target);
    if (npc->m_data->m_nClassID == 0x51) {
        return false;
    }

    L2_GUARD(*npc->m_lock);
    const bool alreadyConsumed = npc->m_data->m_bIsCorpse;
    if (!alreadyConsumed) {
        npc->m_data->m_bIsCorpse = true;
    }
    return alreadyConsumed;
}

void CSkillEffect_i_plunder::BroadCastToPartyMember(User* user, CParty* party, int itemClassId, int amount) {
    // Current L2Server 0x00a9f728.
    L2_GUARD_FUNCTION();
    if (party == nullptr || user == nullptr || itemClassId == 0x39) {
        return;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* member : members) {
        if (member->m_nId == user->m_nId) {
            continue;
        }
        CUserSocket* const socket = member->GetSocket();
        const char16_t* const userName = user->GetName2(member);
        if (amount < 2) {
            socket->Send("chccScd", 0x62, SystemMessageId::CHARACTER_SWEEP_OBTAINED_ITEM, 2, 0x0c, userName, 3, itemClassId);
        } else {
            socket->Send(
                "chccScdcd", 0x62, SystemMessageId::CHARACTER_SWEEP_OBTAINED_MULTIPLE_ITEMS, 3, 0x0c, userName, 3,
                itemClassId, 1, amount
            );
        }
    }
}

void CSkillEffect_i_plunder::GiveItem(User* user, CParty* party, CNPC* npc) {
    // Current L2Server 0x00a9f92c.
    L2_GUARD_FUNCTION();
    std::optional<InventoryParams> parametersStorage;
    const int* missingItemClassId = nullptr;
    [&] {
        L2_GUARD(*npc->m_lock);
        for (const auto& [itemClassId, amount] : npc->m_CorpseDropList) {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            if (object == nullptr || !object->IsItem()) {
                missingItemClassId = &itemClassId;
                return;
            }

            const ItemRoutingType routingType = party == nullptr ? IRT_PICKER_FIRST : party->GetRoutingType();
            if (party == nullptr ||
                (routingType != IRT_RANDOMLY_INCLUDING_SPOIL && routingType != IRT_ORDERLY_INCLUDING_SPOIL)) {
                InventoryParams& parameters = parametersStorage.emplace();
                parameters.nItemClassID = itemClassId;
                parameters.nCount = amount;
                parameters.m_Reason = ItemGetReason::Etc;
                if (!user->AddItemToInventory(parameters)) {
                    return;
                }

                const FVector position = user->GetPos();
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,%I64d,,%s,%s,,,",
                //     static_cast<std::int64_t>(0x3ba),
                //     static_cast<std::int64_t>(user->GetAccountID()),
                //     static_cast<std::int64_t>(user->GetDBID()),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.z)),
                //     static_cast<std::int64_t>(user->m_data->m_Race),
                //     static_cast<std::int64_t>(user->m_data->m_Sex),
                //     static_cast<std::int64_t>(user->m_data->m_nClass),
                //     static_cast<std::int64_t>(user->m_data->m_nLevel),
                //     static_cast<std::int64_t>(npc->m_data->m_nClassID),
                //     static_cast<std::int64_t>(npc->m_data->m_nLevel),
                //     static_cast<std::int64_t>(itemClassId),
                //     static_cast<std::int64_t>(amount),
                //     user->GetName(),
                //     user->m_sAccountName.data())
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,%I64d,,%s,%s,,,",
                //     static_cast<std::int64_t>(0x3ba),
                //     static_cast<std::int64_t>(user->GetAccountID()),
                //     static_cast<std::int64_t>(user->GetDBID()),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.z)),
                //     static_cast<std::int64_t>(user->m_data->m_Race),
                //     static_cast<std::int64_t>(user->m_data->m_Sex),
                //     static_cast<std::int64_t>(user->m_data->m_nClass),
                //     static_cast<std::int64_t>(user->m_data->m_nLevel),
                //     static_cast<std::int64_t>(npc->m_data->m_nClassID),
                //     static_cast<std::int64_t>(npc->m_data->m_nLevel),
                //     static_cast<std::int64_t>(itemClassId),
                //     static_cast<std::int64_t>(amount),
                //     Utf16ToWide(user->GetName()).c_str(),
                //     user->m_sAccountName.data())
                Log.Add(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,%I64d,,%s,%s,,,",
                    static_cast<std::int64_t>(0x3ba), static_cast<std::int64_t>(user->GetAccountID()),
                    static_cast<std::int64_t>(user->GetDBID()),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
                    static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
                    static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
                    static_cast<std::int64_t>(npc->m_data->m_nClassID), static_cast<std::int64_t>(npc->m_data->m_nLevel),
                    static_cast<std::int64_t>(itemClassId), static_cast<std::int64_t>(amount), user->GetName(),
                    user->m_sAccountName.data()
                );
                BroadCastToPartyMember(user, party, itemClassId, amount);
                parametersStorage.reset();
            } else if (itemClassId != 0x39) {
                party->SweepItem(itemClassId, amount, user);
            }
        }
    }();
    if (missingItemClassId != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"SkillFxFunc_instant.cpp",
        //     0x11c5,
        //     *missingItemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"SkillFxFunc_instant.cpp",
        //     0x11c5,
        //     *missingItemClassId)
        L2_LOG_ERROR(
            Log,
            "i_plunder reward distribution stopped: class ID %d has no item prototype; earlier reward operations are not "
            "rolled back.",
            *missingItemClassId
        );
    }
}

bool CSkillEffect_i_plunder::CheckInven(User* user, CNPC* npc) {
    // Current L2Server 0x00aa56a4.
    L2_GUARD_FUNCTION();
    ItemAmountDataStruct rewards;
    {
        L2_GUARD(*npc->m_lock);
        for (const auto& [itemClassId, amount] : npc->m_CorpseDropList) {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            CItem* const item = static_cast<CItem*>(object);
            if (item == nullptr) {
                continue;
            }
            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                rewards.Push(itemClassId, amount);
            } else if (amount > 0) {
                for (int index = 0; index < amount; ++index) {
                    rewards.Push(itemClassId, 1);
                }
            }
        }
    }

    if (rewards.dataList.empty()) {
        return true;
    }

    CParty* const party = user->GetParty();
    const ItemRoutingType routingType = party == nullptr ? IRT_PICKER_FIRST : party->GetRoutingType();
    if (party == nullptr || (routingType != IRT_RANDOMLY_INCLUDING_SPOIL && routingType != IRT_ORDERLY_INCLUDING_SPOIL)) {
        if (!user->m_inv.CheckAddableMulti(rewards, false)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
            return false;
        }
        return true;
    }

    for (const ItemAmountData* reward : rewards.dataList) {
        if (reward == nullptr) {
            continue;
        }
        const int itemClassId = reward->m_nItemClassId;
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr || !object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1224,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1224,
            //     itemClassId)
            L2_LOG_ERROR(
                Log, "i_plunder party inventory check failed: reward class ID %d has no item prototype.", itemClassId
            );
            return false;
        }
        CItem* const item = static_cast<CItem*>(object);
        if (itemClassId != 0x39 && !party->CheckRouteItem(item, user)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
        }
    }
    return true;
}

void CSkillEffect_i_plunder::MakeDropList(User* user, CNPC* npc) {
    // Current L2Server 0x00aabc60.
    L2_GUARD_FUNCTION();
    if (user == nullptr || npc == nullptr) {
        return;
    }

    double privateDropChance = 1.0;
    if (user->m_SkillDurationMod.bItemChanceActivated) {
        privateDropChance = npc->GetPrivateDropCorpseItemChanceProb(user);
    }

    CNPCClassConst* const npcClass = npc->m_pClassConst;
    {
        L2_GUARD(*npc->m_lock);
        npc->m_CorpseDropList.clear();
    }

    const int userLevel = user->m_data->m_nLevel;
    double eventRate = 1.0;
    std::vector<CEvent*> events;
    Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
    for (CEvent* event : events) {
        if (event != nullptr && event->InEventTime()) {
            eventRate = static_cast<CEvent_EconomyControlEvent*>(event)->GetSpoilDropRate(userLevel);
            break;
        }
    }

    for (const ItemDropStruct& drop : npcClass->m_CorpseMakeList) {
        double probability = user->GetSpoilBonusPer() * g_dSpoilItemDropRatePer * drop.DropProb * eventRate;
        CObject* const object = g_ObjectDB.GetObjectW(drop.ItemClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item != nullptr && item->m_const->m_bIsItemChanceAffected) {
            probability *= privateDropChance;
        }

        if (genrand64_real1() * 100.0 > probability) {
            continue;
        }
        const int amount = static_cast<int>(
            genrand64_real1() * static_cast<double>(drop.DropMax - drop.DropMin) + static_cast<double>(drop.DropMin) + 0.5
        );
        if (amount <= 0) {
            continue;
        }

        {
            L2_GUARD(*npc->m_lock);
            const auto found = npc->m_CorpseDropList.find(drop.ItemClassId);
            if (found == npc->m_CorpseDropList.end()) {
                npc->m_CorpseDropList.emplace(drop.ItemClassId, amount);
                npc->m_data->m_bItemCollectable = true;
            } else {
                found->second += amount;
            }
        }
    }
}

bool CSkillEffect_i_plunder::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aac2f8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || !L2Common::IsAliveNPC(target)) {
        return false;
    }

    User* const user = creature->CastUser();
    CNPC* const npc = target->CastNPC();
    const int npcClassId = npc->m_data->m_nClass;
    if (npcClassId == 0x51 || npcClassId == 0x52) {
        user->SendSystemMessage(SystemMessageId::TARGET_CANNOT_BE_SPOILED);
        return false;
    }
    if (npc->m_data->m_bIsPlunded) {
        user->SendSystemMessage(SystemMessageId::TARGET_ALREADY_PLUNDERED);
        return false;
    }

    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    int levelDifference = 0;
    if (npc->Level() - skillInfo->m_nMagicLevel > 0) {
        levelDifference = npc->Level() - skillInfo->m_nMagicLevel;
    }
    int chance = 5;
    if ((20 - levelDifference) * 5 > 4) {
        levelDifference = 0;
        if (npc->Level() - skillInfo->m_nMagicLevel > 0) {
            levelDifference = npc->Level() - skillInfo->m_nMagicLevel;
        }
        chance = (20 - levelDifference) * 5;
    }
    if (genrand64_real1() * 100.0 < static_cast<double>(chance)) {
        {
            L2_GUARD(*npc->m_lock);
            npc->m_data->m_bIsPlunded = true;
        }

        float successProbability = 100.0F;
        const int userDifference = npc->m_data->m_nLevel - creature->CastUser()->m_data->m_nLevel;
        if (userDifference < -5) {
            successProbability = userDifference < -10 ? 10.0F : static_cast<float>(userDifference) * 18.0F + 190.0F;
        }
        if (static_cast<float>(std::rand() % 100) <= successProbability) {
            {
                L2_GUARD(*npc->m_lock);
                npc->m_CorpseDropList.clear();
            }
            MakeDropList(user, npc);
        }

        const bool empty = [&]() -> bool {
            L2_GUARD(*npc->m_lock);
            const bool empty = npc->m_CorpseDropList.empty();
            return empty;
        }();
        if (empty) {
            return true;
        }

        CParty* const party = creature->IsPC() ? creature->CastUser()->GetParty() : nullptr;
        GiveItem(user, party, npc);
        if (user->EventByLuckyStat(false) && CheckInven(user, npc)) {
            GiveItem(user, party, npc);
            SkillUseInfo skillUseInfo;
            skillUseInfo.m_skillID = 18104;
            skillUseInfo.m_skillLevel = 1;
            int zero1 = 0;
            int hitTime = 500;
            int zero2 = 0;
            int reuseDelay = 1500;
            int zero3 = 0;
            int zero4 = 0;
            DefaultIOBuffer* const packet = user->MakePacketForMagicSkillUse(
                zero1, user, skillUseInfo, hitTime, zero2, reuseDelay, zero3, zero4, nullptr
            );
            g_ptlUserMap->BroadcastToNeighborDist(user, 0xf000, user->GetPos(), user->GetBroadcastDist(0x1000), packet);
            user->SendSystemMessage(SystemMessageId::LADY_LUCK_ACTIVATED);
        }
    }
    return true;
}

bool CSkillEffect_i_sweeper::IsSpoilRouting(CParty* party) {
    // Current L2Server 0x00a8bac0.
    L2_GUARD_FUNCTION();
    if (party == nullptr) {
        return false;
    }
    const ItemRoutingType routingType = party->GetRoutingType();
    return routingType == IRT_RANDOMLY_INCLUDING_SPOIL || routingType == IRT_ORDERLY_INCLUDING_SPOIL;
}

void CSkillEffect_i_sweeper::BroadCastToPartyMember(User* user, CParty* party, int itemClassId, int amount) {
    // Current L2Server 0x00a9f160.
    L2_GUARD_FUNCTION();
    if (party == nullptr || user == nullptr || itemClassId == 0x39) {
        return;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* member : members) {
        if (member->m_nId == user->m_nId) {
            // The current body returns here. It does not continue to later
            // party members after encountering the acting user.
            return;
        }
        CUserSocket* const socket = member->GetSocket();
        const char16_t* const userName = user->GetName2(member);
        if (amount < 2) {
            socket->Send("chccScd", 0x62, SystemMessageId::CHARACTER_SWEEP_OBTAINED_ITEM, 2, 0x0c, userName, 3, itemClassId);
        } else {
            socket->Send(
                "chccScdcd", 0x62, SystemMessageId::CHARACTER_SWEEP_OBTAINED_MULTIPLE_ITEMS, 3, 0x0c, userName, 3,
                itemClassId, 1, amount
            );
        }
    }
}

void CSkillEffect_i_sweeper::GiveItem(User* user, CParty* party, CNPC* npc) {
    // Current L2Server 0x00a9f37c.
    L2_GUARD_FUNCTION();
    std::optional<InventoryParams> parametersStorage;
    const int* missingItemClassId = nullptr;
    [&] {
        L2_GUARD(*npc->m_lock);
        for (const auto& [itemClassId, amount] : npc->m_CorpseDropList) {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            if (object == nullptr || !object->IsItem()) {
                missingItemClassId = &itemClassId;
                return;
            }

            if (IsSpoilRouting(party)) {
                if (itemClassId != 0x39) {
                    party->SweepItem(itemClassId, amount, user);
                }
                continue;
            }

            InventoryParams& parameters = parametersStorage.emplace();
            parameters.nItemClassID = itemClassId;
            parameters.nCount = amount;
            parameters.m_Reason = ItemGetReason::Etc;
            if (!user->AddItemToInventory(parameters)) {
                return;
            }

            const FVector position = user->GetPos();
            // Log.Add(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,%I64d,,%s,%s,,,,",
            //     static_cast<std::int64_t>(0x3ba),
            //     static_cast<std::int64_t>(user->GetAccountID()),
            //     static_cast<std::int64_t>(user->GetDBID()),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.z)),
            //     static_cast<std::int64_t>(user->m_data->m_Race),
            //     static_cast<std::int64_t>(user->m_data->m_Sex),
            //     static_cast<std::int64_t>(user->m_data->m_nClass),
            //     static_cast<std::int64_t>(user->m_data->m_nLevel),
            //     static_cast<std::int64_t>(npc->m_data->m_nClassID),
            //     static_cast<std::int64_t>(npc->m_data->m_nLevel),
            //     static_cast<std::int64_t>(itemClassId),
            //     static_cast<std::int64_t>(amount),
            //     user->GetName(),
            //     user->m_sAccountName.data())
            // Log.Add(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,%I64d,,%s,%s,,,,",
            //     static_cast<std::int64_t>(0x3ba),
            //     static_cast<std::int64_t>(user->GetAccountID()),
            //     static_cast<std::int64_t>(user->GetDBID()),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.z)),
            //     static_cast<std::int64_t>(user->m_data->m_Race),
            //     static_cast<std::int64_t>(user->m_data->m_Sex),
            //     static_cast<std::int64_t>(user->m_data->m_nClass),
            //     static_cast<std::int64_t>(user->m_data->m_nLevel),
            //     static_cast<std::int64_t>(npc->m_data->m_nClassID),
            //     static_cast<std::int64_t>(npc->m_data->m_nLevel),
            //     static_cast<std::int64_t>(itemClassId),
            //     static_cast<std::int64_t>(amount),
            //     Utf16ToWide(user->GetName()).c_str(),
            //     user->m_sAccountName.data())
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,%I64d,,%s,%s,,,,",
                static_cast<std::int64_t>(0x3ba), static_cast<std::int64_t>(user->GetAccountID()),
                static_cast<std::int64_t>(user->GetDBID()),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
                static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
                static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
                static_cast<std::int64_t>(npc->m_data->m_nClassID), static_cast<std::int64_t>(npc->m_data->m_nLevel),
                static_cast<std::int64_t>(itemClassId), static_cast<std::int64_t>(amount), user->GetName(),
                user->m_sAccountName.data()
            );
            BroadCastToPartyMember(user, party, itemClassId, amount);
            parametersStorage.reset();
        }
    }();
    if (missingItemClassId != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"SkillFxFunc_instant.cpp",
        //     0x101c,
        //     *missingItemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"SkillFxFunc_instant.cpp",
        //     0x101c,
        //     *missingItemClassId)
        L2_LOG_ERROR(
            Log,
            "i_sweeper reward distribution stopped: class ID %d has no item prototype; earlier reward operations are not "
            "rolled back.",
            *missingItemClassId
        );
    }
}

bool CSkillEffect_i_sweeper::CheckInven(User* user, CNPC* npc) {
    // Current L2Server 0x00aa53b8.
    L2_GUARD_FUNCTION();
    ItemAmountDataStruct rewards;

    {
        L2_GUARD(*npc->m_lock);
        for (const auto& [itemClassId, amount] : npc->m_CorpseDropList) {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            CItem* const item = static_cast<CItem*>(object);
            if (item == nullptr) {
                continue;
            }
            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                rewards.Push(itemClassId, amount);
            } else if (amount > 0) {
                for (int index = 0; index < amount; ++index) {
                    rewards.Push(itemClassId, 1);
                }
            }
        }
    }

    if (rewards.dataList.empty()) {
        return true;
    }

    CParty* const party = user->GetParty();
    if (!IsSpoilRouting(party)) {
        if (!user->m_inv.CheckAddableMulti(rewards, false)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
            return false;
        }
        return true;
    }

    for (const ItemAmountData* reward : rewards.dataList) {
        if (reward == nullptr) {
            continue;
        }
        const int itemClassId = reward->m_nItemClassId;
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr || !object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1079,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1079,
            //     itemClassId)
            L2_LOG_ERROR(
                Log, "i_sweeper party inventory check failed: reward class ID %d has no item prototype.", itemClassId
            );
            return false;
        }
        CItem* const item = static_cast<CItem*>(object);
        if (itemClassId != 0x39 && !party->CheckRouteItem(item, user)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
        }
    }
    return true;
}

bool CSkillEffect_i_sweeper::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00aa63b0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || !L2Common::IsDeadNPC(target)) {
        return false;
    }

    User* const user = creature->CastUser();
    CNPC* const npc = target->CastNPC();
    if (!CSkillOperateCondition_op_sweeper::CanSweep(user, npc)) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    const bool empty = [&]() -> bool {
        L2_GUARD(*npc->m_lock);
        const bool empty = npc->m_CorpseDropList.empty();
        return empty;
    }();
    if (empty) {
        return true;
    }

    CParty* const party = creature->IsPC() ? creature->CastUser()->GetParty() : nullptr;
    if (!npc->m_data->m_bItemCollectable) {
        user->SendSystemMessage(SystemMessageId::SWEEP_FAILED_TARGET_NOT_SPOILED);
    } else {
        GiveItem(user, party, npc);
        if (user->EventByLuckyStat(false) && CheckInven(user, npc)) {
            GiveItem(user, party, npc);
            SkillUseInfo skillUseInfo;
            skillUseInfo.m_skillID = 18104;
            skillUseInfo.m_skillLevel = 1;
            int zero1 = 0;
            int hitTime = 500;
            int zero2 = 0;
            int reuseDelay = 1500;
            int zero3 = 0;
            int zero4 = 0;
            DefaultIOBuffer* const packet = user->MakePacketForMagicSkillUse(
                zero1, user, skillUseInfo, hitTime, zero2, reuseDelay, zero3, zero4, nullptr
            );
            g_ptlUserMap->BroadcastToNeighborDist(user, 0xf000, user->GetPos(), user->GetBroadcastDist(0x1000), packet);
            user->SendSystemMessage(SystemMessageId::LADY_LUCK_ACTIVATED);
        }
    }

    {
        L2_GUARD(*npc->m_lock);
        if (!npc->m_data->m_bIsCorpse) {
            npc->m_data->m_bIsCorpse = true;
            return false;
        }
    }
    return true;
}

bool CSkillEffect_i_sowing::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00aa3ca4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x124f)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x124f)
        L2_LOG_ERROR(
            Log, "Cannot sow: the caster is %s; a player is required.", creature == nullptr ? "unavailable" : "not a player"
        );
        return false;
    }
    if (!L2Common::IsAliveNPC(target) || skillAction == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x125a)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x125a)
        L2_LOG_ERROR(
            Log, "Cannot sow: %s.",
            skillAction == nullptr ? "the skill action is unavailable" : "the target is not a living NPC"
        );
        return false;
    }

    const int usedItemClassId = skillAction->m_nUsedItemClassId;
    if (usedItemClassId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] item class id [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1262,
        //     usedItemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] item class id [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1262,
        //     usedItemClassId)
        L2_LOG_ERROR(Log, "Cannot sow: used item class ID %d is not positive.", usedItemClassId);
        return false;
    }

    CObject* const usedObject = g_ObjectDB.GetObjectW(usedItemClassId, false);
    CItem* const usedItem = static_cast<CItem*>(usedObject);
    if (usedItem == nullptr || usedItem->m_const == nullptr || usedItem->m_const->m_enumDefaultAction != ITEM_ACTION_SEED) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] cannot find item item nUsedItemClassId [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1279,
        //     usedItemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] cannot find item item nUsedItemClassId [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1279,
        //     usedItemClassId)
        L2_LOG_ERROR(
            Log, "Cannot sow with item class %d: %s.", usedItemClassId,
            usedItem == nullptr                ? "the item prototype is unavailable"
                : usedItem->m_const == nullptr ? "item metadata is unavailable"
                                               : "the default action is not SEED"
        );
        return false;
    }

    const int manorId = usedItem->m_const->m_nManorId;
    const int manorLevel = usedItem->m_const->m_nManorLev;
    const int cropId = usedItem->m_const->m_nManorCropId;
    const int seedPriceBits = runtime::Int32FromBits(static_cast<std::uint32_t>(usedItem->m_const->m_nDefaultPrice));
    const bool seed2 = usedItem->m_const->m_EtcitemType == 0x0b;

    if (manorId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] item manor id 1 [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1285,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] item manor id 1 [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1285,
        //     manorId)
        L2_LOG_ERROR(Log, "Cannot sow with item class %d: manor ID %d is not positive.", usedItemClassId, manorId);
        return false;
    }
    if (g_ManorDB.GetManor(manorId) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] item manor id 2 [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x128c,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] item manor id 2 [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x128c,
        //     manorId)
        L2_LOG_ERROR(Log, "Cannot sow with item class %d: manor ID %d is not registered.", usedItemClassId, manorId);
        return false;
    }

    CNPC* const npc = target->CastNPC();
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] NPC error",
        //     "SkillFxFunc_instant.cpp",
        //     0x1295)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] NPC error",
        //     "SkillFxFunc_instant.cpp",
        //     0x1295)
        L2_LOG_ERROR(Log, "Cannot sow: the target did not provide an NPC object.");
        return false;
    }
    User* const user = creature->CastUser();

    if (npc->m_data->m_bUnsowing) {
        user->SendSystemMessage(SystemMessageId::TARGET_CANNOT_BE_SEEDED);
        return false;
    }
    if (npc->m_data->m_nCanHarvest != manorId) {
        user->SendSystemMessage(
            npc->m_data->m_nCanHarvest < 1 ? SystemMessageId::INCORRECT_TARGET : SystemMessageId::TARGET_ALREADY_SEEDED
        );
        return false;
    }
    if (!npc->m_data->m_bIsSow) {
        user->SendSystemMessage(SystemMessageId::TARGET_ALREADY_SEEDED);
        return false;
    }
    if (npc->m_data->m_nClassID == 0x51) {
        user->SendSystemMessage(SystemMessageId::INCORRECT_TARGET);
        return false;
    }

    const int userLevelDifference = std::abs(npc->m_data->m_nLevel - user->m_data->m_nLevel);
    const int manorLevelDifference = std::abs(npc->m_data->m_nLevel - manorLevel);
    if ((userLevelDifference > 5 && genrand64_real1() * 100.0 <= static_cast<double>((userLevelDifference - 5) * 5)) ||
        (manorLevelDifference > 5 && genrand64_real1() * 100.0 <= static_cast<double>((manorLevelDifference - 5) * 5)) ||
        !creature->CanDoAction_china()) {
        user->SendSystemMessage(SystemMessageId::SOWING_FAILED);
        return true;
    }

    CObject* const cropObject = g_ObjectDB.GetObjectW(cropId, false);
    CItem* const crop = static_cast<CItem*>(cropObject);
    if (crop == nullptr || crop->m_const == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] cannot find item item nCropId [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1310,
        //     cropId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] cannot find item item nCropId [%d]",
        //     "SkillFxFunc_instant.cpp",
        //     0x1310,
        //     cropId)
        L2_LOG_ERROR(
            Log, "Cannot sow with item class %d: crop item %d has no usable prototype or metadata.", usedItemClassId, cropId
        );
        return false;
    }

    const std::int64_t numerator = seed2 ? static_cast<std::int64_t>(seedPriceBits)
                                         : npc->m_pClassConst->m_nItemDropValue + static_cast<std::int64_t>(seedPriceBits);
    double chance = static_cast<double>(numerator) / static_cast<double>(crop->m_const->m_nDefaultPrice);
    double roll = genrand64_real1();
    if (chance < roll) {
        user->SendSystemMessage(SystemMessageId::SOWING_FAILED);
        return true;
    }

    {
        L2_GUARD(*npc->m_lock);
        npc->m_data->m_bIsSow = false;
        npc->m_data->m_nCanHarvest = 2;
        npc->m_data->m_nSowerSmartID = static_cast<int>(creature->GetSmartID().Sid());
        npc->m_data->m_nCorpseTime += 10;
        npc->m_data->m_bIsSeed2 = seed2;
        while (chance > 0.0 && roll <= chance) {
            const auto found = npc->m_SeedDropList.find(cropId);
            if (found == npc->m_SeedDropList.end()) {
                npc->m_SeedDropList.emplace(cropId, 1);
            } else {
                ++found->second;
            }
            chance -= 1.0;
            roll = genrand64_real1();
        }
    }

    user->SendSystemMessage(SystemMessageId::SOWING_SUCCEEDED);
    CLogFormatter formatter{};
    formatter.WriteLog2_1(
        0x104, creature->CastUser(), nullptr, manorId, cropId, manorLevel, usedItemClassId, seedPriceBits,
        npc->m_data->m_nClassID, 0
    );
    return true;
}

bool CSkillEffect_i_summon_pet::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a9c6c0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] not pc used summon_pet skill.",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2c45)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] not pc used summon_pet skill.",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2c45)
        L2_LOG_ERROR(
            Log, "Cannot apply i_summon_pet: the caster is %s; a player is required.",
            creature == nullptr ? "unavailable" : "not a player"
        );
        return false;
    }
    if (skillAction == nullptr) {
        return false;
    }

    const std::uint32_t petCollarSid = static_cast<std::uint32_t>(skillAction->m_nUsedItemSID);
    CItem* const petCollar = ItemSmartIdManager()->GetObjectW(petCollarSid);
    if (petCollar == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"i_summon_pet::Instant failed. invalid pet collar(%d)",
        //     petCollarSid)
        // L2_LOG_ERROR(Log,
        //     L"i_summon_pet::Instant failed. invalid pet collar(%d)",
        //     petCollarSid)
        L2_LOG_ERROR(
            Log, "Cannot apply i_summon_pet: collar object %u is unavailable.", static_cast<unsigned>(petCollarSid)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid user",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2c58)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid user",
        //     L"SkillFxFunc_instant.cpp",
        //     0x2c58)
        L2_LOG_ERROR(Log, "Cannot apply i_summon_pet: the caster did not provide a user object.");
        return false;
    }

    CPet* const currentPet = user->GetPet();
    const std::uint32_t currentPetClassId = currentPet == nullptr ? 0 : currentPet->GetDBID();
    if (!g_PetManager.IsSummonedPetCollar(petCollar->GetDBID()) || currentPetClassId == petCollar->GetDBID()) {
        creature->CastUser()->SummonPet(petCollar, true);
        return true;
    }
    return false;
}

bool CSkillEffect_i_transfer_hate::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e228.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAliveNPC(target)) {
        return false;
    }

    CCreature* const oldTarget = creature->m_spTarget.GetCreature();
    if (oldTarget == nullptr || oldTarget->m_nId == creature->m_nId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pSkillInfo->m_spOldTarget isn't valid.")
        // L2_LOG_ERROR(Log,
        //     L"pSkillInfo->m_spOldTarget isn't valid.")
        L2_LOG_ERROR(
            Log, "Cannot apply i_transfer_hate: %s.",
            oldTarget == nullptr ? "the caster's current target is unavailable" : "the caster is its own current target"
        );
        return false;
    }

    CNPC* const npc = target->CastNPC();
    const double probability = L2SkillFunc::CalculateProbability(m_Prob, creature, npc, skillInfo);
    if (probability <= genrand64_real1() * 100.0) {
        creature->SendSystemMessage(SystemMessageId::ENMITY_REMOVAL_FAILED);
    } else {
        g_npcserver.Send(
            "cdddd", 0x66, npc->GetIndex(), static_cast<int>(creature->m_nId),
            static_cast<int>(creature->GetSmartID().Sid()), static_cast<int>(oldTarget->GetSmartID().Sid())
        );
    }
    return true;
}

bool CSkillEffect_i_detect_object::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a90cc0.
    L2_GUARD_FUNCTION();
    const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
    CStaticObject* const staticObject = targetInfo.m_pTargetStaticObject;
    if (staticObject == nullptr) {
        return true;
    }

    staticObject->SetTargetable(true, true);
    staticObject->m_data->m_nStealth = staticObject->m_data->m_nStealth == 0;
    if (staticObject->IsDoor()) {
        CDoor* const door = target->CastDoor();
        door->SendDoorStatus(nullptr);
        if (door->m_pEventController != nullptr) {
            door->m_pEventController->SetOnOff(!door->m_data->m_bIsOpen);
        }
        return true;
    }

    if (staticObject->IsChair()) {
        target->CastChair()->SendChairInfo(creature->CastUser()->GetSocket());
    } else if (staticObject->IsSignBoard()) {
        target->CastSignBoard()->SendSignBoardInfo(creature->CastUser()->GetSocket());
    } else {
        staticObject->SendStaticObjectInfo(creature->CastUser()->GetSocket());
    }
    return true;
}

bool CSkillEffect_i_resurrection::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a87c94.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !L2Common::IsDeadCreature(target)) {
        return false;
    }

    if (!target->CastCreature()->m_data->m_bCanBeResurrected) {
        return false;
    }
    return target->CastCreature()->BeResurrectAsked(creature, m_nRestoreRate, false);
}

bool CSkillEffect_i_escape::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a86c40.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    if (!target->IsPC()) {
        return true;
    }

    User* const user = target->CastUser();
    if (user->HasFlagOfSiege()) {
        user->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
        return false;
    }
    if (user->m_PledgeGameUser.IsPlayer()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::SKILL_CANNOT_BE_USED_IN_MATCH);
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    return user->Restart(m_RestartPoint, 0);
}

CSkillEffect_i_real_damage::CSkillEffect_i_real_damage(int power, int criticalProb, int criticalBonus) :
    m_Power(power), m_CriticalProb(criticalProb), m_CriticalBonus(criticalBonus) {
    // Current L2Server 0x00ca4ac4.
}

bool CSkillEffect_i_real_damage::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a98714.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    int power = m_Power;
    if (genrand64_real1() * 100.0 < static_cast<double>(m_CriticalProb)) {
        power = runtime::Int32FromBits(static_cast<std::uint32_t>(power) * static_cast<std::uint32_t>(m_CriticalBonus));
        creature->TriggerAlterSkill(AlterSkill::Critical, ABNS_INVALID);
        CCreature::SendMagicAttckInfo(creature, targetCreature, MagicAttackInfo::Critical);
        creature->SendSystemMessage(SystemMessageId::CHARACTER_CRITICAL_HIT);
    }

    const double resistedDamage = static_cast<double>(runtime::Win64CvttDoubleToInt32(
        static_cast<double>(power) - (1.0 - targetCreature->m_SkillMod.m_dRealRegist) * static_cast<double>(power)
    ));
    const int limitedDamage = runtime::Win64CvttDoubleToInt32(targetCreature->GetDamageByLimit(resistedDamage));
    const double remainingHP = targetCreature->m_data->m_dHP - static_cast<double>(limitedDamage);

    bool preventionRestored = false;
    if (remainingHP > 0.0) {
        targetCreature->m_data->SetHP(remainingHP);
        targetCreature->SendStatusUpdate(VCP_HP, 4, 0);
        targetCreature->m_data->m_LastAttackerServerID = static_cast<std::int32_t>(creature->m_nId);
    } else {
        const std::array<bool, CCreatureSkillMod::PDP_MAX> preventDeath{
            targetCreature->m_SkillMod.m_bPreventDeathParam[CCreatureSkillMod::PDP_HP],
            targetCreature->m_SkillMod.m_bPreventDeathParam[CCreatureSkillMod::PDP_MP],
            targetCreature->m_SkillMod.m_bPreventDeathParam[CCreatureSkillMod::PDP_CP],
        };
        const bool hasPrevention = preventDeath[CCreatureSkillMod::PDP_HP] || preventDeath[CCreatureSkillMod::PDP_MP] ||
            preventDeath[CCreatureSkillMod::PDP_CP];
        bool preventionDeleted = false;
        if (hasPrevention) {
            L2_GUARD(targetCreature->m_InfectedSkillsSect);
            preventionDeleted = targetCreature->_DeleteAbnormalStatus(
                ABNS_PREVENTION_DEATH, targetCreature, nullptr, true, SPET_SELF_DISPEL, false
            );
        }

        if (preventionDeleted) {
            if (preventDeath[CCreatureSkillMod::PDP_HP]) {
                targetCreature->m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
                    targetCreature->m_data->m_dHP, targetCreature->m_data->m_dBaseHP, 0x0b
                );
            }
            if (preventDeath[CCreatureSkillMod::PDP_MP]) {
                targetCreature->m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
                    targetCreature->m_data->m_dMP, targetCreature->m_data->m_dBaseMP, 0x0c
                );
            }
            if (preventDeath[CCreatureSkillMod::PDP_CP]) {
                targetCreature->m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
                    targetCreature->m_data->m_dCP, targetCreature->m_data->m_dBaseCP, 0x0d
                );
            }
            preventionRestored = true;
        } else if (!targetCreature->m_data->m_bUndying) {
            targetCreature->m_data->SetHP(0.0);
            targetCreature->SendStatusUpdate(VCP_HP, 4, 0);
            targetCreature->Die(creature, 0);
        } else {
            targetCreature->m_data->SetHP(1.0);
            targetCreature->SendStatusUpdate(VCP_HP, 4, 0);
        }
    }

    if (!preventionRestored) {
        targetCreature->m_data->m_NeedUpdatePacketBitSet.ClearBit(0x0b);
    }
    CCreature::SendMessageOnDamaged(targetCreature, creature, limitedDamage, true, true, 0, -limitedDamage);
    targetCreature->SafeAccumulateContribute(creature, resistedDamage);
    return true;
}

bool CSkillEffect_i_rebalance_hp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa0968.
    L2_GUARD_FUNCTION();
    if (!target->IsPC() || creature == nullptr) {
        return false;
    }

    User* const targetUser = target->CastUser();
    CParty* const party = targetUser->GetParty();
    if (party == nullptr) {
        if (!creature->IsPC()) {
            return false;
        }

        double hpMaxSum = creature->m_data->m_dBaseHP;
        double hpSum = creature->m_data->m_dHP;
        std::vector<CSummon*> summons;
        creature->CastUser()->GetAllSummonAndPet(summons);
        for (CSummon* const summon : summons) {
            // Recovered source combined the null/action conditions here:
            // if (summon == nullptr || !summon->IsAlive() || ...)
            // Current 0x00aa0968 enters "in_code" for every non-null item.
            if (summon == nullptr) {
                continue;
            }
            GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
            if (!summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) {
                continue;
            }
            const double hp = summon->m_data->m_dHP;
            if (hp <= 0.0) {
                if (hp < 0.0) {
                    summon->m_data->SetHP(1.0);
                }
                continue;
            }
            hpMaxSum += summon->m_data->m_dBaseHP;
            hpSum += hp;
            if (RebalanceHpTotalOverflow(
                    // L"HP over INT_MAX hpSum(%d)(%f) "
                    // L"hpMaxSum(%d)(%f)!!!!.",
                    L"HP redistribution", hpSum, hpMaxSum
                )) {
                return false;
            }
        }

        creature->IncreaseHP_diff(
            hpSum * creature->m_data->m_dBaseHP / hpMaxSum - creature->m_data->m_dHP, true, 1, creature->m_nId, false
        );
        for (CSummon* const summon : summons) {
            // Recovered source used one positive condition here. The current
            // image installs "in_code" before evaluating its action tests.
            // if (summon != nullptr && summon->IsAlive() && ...)
            if (summon == nullptr) {
                continue;
            }
            GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
            if (!summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) {
                continue;
            }
            summon->IncreaseHP_diff(
                hpSum * summon->m_data->m_dBaseHP / hpMaxSum - summon->m_data->m_dHP, true, 1, creature->m_nId, false
            );
        }
        return true;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    double hpMaxSum = 0.0;
    double hpSum = 0.0;
    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }
        if (member->IsAlive() && IsInRebalanceRange(*creature, *member) && member->m_data->m_dHP > 0.0) {
            hpMaxSum += member->m_data->m_dBaseHP;
            hpSum += member->m_data->m_dHP;
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            member, summon,
            // Recovered source evaluated all tests in one expression. Current
            // 0x00aa0968 creates "in_code" immediately after the null test.
            // if (summon != nullptr && summon->IsAlive() && ...)
            if (summon->IsAlive() && IsInRebalanceRange(*creature, *summon) && summon->m_data->m_dHP > 0.0) {
                hpMaxSum += summon->m_data->m_dBaseHP;
                hpSum += summon->m_data->m_dHP;
            }
        );
    }
    if (RebalanceHpTotalOverflow(
            // L"HP over INT_MAX hpSum(%d)(%f) "
            // L"hpMaxSum(%d)(%f)!!!!.",
            L"HP redistribution", hpSum, hpMaxSum
        )) {
        return false;
    }

    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }
        if (member->IsAlive() && IsInRebalanceRange(*creature, *member)) {
            const double hp = member->m_data->m_dHP;
            if (hp < 0.0) {
                member->m_data->SetHP(1.0);
            } else if (hp > 0.0) {
                member->IncreaseHP_diff(hpSum * member->m_data->m_dBaseHP / hpMaxSum - hp, true, 1, creature->m_nId, false);
            }
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            member, summon,
            // Recovered source combined the null/action conditions here:
            // if (summon == nullptr || !summon->IsAlive() || ...)
            // Current 0x00aa0968 enters "in_code" for every non-null item.
            if (!summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) { continue; } const double hp =
                summon->m_data->m_dHP;
            if (hp < 0.0) { summon->m_data->SetHP(1.0); } else if (hp > 0.0) {
                summon->IncreaseHP_diff(hpSum * summon->m_data->m_dBaseHP / hpMaxSum - hp, true, 1, creature->m_nId, false);
            }
        );
    }
    return true;
}

bool CSkillEffect_i_rebalance_mp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa19c8.
    L2_GUARD_FUNCTION();
    if (!target->IsPC() || creature == nullptr) {
        return false;
    }

    User* const targetUser = target->CastUser();
    CParty* const party = targetUser->GetParty();
    const std::uint32_t sourceId = creature->m_nId;
    if (party == nullptr) {
        if (!creature->IsPC()) {
            return false;
        }

        double mpMaxSum = creature->m_data->m_dBaseMP;
        double mpSum = creature->m_data->m_dMP;
        std::vector<CSummon*> summons;
        creature->CastUser()->GetAllSummonAndPet(summons);
        for (CSummon* const summon : summons) {
            // Recovered source combined the null/action conditions here:
            // if (summon == nullptr || !summon->IsAlive() || ...)
            // Current 0x00aa19c8 enters "in_code" for every non-null item.
            if (summon == nullptr) {
                continue;
            }
            GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
            if (!summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) {
                continue;
            }
            mpMaxSum += summon->m_data->m_dBaseMP;
            mpSum += summon->m_data->m_dMP;
        }

        creature->IncreaseMP_diff(
            mpSum * creature->m_data->m_dBaseMP / mpMaxSum - creature->m_data->m_dMP, 3, sourceId, false
        );
        for (CSummon* const summon : summons) {
            // Recovered source combined the null/action conditions here:
            // if (summon == nullptr || !summon->IsAlive() || ...)
            // Current 0x00aa19c8 enters "in_code" for every non-null item.
            if (summon == nullptr) {
                continue;
            }
            GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
            if (!summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) {
                continue;
            }
            summon->IncreaseMP_diff(
                mpSum * summon->m_data->m_dBaseMP / mpMaxSum - summon->m_data->m_dMP, 3, sourceId, false
            );
            summon->m_data->m_NeedUpdatePacketBitSet.ClearBit(0xc);
        }
        return true;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    double mpMaxSum = 0.0;
    double mpSum = 0.0;
    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }
        if (member->IsAlive() && IsInRebalanceRange(*creature, *member)) {
            mpMaxSum += member->m_data->m_dBaseMP;
            mpSum += member->m_data->m_dMP;
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            member, summon,
            // Recovered source evaluated all tests in one expression. Current
            // 0x00aa19c8 creates "in_code" immediately after the null test.
            // if (summon != nullptr && summon->IsAlive() && ...)
            if (summon->IsAlive() && IsInRebalanceRange(*creature, *summon)) {
                mpMaxSum += summon->m_data->m_dBaseMP;
                mpSum += summon->m_data->m_dMP;
            }
        );
    }

    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }
        if (member->IsAlive() && IsInRebalanceRange(*creature, *member)) {
            member->IncreaseMP_diff(
                mpSum * member->m_data->m_dBaseMP / mpMaxSum - member->m_data->m_dMP, 3, sourceId, false
            );
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            member, summon,
            // Recovered source combined the null/action conditions here:
            // if (summon == nullptr || !summon->IsAlive() || ...)
            // Current 0x00aa19c8 enters "in_code" for every non-null item.
            if (!summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) { continue; } summon->IncreaseMP_diff(
                mpSum * summon->m_data->m_dBaseMP / mpMaxSum - summon->m_data->m_dMP, 3, sourceId, false
            );
            summon->m_data->m_NeedUpdatePacketBitSet.ClearBit(0xc);
        );
    }
    return true;
}

bool CSkillEffect_i_rebalance_hp_summon::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa14ec.
    L2_GUARD_FUNCTION();
    if (!target->IsSummonOrPet() || creature == nullptr || !creature->IsPC()) {
        return false;
    }

    double hpMaxSum = creature->m_data->m_dBaseHP;
    double hpSum = creature->m_data->m_dHP;
    std::vector<CSummon*> summons;
    creature->CastUser()->GetAllSummonAndPet(summons);
    for (CSummon* const summon : summons) {
        if (summon == nullptr || !summon->IsAlive() || !IsInRebalanceRange(*creature, *summon)) {
            continue;
        }
        const double hp = summon->m_data->m_dHP;
        if (hp <= 0.0) {
            if (hp < 0.0) {
                summon->m_data->SetHP(1.0);
            }
            continue;
        }
        hpMaxSum += summon->m_data->m_dBaseHP;
        hpSum += hp;
        if (RebalanceHpTotalOverflow(
                // L"[Summon] HP over INT_MAX hpSum(%d)(%f) "
                // L"hpMaxSum(%d)(%f)!!!!.",
                L"Summon HP redistribution", hpSum, hpMaxSum
            )) {
            return false;
        }
    }

    creature->IncreaseHP_diff(
        hpSum * creature->m_data->m_dBaseHP / hpMaxSum - creature->m_data->m_dHP, true, 1, creature->m_nId, false
    );
    for (CSummon* const summon : summons) {
        if (summon != nullptr && summon->IsAlive() && IsInRebalanceRange(*creature, *summon)) {
            summon->IncreaseHP_diff(
                hpSum * summon->m_data->m_dBaseHP / hpMaxSum - summon->m_data->m_dHP, true, 1, creature->m_nId, false
            );
            summon->m_data->m_NeedUpdatePacketBitSet.ClearBit(11);
        }
    }
    return true;
}

bool CSkillEffect_i_death_link::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a8bd74.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }
    if (creature != nullptr && skillInfo != nullptr) {
        const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
        const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
        const double hpTerm = creature->m_data->m_dHP * 100.0 / creature->m_data->m_dBaseHP - 40.0;
        const double rawRatio = (100.0 - hpTerm * 2.0) / 100.0;
        const double ratio = rawRatio < 0.0 ? 0.0 : rawRatio;
        const double baseDamage =
            std::sqrt(attackerInfo.m_MagicPower) * m_dPower * 91.0 / targetInfo.m_MDefend * targetInfo.m_AttrResist * ratio +
            1.0;
        double damage = L2SkillFunc::CalcFinalDamage(
            *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
        );
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        L2SkillFunc::ApplyDamageToTarget(creature, target, damage, false);
    }
    return true;
}

bool CSkillEffect_i_death_link_mp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a96440.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
    const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
    const double hpTerm = creature->m_data->m_dHP * 100.0 / creature->m_data->m_dBaseHP - 40.0;
    const double rawRatio = (100.0 - hpTerm * 2.0) / 100.0;
    const double ratio = rawRatio < 0.0 ? 0.0 : rawRatio;
    const double baseDamage =
        std::sqrt(attackerInfo.m_MagicPower) * m_dPower * 91.0 / targetInfo.m_MDefend * targetInfo.m_AttrResist * ratio +
        1.0;
    const double damage = L2SkillFunc::CalcFinalDamage(
        *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
    );
    targetCreature->m_data->SetMP(targetCreature->m_data->m_dMP <= damage ? 0.0 : targetCreature->m_data->m_dMP - damage);
    if (targetCreature->IsPC()) {
        targetCreature->CastUser()->SendStatusUpdate(VCP_MP, 4, 0);
    }

    bool deleted = false;
    {
        L2_GUARD(targetCreature->m_InfectedSkillsSect);
        if (!targetCreature->m_SkillMod.m_bCanAct) {
            deleted = targetCreature->_DeleteAbnormalStatus(ABNS_SLEEP, nullptr, nullptr, true, SPET_DAMAGE, false);
            if (targetCreature->_DeleteAbnormalStatus(ABNS_FORCE_MEDITATION, nullptr, nullptr, true, SPET_DAMAGE, false)) {
                deleted = true;
            }
        }
        if (targetCreature->_DeleteAbnormalStatus(ABNS_CHANGEBODY, nullptr, nullptr, true, SPET_DAMAGE, true)) {
            deleted = true;
        }
    }

    if (deleted) {
        if (targetCreature->IsPC()) {
            targetCreature->CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            targetCreature->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }
        if (targetCreature->IsPC()) {
            targetCreature->m_bAbnormalStatusChanged = true;
            User* const user = targetCreature->CastUser();
            user->m_bSpelledInfoChanged = true;
            user->m_NeedUpdatePacketBits |= 2U;
        }
        targetCreature->CheckAbnormalVisualEffect();
    }
    return true;
}

bool CSkillEffect_i_soul_shot::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8ae28.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (!user->m_bWaitSoulShotUse) {
        return false;
    }
    user->m_bWaitSoulShotUse = false;

    const ::AttackType attackType = creature->GetAttackType();
    const bool dualWeapon =
        attackType == AT_DUAL || attackType == AT_DUALFIST || attackType == AT_DUALDAGGER || attackType == AT_DUALBLUNT;
    user->m_data->m_dSoulShotAttackBonusPer = (m_dBonus + (dualWeapon ? 4.0 : 0.0)) / 100.0;

    constexpr std::array<std::pair<int, int>, 21> soulShotLevels{{
        {0x07f7, 1},
        {0x0866, 2},
        {0x0867, 3},
        {0x0868, 4},
        {0x0869, 5},
        {0x086a, 6},
        {0x23e9, 7},
        {0x65cc, 2},
        {0x65cd, 3},
        {0x65ce, 4},
        {0x65cf, 5},
        {0x65d0, 6},
        {0x57ae, 7},
        {0x244d, 1},
        {0x244e, 2},
        {0x244f, 3},
        {0x2450, 4},
        {0x2451, 5},
        {0x2452, 6},
        {0x2453, 7},
        {-1, -1},
    }};
    for (const auto& [shotSkillId, shotLevel] : soulShotLevels) {
        if (shotSkillId == skillInfo->m_nSkillId || shotSkillId == -1) {
            user->m_data->m_nSoulShot = shotLevel;
            break;
        }
    }
    user->SendSystemMessage(SystemMessageId::SOULSHOT_POWER_ENABLED);
    return true;
}

bool CSkillEffect_i_summon_soul_shot::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9ea74.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    if (!creature->CastUser()->m_bWaitSummonSoulShotUse) {
        return false;
    }
    if (!creature->CastUser()->HaveSummon() && !creature->CastUser()->HavePet()) {
        creature->CastUser()->m_bWaitSummonSoulShotUse = false;
        creature->SendSystemMessage(SystemMessageId::NO_PET_OR_SERVITOR);
        return false;
    }

    bool applied = false;
    {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            creature->CastUser(), summon,
            // Recovered source combined these tests, but current 0x00a9ea74
            // enters "in_code" after the null test and before IsAlive().
            // if (summon == nullptr || !summon->IsAlive()) {
            if (!summon->IsAlive()) { continue; } applied = true;
            summon->m_data->m_dSoulShotAttackBonusPer = m_dBonus / 100.0; summon->m_data->m_nSoulShot = 1;
        );
    }

    if (!applied) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_USE_SHOTS_FOR_DEAD_PET_OR_SERVITOR);
        creature->CastUser()->m_bWaitSummonSoulShotUse = false;
        return false;
    }

    creature->CastUser()->m_bWaitSummonSoulShotUse = false;
    if (creature->CastUser()->HaveSummon()) {
        creature->SendSystemMessage(SystemMessageId::SERVITOR_USES_SOULSHOT_POWER);
    }
    if (creature->CastUser()->HavePet()) {
        creature->SendSystemMessage(SystemMessageId::PET_USES_SOULSHOT_POWER);
    }
    return true;
}

bool CSkillEffect_i_summon_spirit_shot::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9eddc.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    if (!creature->CastUser()->m_bWaitSummonSpiritShotUse) {
        return false;
    }
    if (!creature->CastUser()->HaveSummon() && !creature->CastUser()->HavePet()) {
        creature->SendSystemMessage(SystemMessageId::NO_PET_OR_SERVITOR);
        creature->CastUser()->m_bWaitSummonSpiritShotUse = false;
        return false;
    }

    bool applied = false;
    {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            creature->CastUser(), summon,
            // Recovered source used:
            // if (!summon->IsSummonOrPet()) {
            // Current 0x00a9eddc calls vslot +0x448, which is IsAlive(), before
            // applying spirit-shot state. Preserve the machine condition exactly.
            if (!summon->IsAlive()) { continue; } summon->m_data->m_dSpiritShotAttackBonusPer = m_dPowerBonus / 100.0;
            summon->m_data->m_dSpiritShotSpeedBonusPer = m_dSpeedBonus / 100.0;
            summon->m_dSpiritShotHealBonus = m_dMultiBonus; summon->m_data->m_bSpiritShot = true; applied = true;
        );
    }

    if (!applied) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_USE_SHOTS_FOR_DEAD_PET_OR_SERVITOR);
        creature->CastUser()->m_bWaitSummonSpiritShotUse = false;
        return false;
    }

    creature->CastUser()->m_bWaitSummonSpiritShotUse = false;
    if (creature->CastUser()->HaveSummon()) {
        creature->SendSystemMessage(SystemMessageId::SERVITOR_USES_SOULSHOT_POWER);
    } else if (creature->CastUser()->HavePet()) {
        creature->SendSystemMessage(SystemMessageId::PET_USES_SOULSHOT_POWER);
    }
    return true;
}

bool CSkillEffect_i_spirit_shot::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8b054.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (!user->m_bWaitSpiritShotUse) {
        return false;
    }
    user->m_bWaitSpiritShotUse = false;
    user->m_data->m_dSpiritShotAttackBonusPer = m_dPowerBonus / 100.0;
    user->m_data->m_dSpiritShotSpeedBonusPer = m_dSpeedBonus / 100.0;
    user->m_dSpiritShotHealBonus = m_dMultiBonus;
    user->m_data->m_bSpiritShot = true;
    user->SendSystemMessage(SystemMessageId::SPIRITSHOT_POWER_ENABLED);
    return true;
}

bool CSkillEffect_i_give_contribution::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8f128.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !target->IsCreature()) {
        return false;
    }
    target->CastCreature()->AccumulateContribute(creature, static_cast<double>(m_nBonus));
    return true;
}

bool CSkillEffect_i_randomize_hate::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8a8e0.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !target->IsNPC()) {
        return false;
    }

    CNPC* const npc = static_cast<CNPC*>(target);
    const double probability =
        L2SkillFunc::CalculateProbability(static_cast<double>(m_nProbability), creature, npc, skillInfo);
    if (probability <= genrand64_real1() * 100.0) {
        creature->SendSystemMessage(SystemMessageId::ENMITY_CHANGE_FAILED);
        return false;
    }
    g_npcserver.Send("cdd", 0x2b, static_cast<int>(npc->m_nId), npc->GetIndex());
    return true;
}

bool CSkillEffect_i_add_hate_mymaster::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9ae20.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || !target->IsNPC() || !creature->IsSummonedNpc()) {
        return false;
    }

    CCreature* const masterCreature =
        g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creature->m_data->m_pMaster.id));
    User* const master = masterCreature != nullptr && masterCreature->IsPC() ? masterCreature->CastUser() : nullptr;
    if (master != nullptr) {
        CNPC* const npc = static_cast<CNPC*>(target);
        g_npcserver.Send(
            "cdddddf", 0x20, static_cast<int>(npc->m_nId), npc->GetIndex(), creature->m_data->m_pMaster.id,
            creature->m_data->m_pMaster.index, m_nAmount, master->GetShieldDefense()
        );
    }
    return true;
}

bool CSkillEffect_i_summon_cubic::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e8bc.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->SummonCubic(m_cubicID, m_cubicLevel, creature == target)) {
        creature->SendSystemMessage(SystemMessageId::CUBIC_SUMMON_FAILED);
    }
    return true;
}

bool CSkillEffect_i_get_agro::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8c074.
    L2_GUARD_FUNCTION();
    if (!target->IsNPC() || !target->IsVisible()) {
        return false;
    }

    g_npcserver.Send(
        "cdddd", 0x92, static_cast<int>(target->Id()), target->GetIndex(), static_cast<int>(creature->Id()),
        creature->GetIndex()
    );
    return true;
}

bool CSkillEffect_i_change_vital_item_count::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a93490.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    int itemClassId = 0;
    int itemCount = 0;
    if (skillInfo != nullptr) {
        itemClassId = skillInfo->m_nConsumeItem;
        itemCount = static_cast<int>(skillInfo->m_nConsumeItemCount);
    }

    return target->CastUser()->DecVitalRestoreCount(nCount, itemClassId, itemCount);
}

bool CSkillEffect_i_acc_change::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a93598.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user != nullptr) {
        const bool enable = !user->m_bHairAccFlag;
        user->SetHairAccFlag(enable);
        user->SendSystemMessage(
            enable ? static_cast<unsigned int>(SystemMessageId::HAIR_ACCESSORY_DISPLAY_ENABLED)
                   : static_cast<unsigned int>(SystemMessageId::HAIR_ACCESSORY_DISPLAY_DISABLED)
        );
        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_SKILL});
        db.RequestUpdateHairAccFlag(*user);
    }
    return true;
}

bool CSKillEffetct_i_summon_territory::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa4af4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    CContainer* const container = CContainerDB::Inst().GetContainer(creature->GetContainerIndex());
    if (container == nullptr) {
        return false;
    }

    const FVector position = creature->GetPos();
    CTerritory* const territory = new CTerritory;
    const double radius = static_cast<double>(m_nRadius);
    const double radius86 = radius * 0.86;
    const double radiusHalf = radius * 0.5;
    const auto addPoint = [territory](double x, double y) {
        territory->AddTerritoryPoint(runtime::Win64CvttDoubleToInt32(x), runtime::Win64CvttDoubleToInt32(y));
    };

    addPoint(position.x, position.y + radius);
    addPoint(position.x + radius86, position.y + radiusHalf);
    addPoint(position.x + radiusHalf, position.y + radius86);
    addPoint(position.x + radius, position.y);
    addPoint(position.x + radiusHalf, position.y - radius86);
    addPoint(position.x + radius86, position.y - radiusHalf);
    addPoint(position.x, position.y - radius);
    addPoint(position.x - radius86, position.y - radiusHalf);
    addPoint(position.x - radiusHalf, position.y - radius86);
    addPoint(position.x - radius, position.y);
    addPoint(position.x - radiusHalf, position.y + radius86);
    addPoint(position.x - radius86, position.y + radiusHalf);

    const double collisionHeight = creature->GetCollisionHeight();
    territory->SetBottom(runtime::Win64CvttDoubleToInt32(position.z - collisionHeight * 2.0));
    territory->SetTop(runtime::Win64CvttDoubleToInt32(position.z + creature->GetCollisionHeight() * 2.0));

    L2_GUARD(g_DynamicTerritoryQueueLock);
    L2_GUARD(container->m_BlockTerritoryLock);

    const DynamicTerritoryInfo info{
        runtime::Int32FromBits(
            static_cast<std::uint32_t>(runtime::crt::Time32Now()) + static_cast<std::uint32_t>(m_nTime)
        ),
        creature->GetContainerIndex(), territory};
    g_DynamicTerritoryQueue.push(info);
    container->m_lBlockTerritoryList.push_back(territory);
    return true;
}

bool CSkillEffect_i_force_sitdown::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8ec8c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }

    User* const user = target->CastUser();
    user->DoNothing();
    user->BasicAction(STUA_SIT_STAND, true, true, STP_STAND);
    return true;
}

bool CSkillEffect_i_register_siege_golem::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8ed8c.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature() || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    CCastle* castle = pledge->GetCastleUnderSiege(user->m_nCurCastleId);
    if (castle != nullptr && pledge->GetCurrentCastleSiegeStatus(user->m_nCurCastleId) == 1) {
        castle->RegisterSiegeGolemSummoner(user);
        return true;
    }

    const int dominionId = user->m_data->m_nDominionSiegeId;
    if (dominionId != 0) {
        CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
        castle = dominion != nullptr ? dominion->m_pMainCastle : nullptr;
        if (castle != nullptr) {
            castle->RegisterSiegeGolemSummoner(user);
            return true;
        }
    }
    return false;
}

bool CSkillEffect_i_block_act::Instant(
    CCreature*, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00a91e04.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target) || (skillAction != nullptr && !skillAction->m_Infected)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature != nullptr && !targetCreature->CanBeBuffed(skillInfo, targetCreature)) {
        return false;
    }

    if (skillInfo->m_IsFlySkill) {
        L2SkillFunc::BlockAction(target->CastCreature(), false);
    } else {
        L2SkillFunc::BlockAction(target->CastCreature(), true);
    }
    return true;
}

bool CSkillEffect_i_summon_hallucination::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a91f24.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    User* const user = creature->CastUser();
    CCreature* const targetCreature = target->CastCreature();
    const FVector destination = targetCreature->GetMeleeAttackPosition(creature);
    if (!CWorld::Inst().IsValidPosition(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y)
        )) {
        return false;
    }

    user->BroadcastFlyToLocation(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), FTLT_HALLUCINATION, 0, 0, 0
    );
    CWorld::Inst().MoveTo(user, destination, false);
    user->SummonHallucination(m_nClassId + 1000000, m_nCount, skillInfo, NST_NOTHING, targetCreature);
    return true;
}

bool CSkillEffect_i_my_summon_kill::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a90fc8.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        creature->CastUser()->DespawnAllSummon(true, false);
    }
    return true;
}

bool CSkillEffect_i_suicide::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a92720.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    creature->Suicide(DAMAGE_SUICIDE_SKILL, 0);
    return true;
}

bool CSkillEffect_i_install_camp::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8a68c.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        creature->CastUser()->InstallBattleCamp(false);
    }
    return isPc;
}

bool CSkillEffect_i_install_advance_base::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8c618.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        creature->CastUser()->InstallAdvancedBattleCamp();
    }
    return isPc;
}

bool CSkillEffect_i_uninstall_advance_base::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8c6e0.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        creature->CastUser()->UninstallAdvancedBattleCamp();
    }
    return isPc;
}

bool CSkillEffect_i_remote_control_trap::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8d05c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }
    creature->CastUser()->ActivatePCTrap();
    return true;
}

bool CSkillEffect_i_get_ultimate_skill_gauge::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8d120.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    creature->UltimateSkillGaugeInc(m_Point);
    creature->SendStatusUpdate(VCP_ULTIMATE_SKILL_POINT, 0, 0);
    return true;
}

bool CSkillEffect_i_pledge_send_system_message::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a90798.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CPledge* const pledge = user == nullptr ? nullptr : user->GetPledge();
    if (pledge != nullptr) {
        pledge->SystemMessage(nMsgNum);
    }
    return true;
}

bool CSkillEffect_i_focus_soul::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8de6c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }
    target->CastUser()->IncreaseSoulCount(m_NumSoul, 0);
    return true;
}

bool CSkillEffect_i_remove_soul::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8df48.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }
    target->CastUser()->DecreaseSoulCount(m_NumSoul);
    return true;
}

bool CSkillEffect_i_compulsion_social_action::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8eb94.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target)) {
        return false;
    }

    target->CastCreature()->SocialAction(m_ActionId, 0x800, false);
    return true;
}

bool CSkillEffect_i_open_dwarf_recipebook::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a913e4.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        creature->CastUser()->SendRecipeBookList(0);
    }
    return isPc;
}

bool CSkillEffect_i_open_common_recipebook::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a914ac.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        creature->CastUser()->SendRecipeBookList(1);
    }
    return isPc;
}

CSkillEffect_i_abnormal_time_change::CSkillEffect_i_abnormal_time_change(
    char16_t* slotName,
    const DebuffType slotType,
    const double timeBonus,
    const SkillFxChangeType changeType,
    const int ignoreSpecialLevel
) :
    m_AbnormalType(ABNS_INVALID),
    dTimeBonus(timeBonus),
    sfct(changeType),
    m_SlotType(slotType),
    m_nIgnoreSpecialLevel(ignoreSpecialLevel) {
    // Current L2Server 0x00aa03d8.
    L2_GUARD_FUNCTION();
    m_AbnormalType = GetAbnormalStatusSlotTypeByName(slotName);
}

CSkillEffect_i_abnormal_time_change::CSkillEffect_i_abnormal_time_change(
    char16_t* slotName, const DebuffType slotType, const SkillFxChangeType changeType, const int ignoreSpecialLevel
) :
    m_AbnormalType(ABNS_INVALID),
    dTimeBonus(0.0),
    sfct(changeType),
    m_SlotType(slotType),
    m_nIgnoreSpecialLevel(ignoreSpecialLevel) {
    // Current L2Server 0x00aa04b8.
    L2_GUARD_FUNCTION();
    m_AbnormalType = GetAbnormalStatusSlotTypeByName(slotName);
}

bool CSkillEffect_i_abnormal_time_change::CheckSlotType(const CSkillInfo& skillInfo) const {
    // Current L2Server 0x00a8fba0.
    L2_GUARD_FUNCTION();
    return m_SlotType == DB_MAX ||
        (static_cast<std::uint32_t>(skillInfo.m_eDebuff) <= 1U && skillInfo.m_eDebuff == m_SlotType);
}

bool CSkillEffect_i_abnormal_time_change::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a97550.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    {
        L2_GUARD(targetCreature->m_InfectedSkillsSect);

        for (InfectedSkillType& infected : targetCreature->m_InfectedSkills) {
            CSkillInfo* const infectedSkill = infected.pSkillInfo;
            if (infectedSkill == nullptr || !CheckSlotType(*infectedSkill) ||
                m_nIgnoreSpecialLevel >= infectedSkill->m_nSpecialLevel ||
                (infectedSkill->m_nAbnormalType != m_AbnormalType && m_AbnormalType != ABNS_ALL)) {
                continue;
            }

            if (infectedSkill->m_nAbnormalTime == -1) {
                infected.nStartTime = -1;
            } else {
                std::int32_t now = 0;
                CIOObject::SetTimeStamp(&now);
                int newStartTime = now;
                if (sfct != SFCT_RESET) {
                    const double percentage = sfct == SFCT_PER ? dTimeBonus : 100.0;
                    const double difference = sfct == SFCT_DIFF ? dTimeBonus : 0.0;
                    const int oldStartTime = infected.nStartTime;
                    const int abnormalTime = target->CastCreature()->GetAbnormalTime(infectedSkill, false);
                    newStartTime = runtime::Win64CvttDoubleToInt32(
                        (static_cast<double>(abnormalTime + (oldStartTime - now)) * percentage) / 100.0 +
                        static_cast<double>(oldStartTime) + difference
                    );
                }
                infected.nStartTime = newStartTime;
            }

            if (m_AbnormalType != ABNS_ALL) {
                break;
            }
        }

        if (targetCreature->IsPC()) {
            targetCreature->CastUser()->GetSocket()->Send(
                "chccdd", 0x62, SystemMessageId::EFFECT_APPLIED, 1, 4, skillInfo->m_nSkillId,
                (skillInfo->m_nSubLevel << 16) | static_cast<std::uint16_t>(skillInfo->m_nLevel)
            );
        }
    }

    if (targetCreature->IsPC()) {
        targetCreature->m_bAbnormalStatusChanged = true;
    }
    if (targetCreature->IsPC()) {
        User* const targetUser = targetCreature->CastUser();
        targetUser->m_bSpelledInfoChanged = true;
        targetUser->m_NeedUpdatePacketBits |= 4U;
    } else {
        targetCreature->CheckAbnormalVisualEffect();
    }

    if (creature->IsPC() && creature->m_spTarget.GetCreature() == targetCreature) {
        targetCreature->SendAbnormalStatusToUserTargetsMe(*creature->CastUser());
    }
    return true;
}

CSkillEffect_i_dispel_by_slot_probability::CSkillEffect_i_dispel_by_slot_probability(
    char16_t* slotName, const int probability
) :
    m_nAbnormalType(ABNS_INVALID), nProb(probability) {
    // Current L2Server 0x00aa0594.
    L2_GUARD_FUNCTION();
    m_nAbnormalType = GetAbnormalStatusSlotTypeByName(slotName);
}

bool CSkillEffect_i_dispel_by_slot_probability::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8fd38.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    static_cast<CCreature*>(target)->DispelBySlotProb(m_nAbnormalType, creature, nProb, const_cast<CSkillInfo*>(skillInfo));
    return true;
}

CSkillEffect_i_set_skill::CSkillEffect_i_set_skill(const int skillId, const int skillLevel) :
    m_SkillId(skillId), m_SkillLevel(skillLevel) {}

bool CSkillEffect_i_set_skill::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a90eb8. Ctor 0x00aa2ddc.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (!user->AcquireSkill(m_SkillId, m_SkillLevel, 0, false, true)) {
        return false;
    }

    user->SendAcquiredSkillToDB(m_SkillId, m_SkillLevel, 0, true);
    user->ValidateChanged(VB_MAX);
    return true;
}

CSkillEffect_i_skill_turning::CSkillEffect_i_skill_turning(const int magicType, const int probability) :
    nProb(probability) {
    // Current L2Server 0x00aa4810.
    L2_GUARD_FUNCTION();
    m_MagicTypeList.push_back(magicType);
}

CSkillEffect_i_skill_turning::CSkillEffect_i_skill_turning(std::vector<std::int64_t>* magicTypes, const int probability) :
    nProb(probability) {
    // Current L2Server 0x00aa48dc.
    L2_GUARD_FUNCTION();
    for (const std::int64_t magicType : *magicTypes) {
        m_MagicTypeList.push_back(runtime::Int32FromBits(static_cast<std::uint32_t>(magicType)));
    }
}

bool CSkillEffect_i_skill_turning::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a9e098.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const caster = creature->CastUser();
    if (L2Common::IsAliveCreature(target)) {
        CCreature* const targetCreature = target->CastCreature();
        const double probability =
            L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, targetCreature, skillInfo);
        if (::genrand64_real1() * 100.0 < probability) {
            for (const int magicType : m_MagicTypeList) {
                targetCreature->CancelCastingSkill(magicType, creature->CastWorldObject());
            }
            return true;
        }
    }

    if (target == nullptr) {
        return false;
    }

    const int packedSkillLevel = (skillInfo->m_nSubLevel << 16) | static_cast<std::uint16_t>(skillInfo->m_nLevel);
    if (target->IsPC()) {
        User* const targetUser = target->CastUser();
        caster->GetSocket()->Send(
            "chccScdd", 0x62, SystemMessageId::CHARACTER_RESISTED_NAMED_EFFECT, 2, 0x0c, targetUser->GetName2(caster), 4,
            skillInfo->m_nSkillId, packedSkillLevel
        );
    } else if (target->IsNPC()) {
        caster->GetSocket()->Send(
            "chccdcdd", 0x62, SystemMessageId::CHARACTER_RESISTED_NAMED_EFFECT, 2, 2, target->CastNPC()->GetNPCId(), 4,
            skillInfo->m_nSkillId, packedSkillLevel
        );
    }
    return false;
}

bool CSkillEffect_i_dispel_all::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8bfa0.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    static_cast<CCreature*>(target)->DispelAll(skillInfo->m_nMagicLevel, creature, skillInfo);
    return true;
}

bool CSkillEffect_i_dispel_by_name::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a85660. Ctor 0x00ca3864.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    static_cast<CCreature*>(target)->DispelByNames(m_vDispelSkillName, creature, const_cast<CSkillInfo*>(skillInfo));
    return true;
}

CSkillEffect_i_dispel_by_slot_myself::CSkillEffect_i_dispel_by_slot_myself(char16_t* slotName) {
    // Current L2Server 0x00a9e9ac.
    L2_GUARD_FUNCTION();
    m_nAbnormalType = GetAbnormalStatusSlotTypeByName(slotName);
}

bool CSkillEffect_i_dispel_by_slot_myself::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a858f4.
    L2_GUARD_FUNCTION();
    creature->DispelBySlot(m_nAbnormalType, creature, -1, skillInfo, SPET_DISPEL, false);
    return true;
}

bool CSkillEffect_i_dispel_by_slot::InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00a85734.
    L2_GUARD_FUNCTION();
    return Instant(nullptr, target, skillInfo, nullptr, 0.0);
}

bool CSkillEffect_i_dispel_by_slot::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a85800.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    target->CastCreature()->DispelBySlot(m_nAbnormalType, creature, m_nLevel, skillInfo, SPET_DISPEL, false);
    return true;
}

bool CSkillEffect_i_capture_flag_start::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a90948.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsFortressFlagpole()) {
        return false;
    }

    CFortressFlagpole* const flagpole = target->CastFortressFlagpole();
    if (flagpole != nullptr && !flagpole->IsFlagSet()) {
        flagpole->CaptureInFortressSiegeStart(creature->CastUser());
    }
    return true;
}

bool CSkillEffect_i_capture_flag::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a90a34.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsFortressFlagpole()) {
        return false;
    }

    CFortressFlagpole* const flagpole = target->CastFortressFlagpole();
    if (flagpole != nullptr && !flagpole->IsFlagSet()) {
        flagpole->CapturedInFortressSiegeBy(creature->CastUser());
    }
    return true;
}

bool CSkillEffect_i_holything_possess::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8a5ac.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsHolyThing()) {
        return false;
    }

    CHolyThing* const holyThing = target->CastHolyThing();
    if (holyThing != nullptr) {
        holyThing->PossessedBy(creature, m_nIsGood);
    }
    return true;
}

bool CSkillEffect_i_bookmark_add_slot::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a91c28.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }
    return CUserBookmarkHandler::AddSlot(target->CastUser(), m_slotNum);
}

bool CSkillEffect_i_summon_agathion::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8e9c8.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || creature->m_data->m_nSlotIndex[ST_LBRACELET] == -1) {
        return false;
    }
    creature->CastUser()->SummonAgathion(m_agathionClassID, m_level);
    return true;
}

bool CSkillEffect_i_unsummon_agathion::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8eaac.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    if (!creature->CastUser()->HaveAgathion()) {
        return false;
    }
    creature->CastUser()->DespawnAgathion(2);
    return true;
}

bool CSkillEffect_i_bonuscount_up::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a911d0.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    user->AddBonusCount(m_BonusCount);
    user->CalExpBonus();
    user->SendVoteSystemInfoPacket();
    runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
    return true;
}

bool CSkillEffect_i_Locked_Item::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a87d9c.
    L2_GUARD_FUNCTION();
    CItem* const scrollItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_HLOCKED);
    if (target == nullptr || !target->IsItem() || scrollItem == nullptr) {
        return false;
    }
    scrollItem->hLockedItem(static_cast<CItem*>(target), static_cast<User*>(creature));
    return true;
}

bool CSkillEffect_i_UnLocked_Item::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a87e70.
    L2_GUARD_FUNCTION();
    CItem* const scrollItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_HLOCKED);
    if (target == nullptr || !target->IsItem() || scrollItem == nullptr) {
        return false;
    }
    scrollItem->hUnLockedItem(static_cast<CItem*>(target), static_cast<User*>(creature));
    return true;
}

bool CSkillEffect_i_change_attribute::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a89238.
    L2_GUARD_FUNCTION();
    CItem* const scrollItem = static_cast<User*>(creature)->GetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE);
    if (target == nullptr || !target->IsItem() || scrollItem == nullptr) {
        return false;
    }
    scrollItem->ChangeAttributeItem(static_cast<CItem*>(target), static_cast<User*>(creature));
    return true;
}

bool CSkillEffect_i_ev_ink::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a93f94.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = target->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->UpdateInkEnergy(nSkillInkEnergy, false);
    return true;
}

bool CSkillEffect_i_hp_down_self::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a89fcc.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }

    creature->IncreaseHP_per(static_cast<double>(m_Amount) / 100.0, true, 4, 0);
    return true;
}

bool CSkillEffect_i_hp_down::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a89ed0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    target->CastCreature()->IncreaseHP_per(static_cast<double>(m_Amount) / 100.0, true, 4, 0);
    return true;
}

bool CSkillEffect_i_hp_by_level_self::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8b514.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature()) {
        return false;
    }

    int nonNegativeDifference = 0;
    if (creature->m_data->m_nLevel - target->CastCreature()->m_data->m_nLevel >= 0) {
        nonNegativeDifference = creature->m_data->m_nLevel - target->CastCreature()->m_data->m_nLevel;
    }
    const int candidatePercent = (10 - nonNegativeDifference) * 10;
    int levelPercent = 0;
    if (candidatePercent >= 0) {
        nonNegativeDifference = 0;
        if (creature->m_data->m_nLevel - target->CastCreature()->m_data->m_nLevel >= 0) {
            nonNegativeDifference = creature->m_data->m_nLevel - target->CastCreature()->m_data->m_nLevel;
        }
        levelPercent = (10 - nonNegativeDifference) * 10;
    }
    creature->IncreaseHP_diff(m_Amount * levelPercent / 100, true, 1, creature->m_nId, false);
    return true;
}

bool CSkillEffect_i_focus_energy::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8aa50.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    int energyCount = 0;
    bool maximumReached = false;
    {
        L2_GUARD(*targetCreature->m_lock);
        if ((m_MaxEnergy == 0 || m_MaxEnergy <= targetCreature->m_nEnergyCount) &&
            targetCreature->m_SkillMod.m_MaxEnergyCount <= targetCreature->m_nEnergyCount) {
            maximumReached = true;
        } else {
            energyCount = ++targetCreature->m_nEnergyCount;
            if (energyCount == 1) {
                targetCreature->m_dwEnergyStartTime = runtime::GetTickCount();
            }
        }
    }

    if (maximumReached) {
        targetCreature->SendSystemMessage(SystemMessageId::FORCE_CANNOT_INCREASE_FURTHER);
        return false;
    }
    if (targetCreature->IsPC()) {
        targetCreature->SendSystemMessage_d(SystemMessageId::FORCE_LEVEL_INCREASED, energyCount);
        targetCreature->CastUser()->SendETCBuffStatusInfo();
    }
    return true;
}

bool CSkillEffect_i_focus_max_energy::Instant(CCreature*, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8ac5c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    bool maximumReached = false;
    {
        L2_GUARD(*targetCreature->m_lock);
        if (m_MaxEnergy == 0 || m_MaxEnergy <= targetCreature->m_nEnergyCount) {
            if (targetCreature->m_SkillMod.m_MaxEnergyCount <= targetCreature->m_nEnergyCount) {
                maximumReached = true;
            } else {
                if (targetCreature->m_nEnergyCount == 0) {
                    targetCreature->m_dwEnergyStartTime = runtime::GetTickCount();
                }
                targetCreature->m_nEnergyCount = targetCreature->m_SkillMod.m_MaxEnergyCount;
            }
        } else {
            if (targetCreature->m_nEnergyCount == 0) {
                targetCreature->m_dwEnergyStartTime = runtime::GetTickCount();
            }
            targetCreature->m_nEnergyCount = m_MaxEnergy;
        }
    }

    if (maximumReached) {
        return false;
    }
    if (targetCreature->IsPC()) {
        targetCreature->CastUser()->SendETCBuffStatusInfo();
    }
    return true;
}

bool CSkillEffect_i_remove_energy::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a90494.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    {
        L2_GUARD(*user->m_lock);
        if (user->m_nEnergyCount < m_nEnergy) {
            user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ENERGY);
        } else {
            user->m_nEnergyCount -= m_nEnergy;
        }
    }
    user->SendETCBuffStatusInfo();
    return true;
}

CSkillEffect_i_m_attack_mp::CSkillEffect_i_m_attack_mp(double power, int checkMaxMP, int baseMP) :
    m_dPower(power), m_nCheckMaxMP(checkMaxMP), m_nBaseMP(baseMP) {
    // Current L2Server 0x00ca49c4.
}

bool CSkillEffect_i_m_attack_mp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a98370.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target) || !L2SkillFunc::CheckIsMagicHit(creature, target) || skillInfo == nullptr) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!targetCreature->m_SkillMod.CanGetDamageMP(creature, targetCreature)) {
        return false;
    }

    const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
    const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
    double baseDamage =
        std::sqrt(attackerInfo.m_MagicPower) * m_dPower * 15.0 / targetInfo.m_MDefend * targetInfo.m_AttrResist;
    if (m_nCheckMaxMP == 1 && m_nBaseMP > 0) {
        baseDamage *= target->CastCreature()->m_data->m_dBaseMP / static_cast<double>(m_nBaseMP);
    }

    double damage = L2SkillFunc::CalcFinalDamage(
        *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
    );
    damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);

    targetCreature->m_data->SetMP(targetCreature->m_data->m_dMP <= damage ? 0.0 : targetCreature->m_data->m_dMP - damage);
    if (targetCreature->IsPC()) {
        targetCreature->CastUser()->SendStatusUpdate(VCP_MP, 4, 0);
    }

    bool deleted = false;
    {
        L2_GUARD(targetCreature->m_InfectedSkillsSect);
        if (!targetCreature->m_SkillMod.m_bCanAct) {
            deleted = targetCreature->_DeleteAbnormalStatus(ABNS_SLEEP, nullptr, nullptr, true, SPET_DAMAGE, false);
            if (targetCreature->_DeleteAbnormalStatus(ABNS_FORCE_MEDITATION, nullptr, nullptr, true, SPET_DAMAGE, false)) {
                deleted = true;
            }
        }
        if (targetCreature->_DeleteAbnormalStatus(ABNS_CHANGEBODY, nullptr, nullptr, true, SPET_DAMAGE, true)) {
            deleted = true;
        }
    }

    if (deleted) {
        if (targetCreature->IsPC()) {
            targetCreature->CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            targetCreature->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }
        if (targetCreature->IsPC()) {
            targetCreature->m_bAbnormalStatusChanged = true;
            User* const user = targetCreature->CastUser();
            user->m_bSpelledInfoChanged = true;
            user->m_NeedUpdatePacketBits |= 2U;
        }
        targetCreature->CheckAbnormalVisualEffect();
    }
    return true;
}

bool CSkillEffect_i_m_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a86518.
    L2_GUARD_FUNCTION();
    return L2SkillFunc::CalcAndApplyMagicAttackDamage(creature, target, skillInfo, distance, m_dPower, false, false, false);
}

bool CSkillEffect_i_m_attack_debuff_dmg::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a938b0.
    L2_GUARD_FUNCTION();
    double power = m_dBasePower;
    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature != nullptr && targetCreature->IsDebuffExist()) {
        L2SkillFunc::AddBonus(&power, &power, SFCT_PER, m_dPowerBonus);
    }
    return L2SkillFunc::CalcAndApplyMagicAttackDamage(creature, target, skillInfo, distance, power, false, false, false);
}

bool CSkillEffect_i_m_attack_by_abnormal_slot::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a865f4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target) ||
        !L2SkillFunc::CheckIsMagicHit(creature, target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    const int dispelled = targetCreature->DispelBySlot(m_SlotType, creature, -1, skillInfo, SPET_DISPEL, true);
    if (dispelled <= 0) {
        return false;
    }
    return L2SkillFunc::CalcAndApplyMagicAttackDamage(
        creature, target, skillInfo, distance, static_cast<double>(dispelled) * m_dPower, false, false, false
    );
}

bool CSkillEffect_i_m_soul_attack::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a86750.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }
    return L2SkillFunc::CalcAndApplyMagicAttackRangeDamage(
        creature, target, skillInfo, distance, m_dPower, m_dPer, false, true, m_ForceHit
    );
}

bool CSkillEffect_i_m_attack_by_hp::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a86858.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target) || !L2SkillFunc::CheckIsMagicHit(creature, target)) {
        return false;
    }
    if (creature != nullptr && skillInfo != nullptr) {
        const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
        const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
        const double hpRatio = creature->m_data->m_dHP / creature->m_data->m_dBaseHP;
        const double baseDamage = (std::sqrt(attackerInfo.m_MagicPower) * m_dPower * 91.0 / targetInfo.m_MDefend) *
            targetInfo.m_AttrResist * hpRatio;
        double damage = L2SkillFunc::CalcFinalDamage(
            *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
        );
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        L2SkillFunc::ApplyDamageToTarget(creature, target, damage, false);
    }
    return true;
}

bool CSkillEffect_i_m_attack_by_dist::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a86a38.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target) || !L2SkillFunc::CheckIsMagicHit(creature, target)) {
        return false;
    }
    if (creature != nullptr && skillInfo != nullptr) {
        const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
        const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
        const double rawRatio = std::sqrt(distance) / (static_cast<double>(skillInfo->m_nAffectRange) + 0.1);
        const double ratio = rawRatio <= 1.0 ? rawRatio : 1.0;
        const double baseDamage = (std::sqrt(attackerInfo.m_MagicPower) * m_dPower * 91.0 / targetInfo.m_MDefend) *
            targetInfo.m_AttrResist * (1.0 - ratio * 0.7);
        double damage = L2SkillFunc::CalcFinalDamage(
            *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
        );
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        L2SkillFunc::ApplyDamageToTarget(creature, target, damage, false);
    }
    return true;
}

bool CSkillEffect_i_m_attack_by_abnormal::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a8dc58.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2Common::IsAliveCreature(target) ||
        !L2SkillFunc::CheckIsMagicHit(creature, target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (creature != nullptr && skillInfo != nullptr) {
        const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
        const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
        const double abnormalFactor = static_cast<double>(targetCreature->GetAbnormalCount()) * 0.125 * 0.7 + 0.3;
        const double baseDamage = abnormalFactor *
            (std::sqrt(attackerInfo.m_MagicPower) * m_Power * 91.0 / targetInfo.m_MDefend) * targetInfo.m_AttrResist;
        double damage = L2SkillFunc::CalcFinalDamage(
            *creature, *target, skillInfo, attackerInfo, targetInfo, m_Power, distance, baseDamage, true
        );
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        L2SkillFunc::ApplyDamageToTarget(creature, target, damage, true);
    }
    return true;
}

bool CSkillEffect_i_m_attack_over_hit::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a8eee4.
    L2_GUARD_FUNCTION();
    return L2SkillFunc::CalcAndApplyMagicAttackDamage(creature, target, skillInfo, distance, m_dPower, false, true, false);
}

bool CSkillEffect_i_m_attack_range::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a905c0.
    L2_GUARD_FUNCTION();
    return L2SkillFunc::CalcAndApplyMagicAttackRangeDamage(
        creature, target, skillInfo, distance, m_dPower, m_dPer, false, false, m_ForceHit
    );
}

bool CSkillEffect_i_m_attack_over_hit_range::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a906ac.
    L2_GUARD_FUNCTION();
    return L2SkillFunc::CalcAndApplyMagicAttackRangeDamage(
        creature, target, skillInfo, distance, m_dPower, m_dPer, true, false, m_ForceHit
    );
}

bool CSkillEffect_i_m_attack_link::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double distance
) {
    // Current L2Server 0x00a94bf0.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::IsAttackable(target)) {
        return false;
    }
    if (!L2SkillFunc::CheckIsMagicHit(creature, target)) {
        if (creature != nullptr) {
            CCreature::SendMagicAttckInfo(creature, target->CastCreature(), MagicAttackInfo::Avoid);
        }
        return false;
    }

    if (creature != nullptr && skillInfo != nullptr) {
        double adjustedPower = m_dPower;
        if (m_Sfct == SFCT_PER) {
            double base = (static_cast<double>(m_Bonus) + 100.0) / 100.0;
            const int chainCount = creature->m_nChainAttackCount;
            std::uint32_t exponent = static_cast<std::uint32_t>(chainCount);
            if (chainCount < 0) {
                exponent = 0U - exponent;
            }
            double factor = 1.0;
            for (;;) {
                if ((exponent & 1U) != 0U) {
                    factor *= base;
                }
                exponent >>= 1U;
                if (exponent == 0U) {
                    break;
                }
                base *= base;
            }
            if (chainCount < 0) {
                factor = 1.0 / factor;
            }
            adjustedPower *= factor;
        }

        const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*creature);
        const L2SkillFunc::MagicTargetInfo targetInfo = L2SkillFunc::GetMagicTargetInfo(*creature, *target, skillInfo);
        const double baseDamage =
            (std::sqrt(attackerInfo.m_MagicPower) * adjustedPower * 91.0 / targetInfo.m_MDefend) * targetInfo.m_AttrResist;
        double damage = L2SkillFunc::CalcFinalDamage(
            *creature, *target, skillInfo, attackerInfo, targetInfo, m_dPower, distance, baseDamage, true
        );
        damage = L2SkillFunc::ApplyBonusToAttackDamage(damage, *creature, *skillInfo);
        L2SkillFunc::ApplyDamageToTarget(creature, target, damage, false);
    }
    return true;
}

bool CSkillEffect_i_m_attack_by_range::Instant(CCreature*, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a8e83c: the shipped handler is a guarded
    // success-only body and does not consume either constructor field.
    L2_GUARD_FUNCTION();
    return true;
}

bool CSkillEffect_i_fishing_cast::Instant(CCreature*, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a91578.
    L2_GUARD_FUNCTION();
    return true;
}

bool CSkillEffect_i_fishing_pumping::Instant(CCreature*, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a915f8: this disabled handler returns false.
    L2_GUARD_FUNCTION();
    return false;
}

bool CSkillEffect_i_fishing_reeling::Instant(CCreature*, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a91678: this disabled handler returns false.
    L2_GUARD_FUNCTION();
    return false;
}

bool CSkillEffect_i_fishing_shot::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a916f8.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    if (!creature->CastUser()->m_bWaitFishingShotUse) {
        return false;
    }

    creature->CastUser()->m_bWaitFishingShotUse = false;
    creature->CastUser()->m_data->m_nFishingShot = 1;
    creature->CastUser()->m_pFishingController->m_dFishingShotBonus = dBonus;
    return true;
}

bool CSkillEffect_i_defuse_trap::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e124.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveNPC(target) || !target->IsTrap()) {
        return false;
    }

    CTrap* const trap = target->CastTrap();
    if (trap->Level() <= m_TrapLevel) {
        trap->Defuse(creature);
    }
    return true;
}

bool CSkillEffect_i_detect_trap::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8e020.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveNPC(target) || !target->IsTrap()) {
        return false;
    }

    CTrap* const trap = target->CastTrap();
    if (trap->Level() <= m_TrapLevel) {
        trap->OnDetected(creature);
    }
    return true;
}

bool CSkillEffect_i_align_direction::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aa0660.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const aligned = target->CastCreature();
    if (aligned->IsBoss() || aligned->IsZzoldagu()) {
        return false;
    }

    const double probability = L2SkillFunc::CalculateProbability(static_cast<double>(nProb), creature, aligned, skillInfo);
    if (genrand64_real1() * 100.0 >= probability) {
        return true;
    }

    FAngle direction = aligned->GetDir();
    direction.Yaw &= 0xffff;
    aligned->StopMove();
    FVector position = aligned->GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        aligned, 0x5000, position, 0x800, "cdddd", 0x7a, static_cast<int>(aligned->m_nId), direction.Yaw, 1, 0xffff
    );

    const FVector sourcePosition = creature->GetPos();
    position = aligned->GetPos();
    direction.Yaw = runtime::Win64CvttDoubleToInt32(
        std::atan2(position.y - sourcePosition.y, position.x - sourcePosition.x) * (65535.0 / 6.283185307179586)
    );
    aligned->m_data->SetDir(direction);

    direction = aligned->GetDir();
    direction.Yaw &= 0xffff;
    position = aligned->GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        aligned, 0x5000, position, 0x800, "cddd", 0x61, static_cast<int>(aligned->m_nId), direction.Yaw, 0xffff
    );

    if (aligned->IsPC()) {
        aligned->CastUser()->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    } else {
        aligned->SendMyInfo();
    }
    return true;
}

bool CSkillEffect_i_spoil::CheckCond(CCreature* creature, CNPC* npc) {
    // Current L2Server 0x00a8b8a4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || npc == nullptr) {
        return false;
    }

    const int npcClass = npc->m_data->m_nClass;
    if (npcClass == 0x51 || npcClass == 0x52 || npc->m_data->m_bIsPlunded) {
        creature->SendSystemMessage(SystemMessageId::TARGET_CANNOT_BE_SPOILED);
        return false;
    }
    if (!npc->m_data->m_bIsSpoil) {
        return true;
    }
    creature->SendSystemMessage(SystemMessageId::TARGET_ALREADY_SPOILED);
    return false;
}

bool CSkillEffect_i_spoil::SetSpoilUserInfoToNPC(CCreature* creature, CNPC* npc) {
    // Current L2Server 0x00a8b9a0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || npc == nullptr) {
        return false;
    }

    {
        L2_GUARD(*npc->m_lock);
        npc->m_data->m_bIsSpoil = true;
        npc->m_data->m_nCanSweeper = 2;
        npc->m_data->m_nSpoilerSmartID = static_cast<int>(creature->GetSmartID().Sid());
        npc->m_data->m_nCorpseTime += 10;
    }
    return true;
}

void CSkillEffect_i_spoil::MakeDropList(User* user, CNPC* npc) {
    // Current L2Server 0x00aab884.
    L2_GUARD_FUNCTION();
    if (user == nullptr || npc == nullptr) {
        return;
    }

    double privateDropChance = 1.0;
    if (user->m_SkillDurationMod.bItemChanceActivated) {
        privateDropChance = npc->GetPrivateDropCorpseItemChanceProb(user);
    }

    const int userLevel = user->m_data->m_nLevel;
    double eventRate = 1.0;
    std::vector<CEvent*> events;
    Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
    for (CEvent* const event : events) {
        if (event != nullptr && event->InEventTime()) {
            eventRate = static_cast<CEvent_EconomyControlEvent*>(event)->GetSpoilDropRate(userLevel);
            break;
        }
    }

    for (const ItemDropStruct& drop : npc->m_pClassConst->m_CorpseMakeList) {
        double probability = user->GetSpoilBonusPer() * g_dSpoilItemDropRatePer * drop.DropProb * eventRate *
            (user->GetUserPremiumSpoilBonus() + 1.0);
        CObject* const object = g_ObjectDB.GetObjectW(drop.ItemClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item != nullptr && item->m_const->m_bIsItemChanceAffected) {
            probability *= privateDropChance;
        }
        if (genrand64_real1() * 100.0 > probability) {
            continue;
        }

        const int amount = static_cast<int>(
            genrand64_real1() * static_cast<double>(drop.DropMax - drop.DropMin) + static_cast<double>(drop.DropMin) + 0.5
        );
        if (amount <= 0) {
            continue;
        }

        {
            L2_GUARD(*npc->m_lock);
            const auto found = npc->m_CorpseDropList.find(drop.ItemClassId);
            if (found == npc->m_CorpseDropList.end()) {
                npc->m_CorpseDropList.emplace(drop.ItemClassId, amount);
                npc->m_data->m_bItemCollectable = true;
            } else {
                found->second += amount;
            }
        }
    }
}

bool CSkillEffect_i_spoil::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00aac05c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC() || !L2Common::IsAliveNPC(target)) {
        return false;
    }

    CNPC* const npc = target->CastNPC();
    if (!CheckCond(creature, npc)) {
        return false;
    }

    int levelDifference = 0;
    if (npc->Level() - skillInfo->m_nMagicLevel > 0) {
        levelDifference = npc->Level() - skillInfo->m_nMagicLevel;
    }
    int chance = 5;
    if ((20 - levelDifference) * 5 > 4) {
        levelDifference = 0;
        if (npc->Level() - skillInfo->m_nMagicLevel > 0) {
            levelDifference = npc->Level() - skillInfo->m_nMagicLevel;
        }
        chance = (20 - levelDifference) * 5;
    }
    if (genrand64_real1() * 100.0 < static_cast<double>(chance)) {
        SetSpoilUserInfoToNPC(creature, npc);

        User* const user = creature->CastUser();
        float dropGate = 100.0F;
        const int userDifference = npc->m_data->m_nLevel - user->m_data->m_nLevel;
        if (userDifference < -5) {
            dropGate = userDifference < -10 ? 10.0F : static_cast<float>(userDifference) * 18.0F + 190.0F;
        }
        if (static_cast<float>(std::rand() % 100) <= dropGate) {
            MakeDropList(creature->CastUser(), npc);
        }
        creature->SendSystemMessage(SystemMessageId::TARGET_SPOILED);
    }
    return true;
}

bool CSkillEffect_i_collecting::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double
) {
    // Current L2Server 0x00aa6700.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    if (!target->IsCollection() || target->CastNPC()->m_data->m_nLevel > 3) {
        User* const user = creature->CastUser();
        user->SendSystemMessage(SystemMessageId::INVALID_TARGET);
        return false;
    }

    User* const user = creature->CastUser();
    CCollection* const collection = target->CastCollection();
    if (!collection->SetBeingCollected()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_GATHER_ANOTHER_PLAYER_ALREADY_GATHERING);
        return false;
    }

    static constexpr int kCollectProbability[9][3] = {
        {40, 0, 0},
        {60, 0, 0},
        {80, 0, 0},
        {90, 40, 0},
        {90, 60, 0},
        {90, 80, 0},
        {90, 90, 40},
        {90, 90, 60},
        {90, 90, 80},
    };

    const int skillId = skillAction != nullptr ? skillAction->GetSkillId() : skillInfo->m_nSkillId;
    bool success = false;

    const int skillIndex = m_nSkillLev - 1;
    const int npcIndex = collection->m_data->m_nLevel - 1;
    const int chance =
        skillIndex >= 0 && skillIndex < 9 && npcIndex >= 0 && npcIndex < 3 ? kCollectProbability[skillIndex][npcIndex] : 0;
    if (chance < static_cast<int>(genrand64_real2() * 101.0)) {
        user->SendSystemMessage(SystemMessageId::GATHERING_FAILED);
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1ac, user, nullptr, skillId, static_cast<std::uint16_t>(m_nSkillLev), target->CastNPC()->GetClassId(), 0, 0, 0,
            0
        );
    } else {
        collection->DropItemToInventory(100.0);

        std::array<ItemManip, 100> requests{};
        int requestCount = 0;
        {
            L2_WRITE_GUARD(collection->m_inv);
            for (CSharedItemData* itemData = collection->m_inv.GetFirst(); itemData != nullptr && requestCount < 100;
                itemData = itemData->Next()) {
                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
                if (!item || !item->IsItem()) {
                    continue;
                }
                requests[requestCount] =
                    ItemManip(0, itemData->m_nClassID, itemData->m_nCount, itemData->GetEnchanted(), 0, 0);
                ++requestCount;
            }
        }

        L2_GUARD(user->m_ManipulateQueueLock);
        CPacketHelperEx<20480> packet;
        User::etcMsg message = user->SaveEtcMsg(
            false, false, skillId, m_nSkillLev, target->CastNPC()->GetClassId(), std::u16string(), std::u16string(), 0
        );
        User::ManipulateQueue queue = user->SaveManipulateQueue(
            ManipulateType::MANI_SKILL_EFFECT_INSTANT, 0, requestCount, requests.data(), nullptr, 0, packet, false,
            ItemGetReason::Etc, message
        );

        const User::ManiItemResultType result = user->_SetManiItemJob(queue);
        if (result == User::MRT_JOBMAP) {
            user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
            collection->ResetBeingCollected();
            return false;
        }
        if (result == User::MRT_JOBNOTMAP) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1753)
            // Noise: MRT_JOBNOTMAP means the request was queued behind an active item operation. Normal waiting is not an
            // error. L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1753)
            return true;
        }
        if (result != User::MRT_NOJOB) {
            collection->ResetBeingCollected();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1759)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
            //     L"SkillFxFunc_instant.cpp",
            //     0x1759)
            L2_LOG_ERROR(
                Log,
                "Collection item changes could not be scheduled: unexpected queue result %d; the collection-in-progress "
                "flag has been reset and the effect fails.",
                static_cast<int>(result)
            );
            return false;
        }
        if (!user->_HandleItemManipulation(queue)) {
            collection->ResetBeingCollected();
            return false;
        }

        user->SendSystemMessage(SystemMessageId::GATHERING_SUCCEEDED);
        success = true;
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1ac, user, nullptr, skillId, static_cast<std::uint16_t>(m_nSkillLev), target->CastNPC()->GetClassId(), 1, 0, 0,
            0
        );
    }

    g_npcserver.Send("cddd", 0x90, static_cast<int>(collection->m_nId), static_cast<int>(user->m_nId), success ? 1 : 0);
    collection->Despawn(false, true);
    return true;
}

bool CSkillEffect_i_capture_ownthing::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9afd0.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    COwnThing* const ownThing = user->m_pOwnThing;
    if (ownThing == nullptr || !target->IsAdvanceBase()) {
        return false;
    }

    CDominion* const dominion = g_ResidenceDB.GetDominion(user->m_data->m_nDominionSiegeId);
    if (dominion == nullptr) {
        return false;
    }
    CPledge* const owner = dominion->GetResidenceOwnerPledge();
    if (owner == nullptr) {
        return false;
    }

    bool matchingBattleCamp = false;
    {
        const CSPointer<CBattleCamp> battleCamp = owner->GetBattleCamp();
        matchingBattleCamp = battleCamp && target->GetDBID() == battleCamp->GetDBID();
    }
    if (!matchingBattleCamp) {
        return false;
    }

    if (user->GetPledge() != nullptr) {
        const int dominionId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7ea, user, nullptr, user->GetPledge()->GetPledgeName(), nullptr, nullptr,
            static_cast<int>(user->m_data->m_nPledgeID), ownThing->GetOriginalResidenceID(),
            ownThing->GetOwningResidenceID(), dominionId, 0, 0, 0
        );
        ownThing->Regenerate(dominion, target->GetPos());
    }
    return true;
}

bool CSkillEffect_i_summon_target_to_me::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9d0c0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !L2SkillFunc::IsAttackable(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }

    FVector destination{};
    FAngle direction{};
    if (!creature->GetAttackFrontPos(targetCreature, destination, direction)) {
        creature->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        return false;
    }

    if (targetCreature->IsPC() && targetCreature->m_data->m_StopMode == STP_STORE) {
        targetCreature->StopStore(true, false);
    }
    if (targetCreature->m_data->m_StopMode == STP_SIT) {
        targetCreature->GetController()->Stand(0);
    }
    targetCreature->DoNothing();
    // targetCreature->ActionFailed(0)
    targetCreature->ActionFailed(AF_DEFAULT);

    if (!CWorld::Inst().MoveTo(targetCreature, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in "
        //     L"i_summon_target_to_me")
        // L2_LOG_ERROR(Log,
        //     L"CWorld::Inst().MoveTo Failed in "
        //     L"i_summon_target_to_me")
        L2_LOG_ERROR(
            Log,
            "i_summon_target_to_me could not move object %u to (%.17g, %.17g, %.17g). The effect still reports success.",
            static_cast<unsigned>(targetCreature->Id()), destination.x, destination.y, destination.z
        );
    } else {
        targetCreature->SetDir(direction);
        targetCreature->BroadcastFlyToLocation(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
        );
        targetCreature->ValidateLocation();
    }
    return true;
}

bool CSkillEffect_i_harvesting::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a9fcf4.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || !L2Common::IsDeadCreature(target)) {
        return false;
    }

    User* const user = creature->CastUser();
    CNPC* const npc = target->CastNPC();
    if (user == nullptr || npc == nullptr) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    const int harvestMode = [&]() -> int {
        L2_GUARD(*npc->m_lock);
        const int harvestMode = npc->m_data->m_nCanHarvest;
        return harvestMode;
    }();

    const std::uint32_t userSid = user->GetSmartID().Sid();
    const std::uint32_t sowerSid = static_cast<std::uint32_t>(npc->m_data->m_nSowerSmartID);
    if (harvestMode == 1) {
        if (sowerSid != userSid) {
            user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
            return false;
        }
    } else if (harvestMode == 2 && sowerSid != userSid) {
        CCreature* const sower = g_CreatureSmartIdManager.GetObjectW(sowerSid);
        if (sower == nullptr || !sower->IsPC()) {
            user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
            return false;
        }
        User* const sowerUser = sower->CastUser();
        CParty* const sowerParty = sowerUser->GetParty();
        if (sowerParty == nullptr || !sowerParty->IsMember(userSid)) {
            user->SendSystemMessage(SystemMessageId::NO_HARVEST_PRIORITY);
            return false;
        }
    }

    if (npc->m_data->m_bIsSow) {
        user->SendSystemMessage(SystemMessageId::HARVEST_FAILED_TARGET_NOT_SEEDED);
        return true;
    }

    int levelDifference = runtime::Int32FromBits(
        static_cast<std::uint32_t>(npc->m_data->m_nLevel) - static_cast<std::uint32_t>(user->m_data->m_nLevel)
    );
    if (levelDifference < 1) {
        levelDifference = runtime::NegateInt32Wrapping(levelDifference);
    }
    const int failureChance = runtime::Int32FromBits(static_cast<std::uint32_t>(levelDifference) * 5U - 25U);
    if (levelDifference > 5 && genrand64_real1() * 100.0 + 0.0 < static_cast<double>(failureChance)) {
        user->SendSystemMessage(SystemMessageId::HARVEST_FAILED);
        npc->m_SeedDropList.clear();
        return false;
    }

    for (const auto& [itemClassId, amount] : npc->m_SeedDropList) {
        user->AddItemToInventory(
            itemClassId, static_cast<std::int64_t>(amount), ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
        );

        CLogFormatter formatter;
        formatter.WriteLog2_1(0x105, user, nullptr, itemClassId, amount, npc->m_data->m_nClassID, 0, 0, 0, 0);

        CParty* const party = user->GetParty();
        if (party == nullptr) {
            continue;
        }
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            if (member->m_nId == creature->m_nId) {
                continue;
            }
            CUserSocket* const socket = member->GetSocket();
            if (amount < 2) {
                const char16_t* const actorName = creature->CastUser()->GetName2(member);
                socket->Send("chccScd", 0x62, SystemMessageId::CHARACTER_HARVESTED_ITEM, 2, 0x0c, actorName, 3, itemClassId);
            } else {
                const char16_t* const actorName = creature->CastUser()->GetName2(member);
                socket->Send(
                    "chccScdcd", 0x62, SystemMessageId::CHARACTER_HARVESTED_MULTIPLE_ITEMS, 3, 0x0c, actorName, 3,
                    itemClassId, 1, amount
                );
            }
        }
    }
    npc->m_SeedDropList.clear();
    return true;
}

bool CSkillEffect_i_capture_ownthing_start::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8c7a8.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    if (creature->CastUser()->m_pOwnThing == nullptr) {
        return false;
    }
    return g_ResidenceDB.GetDominion(creature->CastUser()->m_data->m_nDominionSiegeId) != nullptr;
}

bool CSkillEffect_i_transfer_abnormal::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a8cf60.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }
    creature->CastUser()->TransInfectedSkills(target->CastUser(), m_nAbnormalType, m_MaxSlot);
    return true;
}

bool CSkillEffect_i_install_camp_ex::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a9018c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    CSharedCreatureData* const data = user->m_data;
    if (data->m_nPState != 1 && data->m_nPState != 2 && data->m_nWorldId == g_nWorldId) {
        return false;
    }
    user->InstallBattleCamp(true);
    return true;
}

bool CSkillEffect_i_set_return_pos::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00aa02c8.
    L2_GUARD_FUNCTION();
    if (!target->IsCreature() || !creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetReturnValue(nSkillID, runtime::crt::Time32Now() + nDuration);
    return true;
}

bool CSkillEffect_i_pcbang_point_up::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a89dc0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr || !L2Common::IsAliveCreature(target) || !target->IsPC()) {
        return false;
    }
    User* const user = target->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->UpdatePoint(nAmount, PT_PCCAFE_POINT, DJWP_ADD, 0);
    return true;
}

bool CSkillEffect_i_steal_abnormal::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a8db44.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAlivePC(target)) {
        return false;
    }
    creature->CastUser()->StealInfectedSkills(
        target->CastUser(), static_cast<int>(m_SlotGroup), m_MaxSlot, m_Prob, skillInfo
    );
    return true;
}

bool CSkillEffect_i_event_agathion_reuse_delay::Instant(
    CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a96820.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    const double firstDelay = user->GetRemainReuseDelaySec(0x17e9, 1, 0);
    SkillReuseHelper::SendSystemMsg(
        *user, static_cast<int>(firstDelay), SystemMessageId::FIRST_AGATHION_GIFT_REUSE_HOURS_MINUTES_SECONDS, 0, 0
    );
    const double secondDelay = user->GetRemainReuseDelaySec(0x17ea, 1, 0);
    SkillReuseHelper::SendSystemMsg(
        *user, static_cast<int>(secondDelay), SystemMessageId::SECOND_AGATHION_GIFT_REUSE_HOURS_MINUTES_SECONDS, 0, 0
    );
    return true;
}

bool CSkillEffect_i_refuel_airship::Instant(CCreature* creature, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a9c890.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !creature->IsInAirShip()) {
        return false;
    }
    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(creature->GetMyAirShipID());
    if (airShip == nullptr || !airShip->CanRefuel(m_Amount)) {
        return false;
    }
    airShip->Refuel(m_Amount, creature->CastUser());
    return true;
}

bool CSkillEffect_i_consume_summon::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double
) {
    // Current L2Server 0x00a924b4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature) || !L2Common::IsAliveCreature(target) || !target->IsSummon()) {
        return false;
    }

    CSummon* const summon = target->CastSummon();
    User* const user = creature->CastUser();
    if (summon == nullptr || summon->m_data->m_bIsAlive == 0 || user == nullptr || user->m_nId != summon->m_MasterUserSID) {
        return false;
    }
    user->DespawnSummon(summon, false, false);
    return true;
}

bool CSkillEffect_i_blink_return::Instant(
    CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2*, double
) {
    // Current L2Server 0x00a9e570.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature) || !target->IsPC() || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CCreature* const targetCreature = target->CastUser();
    if (user == nullptr || targetCreature == nullptr) {
        return false;
    }

    FVector returnPosition = user->FindReturnValue(skillInfo->m_nSkillId);
    if (FVector{}.IsEqual(returnPosition)) {
        return false;
    }

    const FVector targetPosition = targetCreature->GetPos();
    const double x = returnPosition.x - targetPosition.x;
    const double y = returnPosition.y - targetPosition.y;
    const double z = returnPosition.z - targetPosition.z;
    if (std::sqrt(x * x + y * y + z * z) > static_cast<double>(m_Dist)) {
        return false;
    }
    return targetCreature->BlinkReturnPos(returnPosition);
}

CSkillEffect_i_unlock::CSkillEffect_i_unlock(
    bool useItem, int levelOneProbability, int levelTwoProbability, int levelThreeProbability
) :
    SuccessProb{}, m_bUseItem(useItem) {
    // Current L2Server 0x00aa4f2c.
    SuccessProb[1] = levelOneProbability;
    SuccessProb[2] = levelTwoProbability;
    SuccessProb[3] = levelThreeProbability;
}

bool CSkillEffect_i_unlock::Instant(CCreature* creature, CWorldObject* target, const CSkillInfo*, CSkillAction2*, double) {
    // Current L2Server 0x00a95508.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC() || (target->IsNPC() && static_cast<int>(target->CastNPC()->m_data->m_nClass) == 0x53) ||
        !target->IsDoor()) {
        return false;
    }

    CDoor* door = target->CastDoor();
    if (door->m_eType == DOORT_CHILD) {
        door = door->m_pParent;
        if (door == nullptr) {
            // Log.Add(LOG_ERROR, L"parent door is null")
            // L2_LOG_ERROR(Log, L"parent door is null")
            L2_LOG_ERROR(
                Log, "Cannot unlock child door object %u: its parent door is unavailable.",
                static_cast<unsigned>(target->Id())
            );
            return false;
        }
    }

    const DoorOpenMethodEnum method = door->m_eDoorOpenMethod;
    const int doorLevel = door->m_data->m_cLevel;
    const bool validMethod = method == DOOROPEN_SKILL || method == DOOROPEN_TIME || method == DOOROPEN_TIME_SKILL ||
        method == DOOROPEN_SKILL_ITEM || method == DOOROPEN_ITEM;
    if (!validMethod || doorLevel > 6) {
        creature->SendSystemMessage(SystemMessageId::DOOR_CANNOT_BE_OPENED);
        return false;
    }

    bool succeeded = false;
    if (m_bUseItem && (method == DOOROPEN_TIME_SKILL || method == DOOROPEN_SKILL_ITEM || method == DOOROPEN_ITEM)) {
        const std::u16string_view doorName(door->m_sDoorName);
        for (std::size_t index = 0; index < m_vDoorNames.size(); ++index) {
            if (runtime::StringView(runtime::CheckedVectorAt(m_vDoorNames, index)) == doorName && index < m_vProbs.size()) {
                const int roll = static_cast<int>(genrand64_real2() * 100.0 + 1.0);
                succeeded = static_cast<double>(roll) <=
                    creature->DexBonusF() * static_cast<double>(runtime::CheckedVectorAt(m_vProbs, index));
            }
        }
    } else if (!m_bUseItem && (method == DOOROPEN_SKILL || method == DOOROPEN_TIME_SKILL || method == DOOROPEN_SKILL_ITEM)) {
        succeeded = doorLevel == 0;
        if (doorLevel != 0) {
            const int roll = static_cast<int>(genrand64_real2() * 100.0 + 1.0);
            succeeded = static_cast<double>(roll) <= creature->DexBonusF() * static_cast<double>(SuccessProb[doorLevel]);
        }
    }

    if (succeeded) {
        door->Open(false, false);
    } else {
        creature->SendSystemMessage(SystemMessageId::DOOR_UNLOCK_FAILED);
    }
    return true;
}
