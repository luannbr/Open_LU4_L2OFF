#pragma once

#include "Shared/src/PledgeType.h"

#include <cstdint>


class CNPC;
struct CNPCEvent;
struct FVector;
class CCreature;
class CSharedCreatureData;
class CSharedItemData;
class CSharedPledgeData;
class CSharedStaticObjectData;
class ItemIndexList;
class User;

// Recovered from NPCEventFactory.h.
class CNPCEventFactory {
public:
    static CNPCEvent* CreateNoDesireEvent(CNPC* npc);
    static CNPCEvent* CreateAttackedEvent(
        CNPC* npc, CSharedCreatureData* attacker, float damage, int argument, std::int64_t context, int tail
    );
    static CNPCEvent* CreateSpelledEvent(
        CNPC* npc, CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel
    );
    static CNPCEvent* CreateSpellSuccessedEvent(CNPC* npc, CSharedCreatureData* target, std::int64_t skillNameId);
    static CNPCEvent* CreateTalkedEvent(CNPC* npc, CSharedCreatureData* talker, int fromChoice, int code);
    static CNPCEvent* CreateTalkSelectedEvent(CNPC* npc, CSharedCreatureData* talker, int fromChoice, int code);
    static CNPCEvent* CreateSeeCreatureEvent(CNPC* npc, CSharedCreatureData* creature);
    static CNPCEvent* CreateSeeItemEvent(CNPC* npc, ItemIndexList* itemIndexList);
    static CNPCEvent* CreateSeeSpellEvent(
        CNPC* npc, CSharedCreatureData* speller, CSharedCreatureData* target, std::int64_t skillNameId
    );
    static CNPCEvent* CreateDesireManipulation(CNPC* npc, CSharedCreatureData* speller, int desire, double value);
    static CNPCEvent* CreatePartyAttackedEvent(
        CNPC* npc, CSharedCreatureData* attacker, CSharedCreatureData* privateNpc, int damage
    );
    static CNPCEvent* CreatePartySpelledEvent(
        CNPC* npc,
        CSharedCreatureData* speller,
        CSharedCreatureData* privateNpc,
        int skillId,
        int skillLevel,
        int skillSubLevel
    );
    static CNPCEvent* CreatePartyDiedEvent(CNPC* npc, CNPC* deadMember);
    static CNPCEvent* CreateClanAttackedEvent(
        CNPC* npc,
        CSharedCreatureData* attacker,
        CSharedCreatureData* victim,
        int weaponClassId,
        std::int64_t skillNameId,
        int damage
    );
    static CNPCEvent* CreateClanSpelledEvent(
        CNPC* npc, CSharedCreatureData* speller, CSharedCreatureData* victim, int skillId, int skillLevel, int skillSubLevel
    );
    static CNPCEvent* CreateStaticObjectClanAttackedEvent(
        CNPC* npc,
        CSharedCreatureData* attacker,
        CSharedStaticObjectData* victim,
        int weaponClassId,
        std::int64_t skillNameId,
        int damage
    );
    static CNPCEvent* CreateStaticObjectClanSpelledEvent(
        CNPC* npc,
        CSharedCreatureData* speller,
        CSharedStaticObjectData* victim,
        int skillId,
        int skillLevel,
        int skillSubLevel
    );
    static CNPCEvent* CreateCreatedEvent(CNPC* npc, int argument);
    static CNPCEvent* CreateTimerEvent(CNPC* npc);
    static CNPCEvent* CreateTimerEventEx(CNPC* npc, int timerId);
    static CNPCEvent* CreateOutOfTerritoryEvent(CNPC* npc);
    static CNPCEvent* CreateMyDyingEvent(CNPC* npc);
    static CNPCEvent* CreateTimeEvent(CNPC* npc, int time);
    static CNPCEvent* CreateAttackedByPledgeEvent(CNPC* npc, int pledgeIndex);
    static CNPCEvent* CreateUserConnectedEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateAuctionAgitGetCostInfoReturned(
        CNPC* npc, CSharedCreatureData* talker, std::int64_t cost, char16_t* expirationText
    );
    static CNPCEvent* CreateDeletePreviousOlympiadPointReturned(CNPC* npc, User* talker, int ask, int reply);
    static CNPCEvent* CreateDeleteOlympiadTradePointReturned(CNPC* npc, User* talker, int deletedPoint, int reply);
    static CNPCEvent* CreateCreateAcademyEvent(CNPC* npc, CSharedCreatureData* talker, char16_t* academyName, int reply);
    static CNPCEvent* CreateCreateSubPledgeEvent(
        CNPC* npc, CSharedCreatureData* talker, int reply, int pledgeType, int pledgeMasterDbId, char16_t* subPledgeName
    );
    static CNPCEvent* CreateUpdateSubPledgeMasterEvent(CNPC* npc, CSharedCreatureData* talker, int reply, int pledgeType);
    static CNPCEvent* CreatePledgeMasterTransferEvent(CNPC* npc, User* talker, char16_t* newMasterName, int reply);
    static CNPCEvent* CreatePledgeMasterTransferCancelEvent(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateTB_CheckMemberRegisterStatusReturned(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateRenameSubPledgeEvent(
        CNPC* npc, User* talker, PledgeType pledgeType, char16_t* subPledgeName, int reply
    );
    static CNPCEvent* CreateAbnormalStatusChangedEvent(
        CNPC* npc, CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel
    );
    static CNPCEvent* CreateTeleportToUserResEvent(CNPC* npc, CSharedCreatureData* talker, int integer0, int success);
    static CNPCEvent* CreateDoorHpLevelInformed(CNPC* npc, CSharedCreatureData* talker, int level);
    static CNPCEvent* CreateControlTowerLevelInformed(CNPC* npc, CSharedCreatureData* talker, int level);
    static CNPCEvent* CreateTB_RegisterPledgeReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateTB_RegisterMemberReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateTB_GetNpcTypeInformed(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateTB_SetNpcTypeReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateTB_GetPledgeRegisterStatusInformed(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateTB_GetBattleRoyalPledgeListInformed(
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
    );
    static CNPCEvent* CreateSubJobListInformed(
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
    );
    static CNPCEvent* CreateSubJobCreated(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateSubJobChanged(CNPC* npc, CSharedCreatureData* talker, int reply, int level);
    static CNPCEvent* CreateSubJobRenewed(CNPC* npc, CSharedCreatureData* talker, int reply, int integer0);
    static CNPCEvent* CreateUserLevelUpEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateGetRelatedFortressListReturned(
        CNPC* npc, CSharedCreatureData* talker, int fortress0, int fortress1, int fortress2, int fortress3, int fortress4
    );
    static CNPCEvent* CreateChangeEventStage(CNPC* npc, int stage);
    static CNPCEvent* CreateCheckKamarokaRewardEvent(CNPC* npc, CSharedCreatureData* talker, int result, int reward);
    static CNPCEvent* CreateAttackFinishedEvent(CNPC* npc, CSharedCreatureData* target);
    static CNPCEvent* CreateFleeFinishedEvent(CNPC* npc, CSharedCreatureData* target);
    static CNPCEvent* CreateGetItemFinishedEvent(CNPC* npc, CSharedItemData* item, int success);
    static CNPCEvent* CreateFollowFinishedEvent(CNPC* npc, CSharedCreatureData* target);
    static CNPCEvent* CreateMoveToWayPointFinishedEvent(CNPC* npc, int wayPointIndex, int nextWayPointIndex);
    static CNPCEvent* CreateUseSkillFinishedEvent(
        CNPC* npc, CSharedCreatureData* target, std::int64_t skillNameId, int success
    );
    static CNPCEvent* CreateMoveToFinishedEvent(CNPC* npc, const FVector& destination);
    static CNPCEvent* CreateSSQSystemEvent(CNPC* npc, int ask, int reply);
    static CNPCEvent* CreateClanDiedEvent(CNPC* npc, CSharedCreatureData* victim);
    static CNPCEvent* CreateNodeArrivedEvent(CNPC* npc, int argument1, int argument2, int argument3, int state, int success);
    static CNPCEvent* CreateShowEnchantSkillDrawerEvent(
        CNPC* npc, CSharedCreatureData* talker, std::int64_t skillNameId, int actionId
    );
    static CNPCEvent* CreateFireEvent(CNPC* npc);
    static CNPCEvent* CreateInstantZoneEnterReturned(
        CNPC* npc, CSharedCreatureData* talker, int reply, int state, int integer0
    );
    static CNPCEvent* CreateInstantZoneRemainDuration(CNPC* npc, int reply);
    static CNPCEvent* CreateCastleSiegeDefenceCountDecreaseReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateRegisterPVPMatchResultEvent(
        CNPC* npc,
        CSharedCreatureData* talker,
        int reply,
        std::uint32_t integer0,
        std::uint32_t integer1,
        std::uint32_t integer2,
        std::uint32_t integer3,
        std::uint32_t integer4
    );
    static CNPCEvent* CreateUnRegisterPVPMatchResultEvent(
        CNPC* npc,
        CSharedCreatureData* talker,
        std::uint32_t integer0,
        std::uint32_t integer1,
        std::uint32_t integer2,
        std::uint32_t integer3,
        std::uint32_t integer4,
        int reply
    );
    static CNPCEvent* CreateStartPVPMatchResult(CNPC* npc, int reply, std::uint32_t integer0, std::uint32_t integer1);
    static CNPCEvent* CreateEndPVPMatchResult(
        CNPC* npc,
        std::uint32_t integer0,
        std::uint32_t integer1,
        std::uint32_t integer2,
        std::uint32_t integer3,
        std::uint32_t integer4,
        int reply
    );
    static CNPCEvent* CreateCheckRegisterPartyResult(CNPC* npc, std::uint32_t integer0, int reply);
    static CNPCEvent* CreateCheckRegisterPartyResult2(
        CNPC* npc, std::uint32_t integer0, int integer1, std::uint32_t integer2, int integer3
    );
    static CNPCEvent* CreateRegisterResurrectionTowerResult(CNPC* npc, std::uint32_t integer0, int reply);
    static CNPCEvent* CreateCheckRegisterUserResult(CNPC* npc, std::uint32_t integer0, int reply);
    static CNPCEvent* CreateIsUserPVPMatchingResult(CNPC* npc, std::uint32_t integer0, int reply);
    static CNPCEvent* CreateRegisterUserPVPMatchResultEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateUnregisterUserPVPMatchResultEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateRegisterUserResurrectionTowerResult(CNPC* npc, std::uint32_t integer0, int reply);
    static CNPCEvent* CreatekilledUserPVPMatchResultEvent(
        CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker, int reply
    );
    static CNPCEvent* CreateGetRankUserPVPMatchResultEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateLetInUserPVPMatchEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateStartUserPVPMatchResult(CNPC* npc);
    static CNPCEvent* CreateWithdrawUserPVPMatchResult(CNPC* npc);
    static CNPCEvent* CreateEndUserPVPMatchResult(
        CNPC* npc,
        CSharedCreatureData* creature0,
        CSharedCreatureData* creature1,
        CSharedCreatureData* creature2,
        CSharedCreatureData* creature3,
        CSharedCreatureData* creature4
    );
    static CNPCEvent* CreateChangeNPCStatusReturned(CNPC* npc, int state);
    static CNPCEvent* CreateRegisterDominionSiegeReturned(
        CNPC* npc, CSharedCreatureData* talker, int requestArgument, int state, int resultArgument, int success
    );
    static CNPCEvent* CreateDeclareLordReturned(CNPC* npc, CSharedCreatureData* talker, int integer0, int success);
    static CNPCEvent* CreateGetRegistrySizeReturned(
        CNPC* npc, CSharedCreatureData* talker, int requestArgument, int state, int registrySize
    );
    static CNPCEvent* CreateLoadDBSavingMapReturned(CNPC* npc, int integer0, int integer1);
    static CNPCEvent* CreateEventRankerInfo(
        CNPC* npc, CSharedCreatureData* talker, int integer0, int integer1, int integer2
    );
    static CNPCEvent* CreateUpgradeSubPledgeMemberCountEvent(
        CNPC* npc, CSharedCreatureData* talker, int integer0, int reply
    );
    static CNPCEvent* CreateOnOlympiadGamePrepared(CNPC* npc, int eventId, int integer0);
    static CNPCEvent* CreateDominionScriptEvent(
        CNPC* npc, CSharedCreatureData* talker, int scriptArgument1, int scriptArgument2, int scriptArgument3
    );
    static CNPCEvent* CreateDieSetEvent(CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker);
    static CNPCEvent* CreateRestartSetEvent(CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker);
    static CNPCEvent* CreateSocialActionEvent(CNPC* npc, CSharedCreatureData* talker, int actionId);
    static CNPCEvent* CreatePetDiedEvent(CNPC* npc, int integer0, int integer1);
    static CNPCEvent* CreateWasCollectedEvent(CNPC* npc, CSharedCreatureData* attacker, int integer0);
    static CNPCEvent* CreateAirShipEvent(CNPC* npc, int state);
    static CNPCEvent* CreateDominionSupplyDestructed(CNPC* npc, CSharedCreatureData* attacker, int integer0);
    static CNPCEvent* CreateNpcToWinner(CNPC* npc, CSharedCreatureData* target);
    static CNPCEvent* CreateCow(CNPC* npc, CSharedCreatureData* target);
    static CNPCEvent* CreateDebugAIEvent(CNPC& npc, int reply, CCreature& creature);
    static CNPCEvent* CreateIsToggleSkillOnOffEvent(CNPC* npc, CSharedCreatureData* target, int integer0);
    static CNPCEvent* CreateOlympiadMatchResultEvent(CNPC* npc, User* talker, int reply, int integer0);
    static CNPCEvent* CreateInZoneUserEvent(CNPC* npc, CSharedCreatureData* target, int integer0);
    static CNPCEvent* CreateAreaStatusEvent(CNPC* npc, int inzoneId, char16_t* name, bool status);
    static CNPCEvent* CreateDoorStatusEvent(CNPC* npc, int inzoneId, char16_t* name, bool status);
    static CNPCEvent* CreatePetAIEvent(CNPC* npc, int state);
    static CNPCEvent* CreatePetReactionEvent(CNPC* npc, int state);
    static CNPCEvent* CreateSetAgitDecoReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateResetAgitDecoReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateCleftStateChangedEvent(CNPC* npc);
    static CNPCEvent* CreateBlockUpsetStartedEvent(CNPC* npc);
    static CNPCEvent* CreateBlockUpsetFinishedEvent(CNPC* npc);
    static CNPCEvent* CreateSetNoblessReturned(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateSetHeroReturned(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateCheckCursedUserReturned(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateMPCCTeleportedEvent(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateTrapStepInEvent(CNPC* npc, int integer0, int integer1);
    static CNPCEvent* CreateTrapStepOutEvent(CNPC* npc, int integer0, int integer1);
    static CNPCEvent* CreateTrapActivatedEvent(CNPC* npc, int integer0);
    static CNPCEvent* CreateTrapDefusedEvent(CNPC* npc, int integer0);
    static CNPCEvent* CreateTrapDetectedEvent(CNPC* npc, int integer0);
    static CNPCEvent* CreateFortressSiegeRegisterReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateFortressSiegeUnregisterReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateFortressContractCastleReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateFortressUpgradeFacilityReturned(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateDominionSiegeStart(CNPC* npc, CSharedCreatureData* talker, int integer0);
    static CNPCEvent* CreateDominionSiegeEnd(CNPC* npc, CSharedCreatureData* talker, int integer0);
    static CNPCEvent* CreateDominionSiegePCKilled(
        CNPC* npc, CSharedCreatureData* talker, CSharedCreatureData* attacker, int integer0
    );
    static CNPCEvent* CreateMasterAttackEvent(
        CNPC* npc,
        CSharedCreatureData* attacker,
        CSharedCreatureData* target,
        int damage,
        int skillId,
        int skillLevel,
        int skillSubLevel
    );
    static CNPCEvent* CreateMasterAttackedEvent(
        CNPC* npc,
        CSharedCreatureData* attacker,
        CSharedCreatureData* target,
        int damage,
        int skillId,
        int skillLevel,
        int skillSubLevel
    );
    static CNPCEvent* CreateFieldCycleStepExpired(CNPC* npc, int integer1, int eventId, int integer0);
    static CNPCEvent* CreateTeleportRequestedEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateBuyRequestedEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateSellRequestedEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateQuestAcceptedEvent(CNPC* npc, CSharedCreatureData* talker, int questId);
    static CNPCEvent* CreateMenuSelectedEvent(
        CNPC* npc, CSharedCreatureData* talker, int ask, std::int64_t reply, int state, int actionId
    );
    static CNPCEvent* CreateManorMenuSelectedEvent(CNPC* npc, CSharedCreatureData* talker, int ask, int state, int time);
    static CNPCEvent* CreateScriptEvent(CNPC* npc, int argument1, int argument2, int argument3);
    static CNPCEvent* CreateLearnSkillRequestedEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateEnchantSkillRequestedEvent(CNPC* npc, CSharedCreatureData* talker, int actionId);
    static CNPCEvent* CreateOneSkillSelectedEvent(
        CNPC* npc, CSharedCreatureData* talker, std::int64_t skillNameId, int needQuest, int integer0
    );
    static CNPCEvent* CreateOneEnchantSkillSelectedEvent(
        CNPC* npc, CSharedCreatureData* talker, std::int64_t skillNameId, int actionId
    );
    static CNPCEvent* CreateUseSkillStartedEvent(CNPC* npc, CSharedCreatureData* target, std::int64_t skillNameId);
    static CNPCEvent* CreateSkillQuestAcceptedEvent(CNPC* npc, CSharedCreatureData* talker, int questId);
    static CNPCEvent* CreateClassChangeRequestedEvent(CNPC* npc, CSharedCreatureData* talker, int occupationNameId);
    static CNPCEvent* CreateManageCastleSiegeEvent(CNPC* npc, CSharedCreatureData* talker, int ask, int reply);
    static CNPCEvent* CreateCreateAllianceEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateCreatePledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateDismissPledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateRevivePledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateLevelUpPledgeEvent(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateTutorialEventEvent(CNPC* npc, CSharedCreatureData* talker, int eventId);
    static CNPCEvent* CreateTutorialMarkClickedEvent(CNPC* npc, CSharedCreatureData* talker, int eventId, int questionId);
    static CNPCEvent* CreateRealDamagedEvent(CNPC* npc, CSharedCreatureData* attacker, std::int64_t reply);
    static CNPCEvent* CreateDualJobCreated(CNPC* npc, CSharedCreatureData* talker, int reply);
    static CNPCEvent* CreateSceneStoppedEvent(CNPC* npc, CSharedCreatureData* talker, int sceneId);
    static CNPCEvent* CreateGetPledgeRaidRewardRateEvent(CNPC* npc, float rewardRate);
    static CNPCEvent* CreateInstantAgitEvent(CNPC* npc, CSharedCreatureData* talker, char job, int reply);
    static CNPCEvent* CreateInstantAgitOwnerEvent(CNPC* npc, char job, int reply);
    static CNPCEvent* CreateCallToChangeClassEvent(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* CreateChangeToAwakenedClassEvent(CNPC* npc, CSharedCreatureData* talker, int reply, int integer0);
    static CNPCEvent* CreateTotalSkillEnchantCountEvent(CNPC* npc, CSharedCreatureData* talker, int integer0, int integer1);
    static CNPCEvent* CreateChangeClassEvent(CNPC* npc, CSharedCreatureData* talker, int reply, int integer0);
    static CNPCEvent* CreateCanChangeToSubclassEvent(CNPC* npc, CSharedCreatureData* talker, int reply, int integer0);
    static CNPCEvent* CreateStartDynamicQuestResultEvent(CNPC* npc, CSharedCreatureData* talker, int integer0);
    static CNPCEvent* CreateWorldChatEvent(CNPC* npc, User* talker);
    static CNPCEvent* CreatePledgeGameResultEvent(CNPC* npc, User* talker, int reply);
    static CNPCEvent* CreateGetGreatEventContributor(CNPC* npc, CSharedCreatureData* target);
    static CNPCEvent* CreateFishingEventEvent(CNPC* npc, CSharedCreatureData* talker, int eventId);
    static CNPCEvent* CreateItemNpcEvent(CNPC* npc, CSharedCreatureData* talker, int eventId, int integer0);
    static CNPCEvent* CreatePcDieNpcEvent(CNPC* npc, CSharedCreatureData* talker, int eventId);
    static CNPCEvent* CreateMailNpcEvent(CNPC* npc, CSharedCreatureData* talker, int eventId);
    static CNPCEvent* Create_NPCEVENT_CAST_BUFF_TO_RANDOM_USER(CNPC* npc, CSharedCreatureData* talker);
    static CNPCEvent* Create_NPCEVENT_RESULT_PLEDGE_RAID_POINT(
        CNPC* npc, int success, CSharedPledgeData* pledge, std::int64_t reply
    );
    static CNPCEvent* Create_NPCEVENT_WORLD_RAID_START(CNPC* npc, int integer0, int integer1);
    static CNPCEvent* Create_NPCEVENT_WORLD_RAID_EVENT(CNPC* npc, int integer0, int argument1, int argument2, int argument3);
    static CNPCEvent* CreateMiniGameAgitEvent(char subtype, CNPC* npc, User* talker, int reply);
};
