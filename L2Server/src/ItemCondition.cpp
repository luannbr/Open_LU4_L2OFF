#include "ItemCondition.h"
#include "Agit.h"
#include "Area.h"
#include "CWorld.h"
#include "Castle.h"
#include "Creature.h"
#include "Fortress.h"
#include "InstantAgit.h"
#include "KalieEvent.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "User.h"

#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/ResidenceData.h"

#include <algorithm>

namespace {

template <typename T>
bool Contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

User* CastPC(CCreature* creature) { return creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr; }

int ClassIdForCondition(CCreature* creature) {
    return creature->IsPC() ? static_cast<int>(creature->m_data->m_nClass) : creature->m_data->m_nClassID;
}

void AppendScalar(std::vector<int>& conditions, int value) { conditions.push_back(value); }

} // namespace

CItemEquipCondition::~CItemEquipCondition() = default;

bool CItemEquipCondition::CheckCommon(CCreature* creature, CItem* item) {
    L2_GUARD_FUNCTION();
    return creature != nullptr && item != nullptr;
}

bool CItemEquipCondition::CanEquip(CCreature* creature, CItem* item, CheckEquipConditionItemType::Enum itemType) {
    L2_GUARD_FUNCTION();
    if (!CheckCommon(creature, item)) {
        return false;
    }
    return itemType == CheckEquipConditionItemType::CECIT_SHAPESHIFT ? CheckShapeShifted(creature, item)
                                                                     : CheckPrivate(creature, item);
}

bool CItemEquipCondition::CheckPrivate(CCreature*, CItem*) { return true; }

bool CItemEquipCondition::CheckShapeShifted(CCreature* creature, CItem* item) { return CheckPrivate(creature, item); }

void CItemEquipCondition::GetConditionList(std::vector<int>&) {}

CItemUseCondition::~CItemUseCondition() = default;

bool CItemUseCondition::CheckCommon(CCreature* creature, CItem* item) {
    L2_GUARD_FUNCTION();
    return creature != nullptr && item != nullptr;
}

bool CItemUseCondition::CanUse(CCreature* creature, CItem* item) {
    L2_GUARD_FUNCTION();
    return CheckCommon(creature, item) && CheckPrivate(creature, item);
}

bool CItemUseCondition::CheckPrivate(CCreature*, CItem*) { return true; }

void CItemUseCondition::GetConditionList(std::vector<int>&) {}

#define L2_DEFINE_USE_VECTOR(Name, Type, Field)                                                                             \
    CItemUseCondition_##Name::CItemUseCondition_##Name(std::vector<int>* values) : CItemUseCondition(Type) {                \
        if (values != nullptr) {                                                                                            \
            Field = *values;                                                                                                \
        }                                                                                                                   \
    }                                                                                                                       \
    void CItemUseCondition_##Name::GetConditionList(std::vector<int>& conditions) { conditions = Field; }

L2_DEFINE_USE_VECTOR(race, UCT_RACE, m_vRace)
L2_DEFINE_USE_VECTOR(category, UCT_CATEGORY, m_vCategory)
L2_DEFINE_USE_VECTOR(inzone_num, UCT_INZONE_NUM, m_vInZoneType)
L2_DEFINE_USE_VECTOR(transformid, UCT_TRANSFORMID, m_vTransformID)

#undef L2_DEFINE_USE_VECTOR

bool CItemUseCondition_is_in_residence_siege_field::CheckPrivate(CCreature* creature, CItem*) {
    User* const user = CastPC(creature);
    if (user == nullptr || user->m_AreaMod.m_bIsInPvPZone || !user->m_AreaMod.m_bIsInBattleField) {
        return false;
    }

    const FVector position = user->GetPos();
    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo == nullptr || !geo->IsBattleField(position)) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(position);
    return residence != nullptr && (residence->IsCastle() || residence->IsFortress());
}

bool CItemUseCondition_race::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return Contains(m_vRace, static_cast<int>(creature->m_data->m_Race));
}

bool CItemUseCondition_classid::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return ClassIdForCondition(creature) == m_nClassID;
}

void CItemUseCondition_classid::GetConditionList(std::vector<int>& conditions) { AppendScalar(conditions, m_nClassID); }

bool CItemUseCondition_category::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    const int classId = ClassIdForCondition(creature);
    for (const int category : m_vCategory) {
        if (CCategoryDataDB::Inst().IsInCategory(classId, category)) {
            return true;
        }
    }
    return false;
}

bool CItemUseCondition_inzone::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return m_bIsInInZone == (creature->GetInZoneID() > 0);
}

void CItemUseCondition_inzone::GetConditionList(std::vector<int>& conditions) {
    AppendScalar(conditions, m_bIsInInZone ? 1 : 0);
}

