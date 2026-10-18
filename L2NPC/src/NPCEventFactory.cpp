#include "NPCEventFactory.h"

#include "GlobalObject.h"
#include "L2Core/src/inc/Guard.h"
#include "NPC.h"
#include "NpcEvent.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"
#include "User.h"

#include <cmath>
#include <cstring>

namespace {

std::int64_t PackSpelledSkillNameId(int skillId, int skillLevel, int skillSubLevel) {
    const auto signedBits = [](int value) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(value)); };
    const std::uint64_t packed = ((signedBits(skillId) << 16) | signedBits(skillLevel)) << 16 | signedBits(skillSubLevel);
    std::int64_t result = 0;
    static_assert(sizeof(result) == sizeof(packed));
    std::memcpy(&result, &packed, sizeof(result));
    return result;
}

} // namespace

CNPCEvent* CNPCEventFactory::CreateNoDesireEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    // Shipping 0x004d63f8 writes the id before retaining and publishing
    // the owner.
    event->m_nEventId = 0;
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAttackedEvent(
    CNPC* npc, CSharedCreatureData* attacker, float damage, int argument, std::int64_t context, int tail
) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    // Shipping 0x004d64cc stamps ATTACKED before the owner keep.
    event->m_nEventId = 1;
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->attacker = attacker;
    event->damage = static_cast<int>(damage);
    event->weapon_class_id = argument;
    event->skill_name_id = context;
    event->IsCritical = tail;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSpelledEvent(
    CNPC* npc, CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x02;
    event->speller = speller;
    event->myself = npc;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSpellSuccessedEvent(CNPC* npc, CSharedCreatureData* target, std::int64_t skillNameId) {
    L2_GUARD_FUNCTION();

    std::uint64_t skillNameBits = 0;
    std::memcpy(&skillNameBits, &skillNameId, sizeof(skillNameBits));
    const std::uint32_t skillIdBits = static_cast<std::uint32_t>(skillNameBits >> 32);
    std::int32_t skillId = 0;
    std::memcpy(&skillId, &skillIdBits, sizeof(skillId));

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xaf;
    event->target = target;
    event->myself = npc;
    event->skill_id = skillId;
    event->skill_level = static_cast<std::int32_t>((skillNameBits >> 16) & 0xffffU);
    event->skill_sub_level = static_cast<std::int32_t>(skillNameBits & 0xffffU);
    event->skill_name_id = skillNameId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTalkedEvent(CNPC* npc, CSharedCreatureData* talker, int fromChoice, int code) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x03;
    event->talker = talker;
    event->myself = npc;
    event->_from_choice = fromChoice;
    event->_code = code;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTalkSelectedEvent(CNPC* npc, CSharedCreatureData* talker, int fromChoice, int code) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x04;
    event->talker = talker;
    event->myself = npc;
    event->_from_choice = fromChoice;
    event->_code = code;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSeeCreatureEvent(CNPC* npc, CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x05;
    event->creature = creature;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSeeItemEvent(CNPC* npc, ItemIndexList* itemIndexList) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    // Shipping 0x004d8f10 keeps the owner before writing any factory
    // fields, then stores id, the borrowed list, and finally myself.
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x07;
    event->m_ItemIndexList = itemIndexList;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSeeSpellEvent(
    CNPC* npc, CSharedCreatureData* speller, CSharedCreatureData* target, std::int64_t skillNameId
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x0f;
    event->myself = npc;
    event->speller = speller;
    event->target = target;
    event->skill_name_id = skillNameId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDesireManipulation(CNPC* npc, CSharedCreatureData* speller, int desire, double value) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x11;
    event->myself = npc;
    event->speller = speller;
    event->desire = desire;
    event->i11 = runtime::Win64CvttDoubleToInt64(value);
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePartyAttackedEvent(
    CNPC* npc, CSharedCreatureData* attacker, CSharedCreatureData* privateNpc, int damage
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x12;
    event->attacker = attacker;
    event->_private = privateNpc;
    event->damage = damage;
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePartySpelledEvent(
    CNPC* npc, CSharedCreatureData* speller, CSharedCreatureData* privateNpc, int skillId, int skillLevel, int skillSubLevel
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x13;
    event->speller = speller;
    event->_private = privateNpc;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePartyDiedEvent(CNPC* npc, CNPC* deadMember) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x14;

    CSharedCreatureData* const deadData = deadMember->m_data;
    event->_private = deadData;

    CSharedCreatureData* const attacker = deadMember->GetLastAttacker();
    event->attacker = attacker;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateClanAttackedEvent(
    CNPC* npc,
    CSharedCreatureData* attacker,
    CSharedCreatureData* victim,
    int weaponClassId,
    std::int64_t skillNameId,
    int damage
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x16;
    event->attacker = attacker;
    event->victim = victim;
    event->weapon_class_id = weaponClassId;
    event->skill_name_id = skillNameId;
    event->damage = damage;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateClanSpelledEvent(
    CNPC* npc, CSharedCreatureData* speller, CSharedCreatureData* victim, int skillId, int skillLevel, int skillSubLevel
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x17;
    event->speller = speller;
    event->victim = victim;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateStaticObjectClanAttackedEvent(
    CNPC* npc,
    CSharedCreatureData* attacker,
    CSharedStaticObjectData* victim,
    int weaponClassId,
    std::int64_t skillNameId,
    int damage
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x18;
    event->attacker = attacker;
    event->victim_so = victim;
    event->weapon_class_id = weaponClassId;
    event->skill_name_id = skillNameId;
    event->damage = damage;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateStaticObjectClanSpelledEvent(
    CNPC* npc, CSharedCreatureData* speller, CSharedStaticObjectData* victim, int skillId, int skillLevel, int skillSubLevel
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x19;
    event->speller = speller;
    event->victim_so = victim;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCreatedEvent(CNPC* npc, int argument) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x0c;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(argument);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTimerEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x0a;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTimerEventEx(CNPC* npc, int timerId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x0b;
    event->myself = npc;
    event->timer_id = timerId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateOutOfTerritoryEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x10;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMyDyingEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    event->m_nEventId = 9;
    event->myself = npc;

    CSharedCreatureData* lastAttacker = npc->GetLastAttacker();
    if (lastAttacker && lastAttacker->m_spMaster) {
        lastAttacker = lastAttacker->m_spMaster;
    }

    event->last_attacker = lastAttacker;

    CSharedPartyData* party = gGlobalObj.GetParty(lastAttacker);
    event->lparty = party;

    CSharedGeneralObjectData* mpcc = gGlobalObj.GetMPCC(lastAttacker);
    event->lmpcc = mpcc;
    event->aparty = nullptr;

    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTimeEvent(CNPC* npc, int time) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x0d;
    event->time = time;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAttackedByPledgeEvent(CNPC* npc, int pledgeIndex) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x30;
    event->myself = npc;
    event->pledge_index = pledgeIndex;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUserConnectedEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x36;
    event->talker = talker;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAuctionAgitGetCostInfoReturned(
    CNPC* npc, CSharedCreatureData* talker, std::int64_t cost, char16_t* expirationText
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x5c;
    event->talker = talker;
    event->reply = cost;
    event->s0 = expirationText;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDeletePreviousOlympiadPointReturned(CNPC* npc, User* talker, int ask, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x60;
    event->talker = talker->m_data;
    event->ask = ask;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDeleteOlympiadTradePointReturned(CNPC* npc, User* talker, int deletedPoint, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x61;
    event->talker = talker->m_data;
    event->ask = deletedPoint;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCreateAcademyEvent(
    CNPC* npc, CSharedCreatureData* talker, char16_t* academyName, int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x67;
    event->myself = npc;
    event->talker = talker;
    event->s0 = academyName;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCreateSubPledgeEvent(
    CNPC* npc, CSharedCreatureData* talker, int reply, int pledgeType, int pledgeMasterDbId, char16_t* subPledgeName
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x68;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(pledgeType);
    event->i1 = static_cast<std::int64_t>(pledgeMasterDbId);
    event->s0 = subPledgeName;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUpdateSubPledgeMasterEvent(
    CNPC* npc, CSharedCreatureData* talker, int reply, int pledgeType
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x6a;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(pledgeType);
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePledgeMasterTransferEvent(CNPC* npc, User* talker, char16_t* newMasterName, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x6d;
    event->myself = npc;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    event->s0 = newMasterName;
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePledgeMasterTransferCancelEvent(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x6e;
    event->myself = npc;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_CheckMemberRegisterStatusReturned(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x6f;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRenameSubPledgeEvent(
    CNPC* npc, User* talker, PledgeType pledgeType, char16_t* subPledgeName, int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x70;
    event->myself = npc;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    event->s0 = subPledgeName;
    event->i0 = static_cast<std::int64_t>(pledgeType);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAbnormalStatusChangedEvent(
    CNPC* npc, CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->speller = speller;
    event->myself = npc;
    event->m_nEventId = 0x71;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTeleportToUserResEvent(
    CNPC* npc, CSharedCreatureData* talker, int integer0, int success
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->myself = npc;
    event->m_nEventId = 0x72;
    event->success = success;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDoorHpLevelInformed(CNPC* npc, CSharedCreatureData* talker, int level) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x4d;
    event->talker = talker;
    event->level = level;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateControlTowerLevelInformed(CNPC* npc, CSharedCreatureData* talker, int level) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x4e;
    event->talker = talker;
    event->level = level;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_RegisterPledgeReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x4f;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_RegisterMemberReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x50;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_GetNpcTypeInformed(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x51;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_SetNpcTypeReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x52;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_GetPledgeRegisterStatusInformed(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x53;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTB_GetBattleRoyalPledgeListInformed(
    CNPC* npc,
    CSharedCreatureData* talker,
    int integer0,
    int integer1,
    int integer2,
    int integer3,
    int integer4,
    int integer5,
    int integer6,
    int integer7,
    int integer8,
    int integer9,
    int integer10,
    int integer11
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x54;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    event->i3 = static_cast<std::int64_t>(integer3);
    event->i4 = static_cast<std::int64_t>(integer4);
    event->i5 = static_cast<std::int64_t>(integer5);
    event->i6 = static_cast<std::int64_t>(integer6);
    event->i7 = static_cast<std::int64_t>(integer7);
    event->i8 = static_cast<std::int64_t>(integer8);
    event->i9 = static_cast<std::int64_t>(integer9);
    event->i10 = static_cast<std::int64_t>(integer10);
    event->i11 = static_cast<std::int64_t>(integer11);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSubJobListInformed(
    CNPC* npc,
    CSharedCreatureData* talker,
    int ask,
    int integer8,
    int integer0,
    int integer1,
    int integer2,
    int integer3,
    int integer4,
    int integer5,
    int integer6,
    int integer7
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x55;
    event->talker = talker;
    event->ask = ask;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    event->i3 = static_cast<std::int64_t>(integer3);
    event->i4 = static_cast<std::int64_t>(integer4);
    event->i5 = static_cast<std::int64_t>(integer5);
    event->i6 = static_cast<std::int64_t>(integer6);
    event->i7 = static_cast<std::int64_t>(integer7);
    event->i8 = static_cast<std::int64_t>(integer8);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSubJobCreated(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x56;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSubJobChanged(CNPC* npc, CSharedCreatureData* talker, int reply, int level) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x57;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->level = level;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSubJobRenewed(CNPC* npc, CSharedCreatureData* talker, int reply, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x58;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUserLevelUpEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xa0;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateGetRelatedFortressListReturned(
    CNPC* npc, CSharedCreatureData* talker, int fortress0, int fortress1, int fortress2, int fortress3, int fortress4
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x95;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(fortress0);
    event->i1 = static_cast<std::int64_t>(fortress1);
    event->i2 = static_cast<std::int64_t>(fortress2);
    event->i3 = static_cast<std::int64_t>(fortress3);
    event->i4 = static_cast<std::int64_t>(fortress4);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateChangeEventStage(CNPC* npc, int stage) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xc7;
    event->myself = npc;
    event->i0 = static_cast<std::int64_t>(stage);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCheckKamarokaRewardEvent(CNPC* npc, CSharedCreatureData* talker, int result, int reward) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xca;
    event->talker = talker;
    event->myself = npc;
    event->rank = result;
    event->reward_flag = reward;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAttackFinishedEvent(CNPC* npc, CSharedCreatureData* target) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x39;
    event->target = target;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFleeFinishedEvent(CNPC* npc, CSharedCreatureData* target) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x3b;
    event->target = target;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateGetItemFinishedEvent(CNPC* npc, CSharedItemData* item, int success) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x3c;
    event->item = item;
    event->myself = npc;
    event->success = success;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFollowFinishedEvent(CNPC* npc, CSharedCreatureData* target) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x3e;
    event->target = target;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMoveToWayPointFinishedEvent(CNPC* npc, int wayPointIndex, int nextWayPointIndex) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x3f;
    event->way_point_index = wayPointIndex;
    event->next_way_point_index = nextWayPointIndex;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUseSkillFinishedEvent(
    CNPC* npc, CSharedCreatureData* target, std::int64_t skillNameId, int success
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x40;
    event->target = target;
    event->skill_name_id = skillNameId;
    event->success = success;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMoveToFinishedEvent(CNPC* npc, const FVector& destination) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x41;
    event->x = runtime::Win64CvttDoubleToInt32(destination.x);
    event->y = runtime::Win64CvttDoubleToInt32(destination.y);
    event->z = runtime::Win64CvttDoubleToInt32(destination.z);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSSQSystemEvent(CNPC* npc, int ask, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x59;
    event->myself = npc;
    event->ask = ask;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateClanDiedEvent(CNPC* npc, CSharedCreatureData* victim) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x5d;
    event->victim = victim;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateNodeArrivedEvent(
    CNPC* npc, int argument1, int argument2, int argument3, int state, int success
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x73;
    event->script_event_arg1 = argument1;
    event->script_event_arg2 = argument2;
    event->script_event_arg3 = argument3;
    event->state = state;
    event->success = success;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateShowEnchantSkillDrawerEvent(
    CNPC* npc, CSharedCreatureData* talker, std::int64_t skillNameId, int actionId
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x79;
    event->talker = talker;
    event->myself = npc;
    event->skill_name_id = skillNameId;
    event->action_id = actionId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFireEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x7a;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateInstantZoneEnterReturned(
    CNPC* npc, CSharedCreatureData* talker, int reply, int state, int integer0
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x7b;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->state = state;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateInstantZoneRemainDuration(CNPC* npc, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x7c;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCastleSiegeDefenceCountDecreaseReturned(
    CNPC* npc, CSharedCreatureData* talker, int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x82;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRegisterPVPMatchResultEvent(
    CNPC* npc,
    CSharedCreatureData* talker,
    int reply,
    std::uint32_t integer0,
    std::uint32_t integer1,
    std::uint32_t integer2,
    std::uint32_t integer3,
    std::uint32_t integer4
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x83;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    event->i3 = static_cast<std::int64_t>(integer3);
    event->i4 = static_cast<std::int64_t>(integer4);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUnRegisterPVPMatchResultEvent(
    CNPC* npc,
    CSharedCreatureData* talker,
    std::uint32_t integer0,
    std::uint32_t integer1,
    std::uint32_t integer2,
    std::uint32_t integer3,
    std::uint32_t integer4,
    int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x84;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    event->i3 = static_cast<std::int64_t>(integer3);
    event->i4 = static_cast<std::int64_t>(integer4);
    event->i5 = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateStartPVPMatchResult(
    CNPC* npc, int reply, std::uint32_t integer0, std::uint32_t integer1
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x85;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateEndPVPMatchResult(
    CNPC* npc,
    std::uint32_t integer0,
    std::uint32_t integer1,
    std::uint32_t integer2,
    std::uint32_t integer3,
    std::uint32_t integer4,
    int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x86;
    event->myself = npc;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    event->i3 = static_cast<std::int64_t>(integer3);
    event->i4 = static_cast<std::int64_t>(integer4);
    event->i5 = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCheckRegisterPartyResult(CNPC* npc, std::uint32_t integer0, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x87;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCheckRegisterPartyResult2(
    CNPC* npc, std::uint32_t integer0, int integer1, std::uint32_t integer2, int integer3
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x88;
    event->myself = npc;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    event->i3 = static_cast<std::int64_t>(integer3);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRegisterResurrectionTowerResult(CNPC* npc, std::uint32_t integer0, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x89;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCheckRegisterUserResult(CNPC* npc, std::uint32_t integer0, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x8a;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateIsUserPVPMatchingResult(CNPC* npc, std::uint32_t integer0, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x8b;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRegisterUserPVPMatchResultEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x8c;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUnregisterUserPVPMatchResultEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x8d;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRegisterUserResurrectionTowerResult(CNPC* npc, std::uint32_t integer0, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x8e;
    event->myself = npc;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreatekilledUserPVPMatchResultEvent(
    CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker, int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x8f;
    event->myself = npc;
    event->talker = talker;
    event->attacker = attacker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateGetRankUserPVPMatchResultEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x90;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateLetInUserPVPMatchEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x91;
    event->myself = npc;
    event->talker = talker;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateStartUserPVPMatchResult(CNPC* npc) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x92;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateEndUserPVPMatchResult(
    CNPC* npc,
    CSharedCreatureData* creature0,
    CSharedCreatureData* creature1,
    CSharedCreatureData* creature2,
    CSharedCreatureData* creature3,
    CSharedCreatureData* creature4
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x94;
    event->myself = npc;
    event->c0 = creature0;
    event->c1 = creature1;
    event->c2 = creature2;
    event->c3 = creature3;
    event->c4 = creature4;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateChangeNPCStatusReturned(CNPC* npc, int state) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x96;
    event->myself = npc;
    event->state = state;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRegisterDominionSiegeReturned(
    CNPC* npc, CSharedCreatureData* talker, int requestArgument, int state, int resultArgument, int success
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x97;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(resultArgument);
    event->i1 = static_cast<std::int64_t>(requestArgument);
    event->state = state;
    event->success = success;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDeclareLordReturned(CNPC* npc, CSharedCreatureData* talker, int integer0, int success) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x98;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->success = success;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateGetRegistrySizeReturned(
    CNPC* npc, CSharedCreatureData* talker, int requestArgument, int state, int registrySize
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x99;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(registrySize);
    event->i1 = static_cast<std::int64_t>(requestArgument);
    event->state = state;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateLoadDBSavingMapReturned(CNPC* npc, int integer0, int integer1) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa1;
    event->myself = npc;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateEventRankerInfo(
    CNPC* npc, CSharedCreatureData* talker, int integer0, int integer1, int integer2
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xb0;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->i2 = static_cast<std::int64_t>(integer2);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUpgradeSubPledgeMemberCountEvent(
    CNPC* npc, CSharedCreatureData* talker, int integer0, int reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x6b;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateOnOlympiadGamePrepared(CNPC* npc, int eventId, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x81;
    event->event_id = eventId;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDominionScriptEvent(
    CNPC* npc, CSharedCreatureData* talker, int scriptArgument1, int scriptArgument2, int scriptArgument3
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->talker = talker;
    event->m_nEventId = 0xa2;
    event->script_event_arg1 = scriptArgument1;
    event->script_event_arg2 = scriptArgument2;
    event->script_event_arg3 = scriptArgument3;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDieSetEvent(CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa3;
    event->myself = npc;
    event->talker = talker;
    event->attacker = attacker;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRestartSetEvent(CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa4;
    event->myself = npc;
    event->talker = talker;
    event->attacker = attacker;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSocialActionEvent(CNPC* npc, CSharedCreatureData* talker, int actionId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa5;
    event->myself = npc;
    event->talker = talker;
    event->action_id = actionId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePetDiedEvent(CNPC* npc, int integer0, int integer1) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa6;
    event->myself = npc;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateWasCollectedEvent(CNPC* npc, CSharedCreatureData* attacker, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa7;
    event->myself = npc;
    event->attacker = attacker;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAirShipEvent(CNPC* npc, int state) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa8;
    event->myself = npc;
    event->state = state;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDominionSupplyDestructed(CNPC* npc, CSharedCreatureData* attacker, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xa9;
    event->myself = npc;
    event->attacker = attacker;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateNpcToWinner(CNPC* npc, CSharedCreatureData* target) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xaa;
    event->myself = npc;
    event->target = target;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCow(CNPC* npc, CSharedCreatureData* target) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xab;
    event->myself = npc;
    event->target = target;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDebugAIEvent(CNPC& npc, int reply, CCreature& creature) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(&(npc), ObjectRefType::EventRetain);
    event->m_nEventId = 0xac;
    event->myself = &npc;
    event->reply = static_cast<std::int64_t>(reply);
    CSharedCreatureData* const shared = creature.m_data;
    event->creature = shared;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateIsToggleSkillOnOffEvent(CNPC* npc, CSharedCreatureData* target, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xad;
    event->myself = npc;
    event->target = target;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateOlympiadMatchResultEvent(CNPC* npc, User* talker, int reply, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xb1;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateInZoneUserEvent(CNPC* npc, CSharedCreatureData* target, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->target = target;
    event->m_nEventId = 0xb8;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateAreaStatusEvent(CNPC* npc, int inzoneId, char16_t* name, bool status) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xb9;
    event->i0 = static_cast<std::int64_t>(inzoneId);
    event->i1 = status ? 1 : 0;
    event->s0 = name;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDoorStatusEvent(CNPC* npc, int inzoneId, char16_t* name, bool status) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xba;
    event->i0 = static_cast<std::int64_t>(inzoneId);
    event->i1 = status ? 1 : 0;
    event->s0 = name;
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePetAIEvent(CNPC* npc, int state) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xb5;
    event->myself = npc;
    event->state = state;
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePetReactionEvent(CNPC* npc, int state) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xb6;
    event->myself = npc;
    event->state = state;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSetAgitDecoReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x5a;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateResetAgitDecoReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x5b;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateWithdrawUserPVPMatchResult(CNPC* npc) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x93;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCleftStateChangedEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x9d;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateBlockUpsetStartedEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x9e;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateBlockUpsetFinishedEvent(CNPC* npc) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x9f;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSetNoblessReturned(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x5e;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSetHeroReturned(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x5f;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCheckCursedUserReturned(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x69;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMPCCTeleportedEvent(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x6c;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTrapStepInEvent(CNPC* npc, int integer0, int integer1) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->myself = npc;
    event->m_nEventId = 0x74;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTrapStepOutEvent(CNPC* npc, int integer0, int integer1) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    event->myself = npc;
    event->m_nEventId = 0x75;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTrapActivatedEvent(CNPC* npc, int integer0) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->myself = npc;
    event->m_nEventId = 0x76;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTrapDefusedEvent(CNPC* npc, int integer0) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->myself = npc;
    event->m_nEventId = 0x77;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTrapDetectedEvent(CNPC* npc, int integer0) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->i0 = static_cast<std::int64_t>(integer0);
    event->myself = npc;
    event->m_nEventId = 0x78;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFortressSiegeRegisterReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x7d;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFortressSiegeUnregisterReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x7e;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFortressContractCastleReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x7f;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFortressUpgradeFacilityReturned(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x80;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDominionSiegeStart(CNPC* npc, CSharedCreatureData* talker, int integer0) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x9a;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDominionSiegeEnd(CNPC* npc, CSharedCreatureData* talker, int integer0) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x9b;
    event->myself = npc;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDominionSiegePCKilled(
    CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker, int integer0
) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x9c;
    event->myself = npc;
    event->talker = talker;
    event->attacker = attacker;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMasterAttackEvent(
    CNPC* npc,
    CSharedCreatureData* attacker,
    CSharedCreatureData* target,
    int damage,
    int skillId,
    int skillLevel,
    int skillSubLevel
) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xbb;
    event->myself = npc;
    event->attacker = attacker;
    event->target = target;
    event->damage = damage;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMasterAttackedEvent(
    CNPC* npc,
    CSharedCreatureData* attacker,
    CSharedCreatureData* target,
    int damage,
    int skillId,
    int skillLevel,
    int skillSubLevel
) {
    L2_GUARD_FUNCTION();

    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xc1;
    event->myself = npc;
    event->attacker = attacker;
    event->target = target;
    event->damage = damage;
    event->skill_id = skillId;
    event->skill_level = skillLevel;
    event->skill_sub_level = skillSubLevel;
    event->skill_name_id = PackSpelledSkillNameId(skillId, skillLevel, skillSubLevel);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFieldCycleStepExpired(CNPC* npc, int integer1, int eventId, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xae;
    event->event_id = eventId;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTeleportRequestedEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x1b;
    event->talker = talker;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateBuyRequestedEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x1c;
    event->talker = talker;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSellRequestedEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x1d;
    event->talker = talker;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateQuestAcceptedEvent(CNPC* npc, CSharedCreatureData* talker, int questId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x1e;
    event->talker = talker;
    event->myself = npc;
    event->quest_id = questId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMenuSelectedEvent(
    CNPC* npc, CSharedCreatureData* talker, int ask, std::int64_t reply, int state, int actionId
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x1f;
    event->talker = talker;
    event->myself = npc;
    event->ask = ask;
    event->reply = reply;
    event->state = state;
    event->action_id = actionId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateManorMenuSelectedEvent(
    CNPC* npc, CSharedCreatureData* talker, int ask, int state, int time
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x26;
    event->talker = talker;
    event->myself = npc;
    event->ask = ask;
    event->state = state;
    event->time = time;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateScriptEvent(CNPC* npc, int argument1, int argument2, int argument3) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x33;
    event->script_event_arg1 = argument1;
    event->script_event_arg2 = argument2;
    event->script_event_arg3 = argument3;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateLearnSkillRequestedEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x20;
    event->talker = talker;
    event->myself = npc;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateEnchantSkillRequestedEvent(CNPC* npc, CSharedCreatureData* talker, int actionId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x21;
    event->talker = talker;
    event->myself = npc;
    event->action_id = actionId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateOneSkillSelectedEvent(
    CNPC* npc, CSharedCreatureData* talker, std::int64_t skillNameId, int needQuest, int integer0
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x22;
    event->talker = talker;
    event->myself = npc;
    event->skill_name_id = skillNameId;
    event->need_quest = needQuest;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateOneEnchantSkillSelectedEvent(
    CNPC* npc, CSharedCreatureData* talker, std::int64_t skillNameId, int actionId
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x23;
    event->talker = talker;
    event->myself = npc;
    event->skill_name_id = skillNameId;
    event->action_id = actionId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateUseSkillStartedEvent(CNPC* npc, CSharedCreatureData* target, std::int64_t skillNameId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->target = target;
    event->m_nEventId = 0xb7;
    event->skill_name_id = skillNameId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSkillQuestAcceptedEvent(CNPC* npc, CSharedCreatureData* talker, int questId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x24;
    event->myself = npc;
    event->talker = talker;
    event->quest_id = questId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateClassChangeRequestedEvent(CNPC* npc, CSharedCreatureData* talker, int occupationNameId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x25;
    event->talker = talker;
    event->myself = npc;
    event->occupation_name_id = occupationNameId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateManageCastleSiegeEvent(CNPC* npc, CSharedCreatureData* talker, int ask, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x31;
    event->myself = npc;
    event->talker = talker;
    event->ask = ask;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCreateAllianceEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x32;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCreatePledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x2c;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDismissPledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x2d;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRevivePledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x2e;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateLevelUpPledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0x2f;
    event->myself = npc;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTutorialEventEvent(CNPC* npc, CSharedCreatureData* talker, int eventId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x34;
    event->talker = talker;
    event->event_id = eventId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTutorialMarkClickedEvent(
    CNPC* npc, CSharedCreatureData* talker, int eventId, int questionId
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0x35;
    event->talker = talker;
    event->event_id = eventId;
    event->question_id = questionId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateRealDamagedEvent(CNPC* npc, CSharedCreatureData* attacker, std::int64_t reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    event->m_nEventId = 0xd6;
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->attacker = attacker;
    event->reply = reply;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateDualJobCreated(CNPC* npc, CSharedCreatureData* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc9;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateSceneStoppedEvent(CNPC* npc, CSharedCreatureData* talker, int sceneId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc6;
    event->talker = talker;
    event->scene_id = sceneId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateGetPledgeRaidRewardRateEvent(CNPC* npc, float rewardRate) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xbd;
    event->f0 = rewardRate;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateInstantAgitEvent(CNPC* npc, CSharedCreatureData* talker, char job, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xbe;
    event->talker = talker;
    event->ask = static_cast<std::int8_t>(job);
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateInstantAgitOwnerEvent(CNPC* npc, char job [[maybe_unused]], int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc3;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCallToChangeClassEvent(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xbf;
    event->talker = talker;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateChangeToAwakenedClassEvent(
    CNPC* npc, CSharedCreatureData* talker, int reply, int integer0
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc0;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateTotalSkillEnchantCountEvent(
    CNPC* npc, CSharedCreatureData* talker, int integer0, int integer1
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc2;
    event->talker = talker;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateChangeClassEvent(CNPC* npc, CSharedCreatureData* talker, int reply, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc4;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateCanChangeToSubclassEvent(
    CNPC* npc, CSharedCreatureData* talker, int reply, int integer0
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xc5;
    event->talker = talker;
    event->reply = static_cast<std::int64_t>(reply);
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateStartDynamicQuestResultEvent(CNPC* npc, CSharedCreatureData* talker, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->m_nEventId = 0xc8;
    event->talker = talker;
    event->myself = npc;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateWorldChatEvent(CNPC* npc, User* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xcb;
    event->talker = talker->m_data;
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePledgeGameResultEvent(CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xcc;
    event->talker = talker->m_data;
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateGetGreatEventContributor(CNPC* npc, CSharedCreatureData* target) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xcd;
    event->target = target;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateFishingEventEvent(CNPC* npc, CSharedCreatureData* talker, int eventId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xce;
    event->talker = talker;
    event->event_id = eventId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateItemNpcEvent(CNPC* npc, CSharedCreatureData* talker, int eventId, int integer0) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xd3;
    event->talker = talker;
    event->event_id = eventId;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreatePcDieNpcEvent(CNPC* npc, CSharedCreatureData* talker, int eventId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xd4;
    event->talker = talker;
    event->event_id = eventId;
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMailNpcEvent(CNPC* npc, CSharedCreatureData* talker, int eventId) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xd5;
    event->talker = talker;
    event->event_id = eventId;
    return event;
}

CNPCEvent* CNPCEventFactory::Create_NPCEVENT_CAST_BUFF_TO_RANDOM_USER(CNPC* npc, CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xcf;
    event->talker = talker;
    return event;
}

CNPCEvent* CNPCEventFactory::Create_NPCEVENT_RESULT_PLEDGE_RAID_POINT(
    CNPC* npc, int success, CSharedPledgeData* pledge, std::int64_t reply
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xd0;
    event->success = success;
    event->pledge0 = pledge;
    event->reply = reply;
    return event;
}

CNPCEvent* CNPCEventFactory::Create_NPCEVENT_WORLD_RAID_START(CNPC* npc, int integer0, int integer1) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xd1;
    event->i0 = static_cast<std::int64_t>(integer0);
    event->i1 = static_cast<std::int64_t>(integer1);
    return event;
}

CNPCEvent* CNPCEventFactory::Create_NPCEVENT_WORLD_RAID_EVENT(
    CNPC* npc, int integer0, int argument1, int argument2, int argument3
) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->m_nEventId = 0xd2;
    event->script_event_arg1 = argument1;
    event->script_event_arg2 = argument2;
    event->script_event_arg3 = argument3;
    event->i0 = static_cast<std::int64_t>(integer0);
    return event;
}

CNPCEvent* CNPCEventFactory::CreateMiniGameAgitEvent(char subtype, CNPC* npc, User* talker, int reply) {
    L2_GUARD_FUNCTION();
    auto* event = new CNPCEvent();
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->myself = npc;
    event->talker = talker->m_data;
    switch (subtype) {
        case 0:
            event->m_nEventId = 0x62;
            break;
        case 1:
            event->m_nEventId = 0x63;
            break;
        case 2:
            event->m_nEventId = 0x64;
            break;
        case 3:
            event->m_nEventId = 0x65;
            break;
        case 4:
            event->m_nEventId = 0x66;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid event id ",
            //     L"NPCEventFactory.cpp",
            //     0x689)
            // Log.Add(LOG_ERROR,
            //     "[AI event] No clan-hall mini-game handler mapping for subtype %d (reply %d). Returning the event with its
            //     default ID.", static_cast<int>(subtype), reply)
            L2_LOG_ERROR(
                Log,
                "[AI event] No clan-hall mini-game handler mapping for subtype %d (reply %d); returning the event with its "
                "default ID.",
                static_cast<int>(subtype), reply
            );
            break;
    }
    event->reply = static_cast<std::int64_t>(reply);
    return event;
}