bool CItemUseCondition_inzone_num::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return Contains(m_vInZoneType, static_cast<int>(creature->GetInZoneID()));
}

bool CItemUseCondition_requiredlevel::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    const int level = creature->IsPC() ? creature->m_data->m_nLevel : -1;
    return level >= m_nRequiredLevel;
}

void CItemUseCondition_requiredlevel::GetConditionList(std::vector<int>& conditions) {
    AppendScalar(conditions, m_nRequiredLevel);
}

CItemUseCondition_transmode_include::CItemUseCondition_transmode_include() : CItemUseCondition(UCT_TRANSMODE_INCLUDE) {}

void CItemUseCondition_transmode_include::SetData(std::vector<int>* values) {
    if (values != nullptr) {
        m_vTransformMode = *values;
    }
}

bool CItemUseCondition_transmode_include::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return true;
    }
    return Contains(m_vTransformMode, static_cast<int>(creature->CastUser()->GetTransformType()));
}

void CItemUseCondition_transmode_include::GetConditionList(std::vector<int>& conditions) { conditions = m_vTransformMode; }

CItemUseCondition_transmode_exclude::CItemUseCondition_transmode_exclude() : CItemUseCondition(UCT_TRANSMODE_EXCLUDE) {}

void CItemUseCondition_transmode_exclude::SetData(std::vector<int>* values) {
    if (values != nullptr) {
        m_vTransformMode = *values;
    }
}

bool CItemUseCondition_transmode_exclude::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return true;
    }
    return !Contains(m_vTransformMode, static_cast<int>(creature->CastUser()->GetTransformType()));
}

void CItemUseCondition_transmode_exclude::GetConditionList(std::vector<int>& conditions) { conditions = m_vTransformMode; }

bool CItemUseCondition_level::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    const int level = creature->IsPC() ? creature->m_data->m_nLevel : -1;
    return m_nStartLevel <= level && level <= m_nEndLevel;
}

bool CItemUseCondition_restart_point::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && static_cast<int>(creature->CastUser()->GetRestartPoint()) == m_nRestartPoint;
}

void CItemUseCondition_restart_point::GetConditionList(std::vector<int>& conditions) {
    AppendScalar(conditions, m_nRestartPoint);
}

bool CItemUseCondition_transformid::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    creature->IsPC();
    return Contains(m_vTransformID, creature->m_data->m_transformID);
}

bool CItemUseCondition_vital_restore::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && user->CanUseVitalConsumeItem();
}

bool CItemUseCondition_sp::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->m_data->m_nSP + m_nSP < 50000000001LL;
}

bool CItemUseCondition_adena::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->m_inv.GetAdenaAmount() + m_nAdena < 1000000000000LL;
}

bool CItemUseCondition_peacezone::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && AreaBasedRule::CancelPost(user);
}

bool CItemUseCondition_subjob::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->m_data->m_nSubJobId != 0;
}

bool CItemUseCondition_vital_item_count::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && user->CanChangeVitalItemCount();
}

bool CItemUseCondition_vital_point_remain::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC() && creature->CastUser()->GetVitalPoint() > 0;
}

bool CItemUseCondtion_Maxhp::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user->m_data->GetHP() != user->m_data->m_dOrgHP;
}

bool CItemUseCondtion_Maxmp::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user->m_data->GetMP() != user->m_data->m_dOrgMP;
}

bool CItemUseCondtion_Maxcp::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user->m_data->GetCP() != user->m_data->m_dOrgCP;
}

CItemUseCondtion_PreventDuplicationABNS::CItemUseCondtion_PreventDuplicationABNS() :
    CItemUseCondition(UCT_NONE), m_nOption(0), m_bCurrentState(false) {}

void CItemUseCondtion_PreventDuplicationABNS::SetData(std::vector<int>* values) {
    if (values != nullptr) {
        m_vUniqueABNS = *values;
    }
}

bool CItemUseCondtion_PreventDuplicationABNS::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    if (user == nullptr) {
        return false;
    }
    for (const int slot : m_vUniqueABNS) {
        if (user->HasAbnormalStatusSlot(static_cast<AbnormalStatusSlotType>(slot))) {
            return false;
        }
    }
    return true;
}

bool CItemUseCondtion_KalieEventBuff::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    if (user == nullptr) {
        return false;
    }
    if (!g_bFreeKalieEvent) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::LUCKY_GIFT_EVENT_AVAILABLE_AFTER_SERVER_OPEN);
    return false;
}

bool CItemUseCondtion_VitalRestore::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user != nullptr && user->GetVitalPoint() + m_nRestorepoint < 140001;
}

bool CItemUseCondtion_VitalRestore_Down::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user != nullptr && user->GetVitalPoint() >= m_nRestorepointDown;
}

#define L2_DEFINE_EQUIP_VECTOR(Name, Type, Field)                                                                           \
    CItemEquipCondition_##Name::CItemEquipCondition_##Name(std::vector<int>* values) : CItemEquipCondition(Type) {          \
        if (values != nullptr) {                                                                                            \
            Field = *values;                                                                                                \
        }                                                                                                                   \
    }                                                                                                                       \
    void CItemEquipCondition_##Name::GetConditionList(std::vector<int>& conditions) { conditions = Field; }

L2_DEFINE_EQUIP_VECTOR(race, ECT_RACE, m_vRace)
L2_DEFINE_EQUIP_VECTOR(category, ECT_CATEGORY, m_vCategory)
L2_DEFINE_EQUIP_VECTOR(some_agit, ECT_SOME_AGIT, m_vAgit)
L2_DEFINE_EQUIP_VECTOR(some_castle, ECT_SOME_CASTLE, m_vCastle)
L2_DEFINE_EQUIP_VECTOR(some_fortress, ECT_SOME_FORTRESS, m_vFortress)
L2_DEFINE_EQUIP_VECTOR(inzone_num, ECT_INZONE_NUM, m_vInZoneType)

#undef L2_DEFINE_EQUIP_VECTOR

#define L2_DEFINE_EQUIP_INT_LIST(Name, Field)                                                                               \
    void CItemEquipCondition_##Name::GetConditionList(std::vector<int>& conditions) { AppendScalar(conditions, Field); }

#define L2_DEFINE_EQUIP_BOOL_LIST(Name, Field)                                                                              \
    void CItemEquipCondition_##Name::GetConditionList(std::vector<int>& conditions) {                                       \
        AppendScalar(conditions, Field ? 1 : 0);                                                                            \
    }

L2_DEFINE_EQUIP_INT_LIST(sex, m_nSex)
L2_DEFINE_EQUIP_INT_LIST(classid, m_nClassID)
L2_DEFINE_EQUIP_BOOL_LIST(agit, m_bHasAgit)
L2_DEFINE_EQUIP_BOOL_LIST(castle, m_bHasCastle)
L2_DEFINE_EQUIP_BOOL_LIST(clan_leader, m_bIsClanLeader)
L2_DEFINE_EQUIP_INT_LIST(clan_level, m_nClanLevel)
L2_DEFINE_EQUIP_BOOL_LIST(hero, m_bIsHero)
L2_DEFINE_EQUIP_BOOL_LIST(nobless, m_bIsNobless)
L2_DEFINE_EQUIP_BOOL_LIST(honors, m_bIsNobless)
L2_DEFINE_EQUIP_BOOL_LIST(chao, m_bIsChao)
L2_DEFINE_EQUIP_BOOL_LIST(academy, m_bIsAcademyMember)
L2_DEFINE_EQUIP_INT_LIST(social_class, m_nSocialClass)
L2_DEFINE_EQUIP_BOOL_LIST(fortress, m_bHasFortress)
L2_DEFINE_EQUIP_INT_LIST(subjob, m_nSubjobId)
L2_DEFINE_EQUIP_BOOL_LIST(inzone, m_bIsInInZone)
L2_DEFINE_EQUIP_INT_LIST(requiredlevel, m_nRequiredLevel)
L2_DEFINE_EQUIP_INT_LIST(castle_type, m_nCastleType)
L2_DEFINE_EQUIP_INT_LIST(pledge_member, m_nPledgeLevel)

#undef L2_DEFINE_EQUIP_INT_LIST
#undef L2_DEFINE_EQUIP_BOOL_LIST

bool CItemEquipCondition_sex::CheckPrivate(CCreature* creature, CItem*) {
    return static_cast<int>(creature->m_data->m_Sex) == m_nSex;
}

bool CItemEquipCondition_race::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return Contains(m_vRace, static_cast<int>(creature->m_data->m_Race));
}

bool CItemEquipCondition_classid::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return ClassIdForCondition(creature) == m_nClassID;
}

bool CItemEquipCondition_category::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    const int classId = ClassIdForCondition(creature);
    for (const int category : m_vCategory) {
        if (CCategoryDataDB::Inst().IsInCategory(classId, category)) {
            return true;
        }
    }
    return false;
}

bool CItemEquipCondition_agit::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    if (user == nullptr) {
        return false;
    }
    if (user->HasWorldAgit() == m_bHasAgit) {
        return true;
    }
    return CInstantAgitManager::inst().GetInstantAgit(*user) != nullptr;
}

bool CItemEquipCondition_some_agit::CheckPrivate(CCreature* creature, CItem*) {
    User* const user = CastPC(creature);
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    CWorldAgit* const agit = pledge != nullptr ? pledge->GetWorldAgit() : nullptr;
    return agit != nullptr && agit->m_data != nullptr && Contains(m_vAgit, agit->m_data->m_nResidenceID);
}

bool CItemEquipCondition_castle::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->HasCastle() == m_bHasCastle;
}

bool CItemEquipCondition_some_castle::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    CCastle* const castle = pledge != nullptr ? pledge->GetCastle() : nullptr;
    return castle != nullptr && castle->m_data != nullptr && Contains(m_vCastle, castle->m_data->m_nResidenceID);
}

bool CItemEquipCondition_clan_leader::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->IsPledgeMaster() == m_bIsClanLeader;
}

bool CItemEquipCondition_clan_level::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    return pledge != nullptr && static_cast<int>(pledge->GetPledgeLevel()) >= m_nClanLevel;
}

bool CItemEquipCondition_hero::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->IsHero() == m_bIsHero;
}

bool CItemEquipCondition_nobless::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    if (user == nullptr) {
        return false;
    }
    const int type = user->m_data->m_nNoblessType;
    return m_bIsNobless == (type == 1 || type == 2);
}

bool CItemEquipCondition_honors::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    return m_bIsNobless == (creature->CastUser()->m_data->m_nNoblessType == 2);
}

bool CItemEquipCondition_chao::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && m_bIsChao == (user->m_data->m_nKarma < 0);
}

bool CItemEquipCondition_academy::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    return m_bIsAcademyMember == (creature->CastUser()->m_data->m_nPledgeType == -1);
}

bool CItemEquipCondition_academy::CheckShapeShifted(CCreature*, CItem*) {
    L2_GUARD_FUNCTION();
    return true;
}

bool CItemEquipCondition_social_class::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->GetSocialClass() >= m_nSocialClass;
}

bool CItemEquipCondition_fortress::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return creature->IsPC() && creature->CastUser()->HasFortress() == m_bHasFortress;
}

bool CItemEquipCondition_some_fortress::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    CFortress* const fortress = pledge != nullptr ? pledge->GetFortress() : nullptr;
    return fortress != nullptr && fortress->m_data != nullptr && Contains(m_vFortress, fortress->m_data->m_nResidenceID);
}

bool CItemEquipCondition_subjob::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && user->m_data->m_nSubJobId == m_nSubjobId;
}

bool CItemEquipCondition_inzone::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return m_bIsInInZone == (creature->GetInZoneID() > 0);
}

bool CItemEquipCondition_inzone_num::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    return Contains(m_vInZoneType, static_cast<int>(creature->GetInZoneID()));
}

bool CItemEquipCondition_requiredlevel::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    const int level = creature->IsPC() ? creature->m_data->m_nLevel : -1;
    return level >= m_nRequiredLevel;
}

CItemEquipCondition_transmode_include::CItemEquipCondition_transmode_include() :
    CItemEquipCondition(ECT_TRANSMODE_INCLUDE) {}

void CItemEquipCondition_transmode_include::SetData(std::vector<int>* values) {
    if (values != nullptr) {
        m_vTransformMode = *values;
    }
}

bool CItemEquipCondition_transmode_include::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    return Contains(m_vTransformMode, static_cast<int>(creature->CastUser()->GetTransformType()));
}

void CItemEquipCondition_transmode_include::GetConditionList(std::vector<int>& conditions) { conditions = m_vTransformMode; }

CItemEquipCondition_transmode_exclude::CItemEquipCondition_transmode_exclude() :
    CItemEquipCondition(ECT_TRANSMODE_EXCLUDE) {}

void CItemEquipCondition_transmode_exclude::SetData(std::vector<int>* values) {
    if (values != nullptr) {
        m_vTransformMode = *values;
    }
}

bool CItemEquipCondition_transmode_exclude::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    return !Contains(m_vTransformMode, static_cast<int>(creature->CastUser()->GetTransformType()));
}

void CItemEquipCondition_transmode_exclude::GetConditionList(std::vector<int>& conditions) { conditions = m_vTransformMode; }

bool CItemEquipCondition_castle_type::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    CCastle* const castle = pledge != nullptr ? pledge->GetCastle() : nullptr;
    return castle != nullptr && castle->m_data != nullptr && castle->m_data->m_ResidenceType == m_nCastleType;
}

bool CItemEquipCondition_pledge_member::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    return pledge != nullptr && static_cast<int>(pledge->GetPledgeLevel()) >= m_nPledgeLevel;
}

bool CItemEquipCondition_mainduallevel::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && user->m_data->m_MainClassLevel >= m_MainDualLevel &&
        user->m_data->m_DualClassLevel >= m_MainDualLevel;
}

bool CItemEquipCondition_jobgroup::CheckPrivate(CCreature* creature, CItem*) {
    L2_GUARD_FUNCTION();
    User* const user = CastPC(creature);
    return user != nullptr && user->GetJobGroup() == m_JopGroup;
}
