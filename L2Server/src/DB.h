#pragma once

#include "AccountJobFinishSignal.h"
#include "Pledge.h"
#include "Portable/ProcessRuntime.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/ReuseDelaySaveReason.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>
#include <string>

struct _SYSTEMTIME;

namespace protocol {
class RQ_EX_SET_RESERVATION_ANNOUNCE;
class RQ_UPDATE_BALTHUS_EVENT_USER_INFO;
namespace Faction {
enum UpdateCommand : int;
enum Reason : int;
} // namespace Faction
} // namespace protocol

namespace Commission {
enum Enum : std::int32_t;
}

namespace CommissionDiscountInfo {
enum Enum : std::int32_t;
}

namespace CommissionExpired {
enum Enum : std::int32_t;
}

namespace CommissionFirst {
enum Enum : std::int32_t;
}

namespace CommissionOwner {
enum Enum : std::int32_t;
}

namespace GItemType {
enum Enum : std::int32_t;
}

namespace DBSecurity {
enum BuilderCmdType : std::int32_t;
}

namespace DROIDNPC_COMMAND {
enum DROIDNPC_COMMAND_TYPE : int;
}

namespace ManipulateType {
enum Enum : std::int32_t;
}

namespace TrainingZone {
enum EndReason : std::int32_t;
}

class CAlliance;
class CAuctionAgit;
class CBattleCamp;
class CCastle;
class CCompeteAgit;
class CDoor;
class CDominion;
class CEtcItem;
class CFortress;
class CItem;
class ItemManip;
class CMailListParameter;
class CMiniGameAgit;
class CNPC;
class CPet;
class CResidence;
class CSiegeAgit;
class CTeamBattleAgit;
class CUserRidingInfo;
class CWorldAgit;
class CWareHouseWithdraw;
class ICDailyMission;
class ItemOptionKey;
class CStackOnly;
class QueryBase;
class IPacketHelper;
class AtomicJob;
class User;
struct InventoryParams;
struct CreateItemStruct;
struct AcquiredSkillInfo;
struct ResidenceGuardInfo;
struct UserMatchingPoint;
template <std::size_t Capacity>
class CPacketHelperEx;

enum CrystalType : std::int32_t;
enum WareHouseClass : std::int32_t;

enum AddtionalLocationType : std::int32_t {
    ALT_NONE = 0,
    ALT_BUILDER = 1,
    ALT_FORCE = 2,
};


class CDB : public MemoryObject {
public:
    class CWorldSetter {
    public:
        ~CWorldSetter() { runtime::process::SetCurrentWorldId(0); }
    };

    CDB();
    ~CDB() override;

    std::uint64_t CreateDBSocket(std::uint64_t index);
    bool InitCacheD();

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    virtual void Send(const char* format, ...) const;
    void SendDB(int worldId, const char* format, ...) const;
    void SendPacket(int worldId, ::RQ_PROTOCOL protocol, ::RQ_EX_PROTOCOL extendedProtocol, network::IPacket& packet) const;
    void RequestMailList(User* user, CMailListParameter& parameter);
    void RequestReadMail(User* user, int mailId, CMailListParameter& parameter);
    void RequestGetTempMail(User* user, int mailId, CMailListParameter& parameter);
    void RequestSendMail(
        User* user,
        int value1,
        int value2,
        const char16_t* receiver,
        const char16_t* title,
        const char16_t* content,
        CMailListParameter& parameter
    );
    void RequestSendPledgeMail(User* user, const char16_t* title, const char16_t* content);
    void RequestSaveMail(
        User* user,
        int value1,
        int value2,
        const char16_t* receiver,
        const char16_t* title,
        const char16_t* content,
        CMailListParameter& parameter
    );
    void RequestReplyMail(User* user, int mailId, CMailListParameter& parameter);
    void RequestForwardMail(User* user, int mailId, CMailListParameter& parameter);
    void RequestModifyMail(User* user, int mailId, const char16_t* receiver, const char16_t* title, const char16_t* content);
    void RequestModifySendMail(
        User* user, int mailId, const char16_t* receiver, const char16_t* title, const char16_t* content, CMailListParameter& parameter
    );
    void RequestDeleteMail(User* user, int mailId, CMailListParameter& parameter);
    void RequestArchiveMail(User* user, int mailId, CMailListParameter& parameter);
    bool SendPrivateStoreCommit(
        User* seller, User* buyer, std::int64_t totalPrice, int itemCount, int packetLength, char* packet
    ) const;
    void TradeDivideAdenaCommit(User* user, int targetCharacterId, int packetLength, char* packet);
    void SendTradeCommit(
        User* first,
        User* second,
        int firstItemCount,
        int firstPacketLength,
        char* firstPacket,
        int secondItemCount,
        int secondPacketLength,
        char* secondPacket
    ) const;
    void RequestCreateChar(
        const char16_t* characterName,
        int race,
        int sex,
        int classId,
        int strength,
        int dexterity,
        int constitution,
        int intelligence,
        int wit,
        int mentality,
        int hairStyle,
        int hairColor,
        int face,
        CUserSocket* socket,
        int level
    ) const;
    void RequestDeleteChar(unsigned int accountId, unsigned int characterId, CUserSocket* socket) const;
    void RestoreCharacter(int characterId, unsigned int accountId, CUserSocket* socket);
    void RequestSetUserNickname(User* actor, User* target, const char16_t* nickname);
    void RequestCreateInitItems(unsigned int characterId, int classId, CUserSocket* socket) const;
    void RequestCrystallizeItem(CItem* item, User* user, int itemCount, int packetLength, char* packet) const;
    void RequestCrystallizePetItem(CItem* item, User* user, CPet* pet, int itemCount, int packetLength, char* packet) const;
    void RequestSellItems(
        int npcClassId, User* user, std::int64_t totalPrice, int itemCount, int packetLength, char* packet
    ) const;
    void RequestBuyItems(
        int npcClassId,
        User* user,
        std::int64_t totalPrice,
        int tax,
        int residenceId,
        bool useResidenceOwner,
        int itemCount,
        int packetLength,
        char* packet
    ) const;
    void RequestDropItem(CItem* item, User* user, FVector position, SlotType slot, bool unknown, std::int64_t count) const;
    void RequestCancelEquipmentAddedEffectAndDropItem(
        User* user, CItem* item, FVector* position, SlotType slot, std::int64_t count, int effectMask, bool setContributeData
    );
    void RequestDropCastleGuard(
        CItem* item, User* user, FVector position, FAngle direction, unsigned int residenceId, ResidenceGuardInfo* guardInfo
    ) const;
    void RequestLoadMercenary(CResidence& residence) const;
    void RequestDeleteMercenary(CResidence& residence) const;
    void RequestSaveMercenary(
        CResidence& residence, int npcIndex, int npcId, FVector position, FAngle direction, double hp, double mp
    ) const;
    void RequestLoadGuardData(CResidence& residence) const;
    void RequestDeleteGuardData(CResidence& residence) const;

    void RequestSaveDropItemAmountPerServer(int value1, int value2);
    void RequestLoadItemAmountPerServer(int itemCount, CPacketHelperEx<20480>* packet);
    void RequestResetEventData(int value1, int value2);
    void RequestSetEventDataPeriod(int period);
    void RequestLoadHonorsSkill(User* user);
    void RequestSetHonorsSkill(User* user, int skillId, int level);
    void RequestDeleteHonorsSkill(User* user, int skillId);
    void SaveStriderMeal(User* user, CUserRidingInfo& ridingInfo);

    void DecreaseUserKarma_KillSummon(int userId, int summonId);
    void RequestNPCPickItem(int npcId, int itemId, int itemType) const;
    void RequestAgitCost(CAuctionAgit& agit, int pledgeId, std::int64_t lease, int costFail, std::int32_t time);
    void RequestAgitAutoSell(CAuctionAgit& agit, std::int64_t minimumBidPrice, int auctionPeriod, int bidDay, int unknown);
    void RequestSetAgitAuction(int agitId, int auctionId);
    void RequestGetContributionRelatedPledge(CResidence& residence, int residenceId);
    void RequestLoadAgitBid(int value1, int value2);
    void SendAgitPledgeAdena(User* user, int value1, int value2, int value3);
    void RequestAgitInfo(User* user, int value1, int value2);
    void RequestAgitSale1(User* user, int value1, int value2);
    void RequestAgitBidRe(User* user, int value1, int value2);
    void RequestAgitBid1(User* user, int value1, int value2);
    void RequestAgitBidCancelRequest(User* user, int value1, int value2, int value3, int value4);
    void RequestCreateAgitAuction(
        User* user,
        int value1,
        int unused,
        const char16_t* description,
        std::int64_t price,
        int period,
        std::int64_t deposit,
        int value2
    );
    void RequestCreateAgitBid(User* user, int value1, int value2, int value3, std::int64_t bid);
    void RequestCancelAgitAuction(User* user, int value1, int value2);
    void RequestSavePetitionMsgForceGetDBID(User& user, const char16_t* value1, const char16_t* value2);
    void RequestMngComment(int value1, User* user, const char16_t* value2, int value3, const char16_t* value4);
    void RequestChangeTimeLimit(User* user, const char16_t* value1, int value2, int value3, int value4);
    void RequestLoadLastTaxChange();
    void RequestSaveLastTaxChange(std::int32_t incomeUpdate, std::int32_t taxChange, std::int32_t manorResetUpdate);
    void RequestLoadPlayTime(int accountId);
    void RequestUpdateOneDayRewardInfo(User* user, int updateType, const ICDailyMission* mission);

    void RequestShortCutInfo(User* user) const;
    void RequestSetShortCut(User* user, int slot, int type, int id, int category, int macroValue) const;
    void EquipHenna(
        User* user, int value1, std::int64_t value2, std::int64_t value3, int value4, int value5, bool value6, int value7
    );
    void UnequipHenna(User* user, int value1, std::int64_t value2, std::int64_t value3, int value4, bool value5);
    void RequestGetMacro(User* user);
    void RequestCreateMacro(User* user, int dataSize, const unsigned char* data);
    void RequestUpdateMacro(User* user, int dataSize, const unsigned char* data);
    void RequestDeleteMacro(User* user, int macroId);
    void RequestDeleteMacroAll(User* user);
    void RequestGetMemoList(User* user, int value, const char16_t* name);
    void RequestClanBBSView(User* user, int value);
    void RequestBBSClanList(
        User* user,
        int worldId,
        int value1,
        int value2,
        int value3,
        SharedUtilsWideChar* name,
        int value4,
        int dataSize,
        const unsigned char* data
    );
    void RequestBBSClanAdmin(
        User* user, int value1, int value2, SharedUtilsWideChar* name, int value3, int dataSize, const unsigned char* data
    );
    void RequestBuilderBookMarkAdd(User* user, const char16_t* bookmarkName, int x, int y, int z);
    void RequestBuilderBookMarkList(User* user, int value);
    void RequestBuilderBookMarkOne(User* user, int value);
    void RequestBuilderBookMarkDel(User* user, int value);
    void RequestBuilderBookMarkTelbyName(User* user, const char16_t* name);
    void RequestDeleteSubJob(User* user, int subJobId);
    void RequestCancelDualClass(User* user, bool value);
    void RequestMainClassSkill(User* user);
    void RequestReloadSkill(User* user);
    void RequestLoadSubJob(User* user, bool value);
    void RequestGetSubJobListForNpc(User* user, int value1, int value2, int value3, int value4);
    void RequestGetXMasSealAmount(User* user);
    void RequestInitXMasSealAmount(User* user);
    void RequestGetPledgeAnnounce(User* user, unsigned int pledgeId, int value);
    void RequestSetPledgeAnnounce(User* user, unsigned int pledgeId, int value, const char16_t* announcement);
    void RequestSwitchPledgeAnnounce(User* user, unsigned int pledgeId, int value);
    void RequestSetAcademyMaster(User* user, unsigned int value1, unsigned int value2, unsigned int value3);
    void RequestGraduateAcademy(CPledge& pledge, User* user);
    void RequestPCCafeCouponCheck(User* user, const int value1, const int value2, const int value3, const int value4);
    void RequestConvertItem(User* user, int value1, int value2);
    void RequestChangeMergedName(int value1, int value2, const char16_t* name, CUserSocket* socket);
    void RequestGetTutorial(User& user);
    void RequestLoadUI(User& user);
    void RequestSaveUI(User& user, int dataSize, unsigned char* data);
    void RequestSetAssociatedInZone(User& user, unsigned int value1, int value2);
    void RequestSaveInventorySlotIndex(User* user);
    void RequestWaitingTransferPledgeMaster(CPledge& pledge);
    void _CreateUpdateItems(
        User* user,
        ManipulateType::Enum type,
        int id,
        int itemCount,
        int packetSize,
        char* packetData,
        AtomicJob* atomicJob,
        int etcManipulationCount,
        CPacketHelperEx<20480>* etcManipulationPacket,
        bool premium
    ) const;
    void CreateUpdateItems(
        User* user,
        int itemCount,
        int packetSize,
        char* packetData,
        AtomicJob* atomicJob,
        int etcManipulationCount,
        CPacketHelperEx<20480>* etcManipulationPacket,
        bool premium
    ) const;
    bool CreateUpdateItemsAtWarehouse(
        int userDatabaseId,
        int manipulationCount,
        ItemManip* manipulations,
        AtomicJob* atomicJob,
        int etcManipulationCount,
        CPacketHelperEx<20480>* etcManipulationPacket
    ) const;
    void CreateUpdatePremiumItems(User* user, std::int64_t warehouseNo, CItem* item, InventoryParams parameters) const;
    void CreateUpdateItemsWithPoint(
        User* user,
        int point,
        int pointType,
        int operation,
        int value,
        int itemCount,
        CPacketHelperEx<20480>* packet,
        int packetSize,
        char* packetData,
        AtomicJob* atomicJob,
        int line
    );
    bool RequestManipulatePledgeItemNew(
        int pledgeDatabaseId,
        int userDatabaseId,
        const std::vector<CreateItemStruct>& items,
        AtomicJob* atomicJob,
        int etcManiCount,
        CPacketHelperEx<20480>* etcManiPacket
    ) const;
    bool RequestManipulatePledgeItem(
        const User* user,
        int pledgeDatabaseId,
        const std::vector<CreateItemStruct>& items,
        AtomicJob* atomicJob,
        int etcManiCount,
        CPacketHelperEx<20480>* etcManiPacket
    ) const;
    void CreateUpdateItemsWithPoint(
        int worldId,
        std::uint32_t userDatabaseId,
        std::uint32_t accountId,
        int value1,
        int point,
        int pointType,
        int value2,
        int packetSize,
        char* packetData,
        AtomicJob* atomicJob,
        int line
    );
    void RequestChangeNicknameColor(User& user, int color, const char16_t* name);
    void SetBirthday(User& user, int value1, int value2, int value3);
    void LoadUserBookmark(User* user);
    void LoadPeriodicItemInfo(User* user);
    void RemoveExpiredPeriodicItem(unsigned int itemDatabaseId, unsigned int itemServerId, User& user, int itemType);
    void RequestSaveRetrieveGoods(User* user, std::int64_t goodsUid, int itemPrimaryId, int itemClassId, int itemAmount);
    void AddUserBookmarkSlot(User* user, int slot);
    void SaveUserBookmarkSlot(User* user, int slot, FVector& position, int value, const char16_t* name, const char16_t* tag);
    void DeleteUserBookmarkSlot(User* user, int slot);
    void ChangeUserBookmarkSlot(User* user, int fromSlot, int toSlot);
    void RequestSaveDuelRecord(
        User* user,
        int individualWinCount,
        int individualLoseCount,
        int individualDrawCount,
        int partyWinCount,
        int partyLoseCount,
        int partyDrawCount
    );
    void RequestLoadPVPPointRestrain(User* user);
    void RequestSavePVPPointRestrain(User* user, int giveCount, int giveTime);
    void RequestLoadBossRecord(User& user);
    void RequestLoadDuelRecord(User* user);
    void RequestLoadBirthInfo(User* user);
    void RequestLoad5YearInfo(User* user);
    void RequestLoadPetItemCount(User& user);
    void RequestLoadPostFriend(User& user);
    void RequestAddPostFriend(User& user, const char16_t* name);
    void RequestDeletePostFriend(User& user, const char16_t* name);
    void RequestLoadSummons(User* user, int summonType);
    void WithdrawPet(User* user, CItem* petCollar, bool newlySummoned);
    void LoadPetItems(CPet* pet, User& user, bool loadInventory, int requestValue);
    void GiveItemToPet(User* user, CItem* item, std::int64_t amount);
    void GetItemFromPet(User* user, CItem* item, std::int64_t amount);
    void SaveSummonInfectedSkills(
        User& user, int npcClassId, int summonIndex, AtomicJob* atomicJob, int skillCount, int packetLength, char* packet
    ) const;
    void SavePetInfo(CPet* pet, AtomicJob* atomicJob);
    void SavePetNameChanged(CPet& pet);
    void ChangePetName(CPet* pet, const char16_t* name);
    void RequestDeleteSummon(User& user, int summonIndex, AtomicJob* atomicJob);
    void RequestChangeSubJob(User* user, int subJobId);
    void RequestCreateSubJob(User* user, int subJobId, int newClassId, bool force);
    void RequestCreateSubJobForNpc(User* user, int npcSid, int userSid, int subJobId, int newClassId);
    void RequestChangeSubJobForNpc(User* user, int npcSid, int userSid, int subJobId);
    void RequestRenewSubJob(User* user, int subJobId, int newClassId, int renewType, bool force, int npcSid, int userSid);
    void RequestStartTraniningZone(User* user);
    void RequestEndTraniningZone(User* user, TrainingZone::EndReason reason, int endTime);
    void RequestRewardTraniningZone(User* user);
    void RequestLoadTraniningZone(User* user);
    void RequestCreateDualJob(User* user, int dualClassId, int unknownAlwaysOne, bool createFlag, int npcSid, int userSid);
    void RequestSaveCharacterForSubJob(User* user);
    void RequestSaveAcquiredSkills(User* user, std::map<int, AcquiredSkillInfo>& acquiredSkills);
    void DepositPet(User* user, CPet* pet, bool complete, AtomicJob* atomicJob);

    void ChangeSSQDropGuardStatus(bool status);
    void SetDawnRoundToPledge(int value1, int value2);
    void StartSSQEvent(int value);
    void InitTimeAttackRecord(int value);
    void LoadTimeAttackInfo();

    void RequestLoadMarketPriceList();
    void RequestLoadAllNobless(bool value1, bool value2);
    void RequestReloadNobless(int value);

    void RequestLoadCursedWeaponList();
    void RequestAddCursedWeapon(
        int value1, int value2, int value3, int value4, double value5, int value6, int value7, int value8, int value9
    );
    void RequestUpdateCursedWeapon(int value1, double value2, int value3, int value4, int value5, int value6);
    void RequestDelCursedWeapon(int value);
    void RequestDelCursedWeaponOnlyInDB(int value1, int value2);
    void RequestLoadPledgeRanking();
    void RequestSetPledgePowerGrade(unsigned int value1, int value2);
    void UpdatePledgeNameValue(
        unsigned int pledgeServerId, unsigned int pledgeId, int requestContext, int nameValueDelta, int worldId
    );
    void UpdatePledgeNameValueByPledgeWar(
        unsigned int value1, unsigned int value2, unsigned int pledgeId, int value4, int nameValueDelta, unsigned int value6
    );
    void UpdateDefenceSiegeCount(CPledge& pledge, int defenceCount);

    void RequestLoadFieldCycleInfo(int value);
    void RequestSaveFieldCycleInfo(int value1, int value2, int value3, int value4, int value5, int value6, int value7);

    void RequestLoadAllAuction(int value);
    void RequestLoadAllAuctionBid(int value);
    void RequestCreateItemAuction(int value1, int value2, int value3, int value4);
    void RequestSetEndTime(int value1, int value2, int value3);
    void RequestSetAuctionStatus(int value1, int value2);
    void RequestSetAuctionItem(int value1, int value2);

    void RequestLoadInstantZoneFlag(int value1, int value2);
    void RequestCreateInstantZone(int value1, int value2, int value3, int value4);
    void RequestLoadInZoneRestriction(User* user);
    void RequestMarkInZoneRestriction(
        std::CDualKey<int, int> userKey,
        int inzoneTypeId,
        int latestBindingTime,
        int entranceCount,
        bool groupRestriction,
        int maxEntranceCount,
        int inzoneDatabaseId
    );
    void RequestResetInZoneRestriction(User& user);
    void RequestLoadUserInzonePoint(User& user);
    void RequestUpdateUserInzonePoint(User& user, int inzoneTypeId, int point, int currentTime);
    void RequestLoadInzoneMaxDBID();
    void RequestSaveInstantZoneFlag(int value1, int value2, int value3);

    void RequestLoadAirShip();
    void RequestSaveAirShip(unsigned int value1, int value2);
    void RequestRemoveAirShip(unsigned int value1, int value2);

    void RequestSendPost(int value);
    void RequestRejectPost(int value1, int value2, int value3);
    void RequestOpenPost(int value1, int value2, int value3);
    void RequestLoadPostList(User& user);
    void RequestReceivedPostList(User& user);
    void RequestSentPostList(User& user);

    void RequestTPacketProcess(int worldId, short protocol, int dataSize, CStackOnly& packet);
    void RequestTQuery(int worldId, short protocol, void* signal, int dataSize, QueryBase& packet);
    void RequestWOlympiadGetMatchResult(int worldId, int characterId);
    static int PushAtomicJobHelper(AtomicJob* atomicJob);
    void RequestSaveVitality(IPacketHelper& packet, int worldId);
    void RequestCheckRaidWorld(int worldId);
    void RequestInsertEventRanking(int value1, int value2, const char16_t* value3, int value4);
    void RequestLoadEventTopRankers(int value1, int value2, int value3);
    void RequestUpdateEventRanking(int value1, int value2);
    void RequestGetEventScore(int value1, int value2, int value3, const char16_t* value4);
    void RequestSetEventScore(int value1, int value2, int value3, int value4);
    void RequestLoadEventDataFromDB();
    void RequestDumpEvent(int index, int total, std::u16string eventName, std::u16string contents);
    void RequestDBSecuritySetting(DBSecurity::BuilderCmdType setting);

    void ResetProfile();
    void ReportProfile(int value);
    void RequestLoadAllDynamicQuest();
    void RequestSaveDynamicQuest(int value1, int value2);

    void RequestLoadInstantAgit();
    void RequestLoadInstantAgitDeco();
    void RequestLoadInstantAgitDecoNpc();
    void RequestTryChooseInstantAgitOwner(
        int instantAgitId, int nextLotteryDate, std::int64_t rentalFee, IPacketHelper& packet
    );
    void RequestClearEventTime(User* user);
    void RequestClearOneEventTime(int value1, int value2);
    void RequestCharLevelUp(User& user, int nameValueDelta, int value2, int value3);
    void RequestModifyAppliedUserInfo(User* user);
    void LoadPeriodicShapedShiftedItemInfo(User* user);
    void RequestUpdateMaxLevel(User& user, unsigned int level);
    void RequestUpdateSociality(User& user, int argument, _SYSTEMTIME* time);
    void RequestUpdateUserRaceSex(User* user, RaceType race, SexType sex);
    void RequestDeletePetitionExternalNotify(User& user);
    void RequestLoadPetitionExternalNotify(User& user);
    void RequestLoadAccountMaxLevel(int value);
    void RequestReloadUserFlagOn(User& user);
    void RequestBeautyPurchaseList(User& user, char requestFlag);
    void RequestUpdateHairAccFlag(User& user);
    void RequestBeautyLoad(int groupCount, CPacketHelperEx<20480> packet);
    void RequestProductSoldCount(int productCount, CPacketHelperEx<20480>& packet);
    void RequestBuyBeautyItem(
        User* user, int builder, std::int64_t adenaPrice, std::int64_t beautyCoinPrice, CPacketHelperEx<20480> packet
    );
    void RequestResetBeautyItem(User* user, int builder, std::int64_t adenaPrice, CPacketHelperEx<20480> packet);
    void RequestLoadBalthusEventUserInfo(User& user);
    void SendAcquiredAbilitySkill(User& user, CPacketHelperEx<20480>& packet);
    void RequestResetAbilitySkillToDB(User& user, std::int64_t costSp);
    void LoadPeriodicHennaInfo(User* user);
    void RequestLoadItems(User* user, bool forceReload) const;
    void RemoveExpiredPeriodicHenna(User* user, int hennaId);
    void RequestLoadChar(unsigned int accountId, CUserSocket* socket) const;
    void RequestLoadWorldChatPoint(User* user);
    void RequestIncWorldChatPoint(User* user);
    void RequestResetWorldChatPoint();
    void RequestWorldChatResetTime();
    void RequestPremiumKamarokaRound();
    void RequestPremiumKamarokaRank(int value1, int value2);
    void RequestPremiumKamarokaPersonalRankInfo(int round, User* user);
    void RequestPremiumKamarokaAddNewRecord(
        int round, int score, int leaderCharacterId, std::u16string characterList, CPacketHelperEx<20480>& packet
    );
    void RequestLoadClientINI(int value);
    void RequestSaveClientINI(int accountId, char type, std::string value);
    void RequestLoadDroidNPCAppearance(
        DROIDNPC_COMMAND::DROIDNPC_COMMAND_TYPE command,
        unsigned int requestContext,
        unsigned int accountId,
        unsigned int characterId
    );
    void RequestUpdateWedding(User& user, int partnerDatabaseId, char updateType);
    void RequestLoadAutoSoulShotUI(User* user);
    void RequestSaveAutoSoulShotUI(User* user);
    void RequestInitUserPledgeBonus(
        User* user,
        CPledge* pledge,
        int bonusIndex,
        std::int32_t lastSaveTime,
        int currentBonusPoint,
        int maxBonusPoint,
        int todayRewardLevel
    );
    void RequestUpdateUserPledgeBonus(
        User* user, CPledge* pledge, int bonusIndex, std::int32_t lastSaveTime, int addedPoint, int maxBonusPoint
    );
    void RequestLoadUserPledgeBonus(User* user);
    void RequestUpdateUserPledgeBonusReward(User* user, int rewardType, std::int32_t updateTime);
    void RequestSetItemReuseDelay(User* user, int reuseKey, int reuseEndTime);
    void RequestUpdateCardUpdownGameRecord(User& user, CPacketHelperEx<20480>& packet);
    void RequestLoadFactionPoint(User* user);
    void RequestUpdateFactionPoint(
        User* user,
        int factionType,
        std::int64_t factionPoint,
        protocol::Faction::UpdateCommand command,
        protocol::Faction::Reason reason
    );

    void SendDeleteLoginAnnounce(int value);
    void ShowIntervalAnnounce(CUserSocket* socket, int repetitive);
    void SetIntervalAnnounce(CUserSocket* socket, int interval, int announceId, const char16_t* message);
    void DelIntervalAnnounce(CUserSocket* socket, int interval, int announceId);
    void LoadIntervalAnnounce();
    void LoadSelectedCharacter(
        int worldId,
        int characterId,
        CUserSocket* socket,
        AddtionalLocationType additionalLocationType,
        int value1,
        int value2,
        int value3,
        bool force
    );
    void RequestCreateAllianceByNpc(CNPC* npc, User* user, CPledge* pledge, const char16_t* name);
    void RequestLoadAllAlliance();
    void RequestSetAllianceInfo(CAlliance& alliance, std::uint32_t selector, std::uint32_t value);
    void RequestLoadAlliance(int worldId, int value);
    void RequestLoadDismissReservedPledge();
    void RequestLoadAllPledgeWar();
    void RequestDeletePledgeCrest(unsigned int value);
    void RequestGetDbRelatedCounts();
    void RequestSetUserPledgeInfo(CPledge* pledge, unsigned int userDatabaseId, unsigned int selector, unsigned int value);
    void RequestSetUserPledgeInfo(User* user, unsigned int selector, unsigned int value);
    void SavePledgeWarPoint(CPledge& pledge, int value1, int value2);
    void SavePledgeWarPointPrev(CPledge& pledge, int value1, int value2);
    void RequestUpdatePledgeWarKillCount(CPledge& pledge, int targetPledgeDatabaseId, int killCount);
    void ResetWarTargeted(CPledge& pledge, int value, std::int32_t resetTime);
    void RequestViewSiegeList(User* user, unsigned int residenceId, const char16_t* pledgeName);
    void RequestApproveBattle(User* user, unsigned int residenceId, const char16_t* pledgeName);
    void RequestSaveBotReport(User* user);
    void RequestLoadBotReport(User* user);
    void RequestLoadBotReportByGM(User* user, User* target);
    void RequestLoadBotReportTopTen(User* user);
    void RequestResetBotReport(User* user);
    void RequestSetBotReport(User* user, int botReportPoint, int confirmTimes, int confirmSeconds, int delaySeconds);
    void RequestSetBotAdmin(User* user);
    void RequestDecayBotReport(User* user);
    void RequestLoadUserTimeToGiveKarmaPointToKiller(User* user);
    void RequestSaveUserTimeToGiveKarmaPointToKiller(User* user, int time);
    void RequestDeleteSkillList(User* user, int deleteCount, CPacketHelperEx<20480>* deletePacket);
    void RequestDeleteAndSaveSkillList(
        User* user, int deleteCount, CPacketHelperEx<20480>* deletePacket, int saveCount, CPacketHelperEx<20480>* savePacket
    );
    void RequestLoadMinigameScores(User* user);
    void RequestInsertMinigameScores(User* user, int score);
    void RequestGetLectureMark(User* user);
    void RequestSetLectureMark(User* user, char mark);
    void RequestLoadFriendList(User* user);
    void RequestBreakFriendship(User* user, unsigned int characterId);
    void RequestBreakFriendshipBatch(User* user, std::vector<int>& characterIds);
    void RequestDeleteBlockListBatch(User* user, std::vector<int>& results, std::vector<std::u16string>& names) const;
    void RequestAddBlockList(User* user, const char16_t* characterName, bool flag) const;
    void RequestDeleteBlockList(User* user, const char16_t* characterName) const;
    void RequestUpdateFriendMemo(User* user, int friendDatabaseId, const char16_t* memo);
    void RequestUpdateBlockMemo(User* user, int blockedDatabaseId, const char16_t* memo);
    void RequestLoadFriendDetails(User* user, int characterId);
    void RequestLoadExDropCount(User* user);
    void SaveQuestInfo(User* user);
    void SaveQuestNPCLog(User* user, int questId) const;
    void RequestUpdateExDropCount(User* user, int countId, int count, L2ServerLong time);
    void RequestResetExDropCount(User* user);
    void SendPunishmentHistory(User* user, int reason);
    void RequestSetPunish(User& user, int punishType, int save, int durationMinutes);
    void RequestGetPunish(User& user);
    void RequestGetLoginAnnounce(User& user);
    void RequestLogCharInfo(User& user);
    void SaveDieDropExp(User& user, std::int64_t experience) const;
    void AddHennaPeriod(User* user, std::uint32_t hennaId, int period);
    void AddItemPeriod(User& user, unsigned int itemDbId, int period);
    void RequestLoadSharedReuseDelaysOfItems(User& user);
    void RequestSaveSharedReuseDelayOfItem(User& user, int sharedReuseGroupId, int reuseEndTime);
    void RequestLoadPCCafePoint(User* user, int pointType);
    void RequestSavePCCafeCount(User* user, int count, int pointType);
    void RequestSaveDailyPCCafePoint(User* user, int point, int checkLevel);
    void RequestSaveVoteSystem(User* user);
    void RequestMakeVariationItem(
        CItem* targetItem,
        CItem* mineralItem,
        CItem* supportItem,
        std::int64_t supportItemAmount,
        User* user,
        ItemOptionKey optionKey
    );
    void RequestCancelVariationItem(CItem* targetItem, User* user, std::int64_t adenaAmount);
    void RequestChangeAttribute(CItem* scrollItem, CItem* targetItem, User* user, AttributeTypeEnum attributeType);
    void RequestShapeShiftItem(
        User* user,
        CItem* extractionItem,
        CItem* targetItem,
        CItem* shapeShiftItem,
        bool extract,
        std::int64_t amount,
        int period
    );
    void RequestShapeShiftItem(
        User* user, CItem* extractionItem, CItem* targetItem, int shapeShiftClassId, std::int64_t amount, int period
    );
    void RequestRestoreShapeShiftItem(User* user, CItem* extractionItem, CItem* targetItem);
    void RequestExpireShapeShiftItem(User* user, CItem* item);
    void RequestLockedItem(CItem* scrollItem, CItem* targetItem, User* user);
    void RequestUnLockedItem(CItem* scrollItem, CItem* targetItem, User* user);
    void RequestEnchantItem(
        CItem* scrollItem,
        CItem* probabilityItem,
        CItem* targetItem,
        int enchantLevel,
        User* user,
        int probability,
        bool lucky
    );
    void RequestEnchantItemFail(
        CItem* scrollItem,
        CItem* probabilityItem,
        CItem* targetItem,
        User* user,
        int crystalClassId,
        int crystalCount,
        int probability
    );
    void RequestSaveAgathionSummonTime(User* user, int agathionId, L2ServerLong summonTime);
    void RequestSaveAgathionDespawnTime(User* user, int agathionId, L2ServerLong despawnTime);
    void RequestInitAgathionElapsedTime(User* user, int agathionId, L2ServerLong elapsedTime);
    void RequestUpdateAgathionElapsedTime(User* user, int agathionId, L2ServerLong elapsedTime);
    void RequestChangeAP(CSPointer<User> user, int abilityPoints);
    void RequestLoadUserPoint(User* user, int pointType);
    void RequestSaveUserPoint(User* user, int pointType, int point, int previousPoint);
    void SendDeletedSkill(User& user, int skillId);
    void SaveSkillUse(
        User& user,
        ReuseDelaySaveReason::Type reason,
        int normalCount,
        int transformedCount,
        int normalLength,
        char* normalBuffer,
        int transformedLength,
        char* transformedBuffer
    ) const;
    void SendDeletedPledgeSkill(User& user, int skillId);
    void RequestDestroyItem(CItem* item, std::int64_t amount, User* user, unsigned int petDatabaseId);
    void RequestUseItem(CItem* item, User* user, std::int64_t count, int unknown) const;
    void PetUseItem(CPet* pet, CItem* item);
    void RequestPickItem(User* user, CItem* item) const;
    void PetGetItem(unsigned int masterSid, CPet& pet, CItem* item);
    void RequestPetDropItem(User* user, CPet* pet, CItem* item, unsigned int petDatabaseId, std::int64_t count);
    void SendSaveCharacterInfo(User* user, bool saveInventory, bool saveWarehouse) const;
    void SaveCharacterSlotInfo(User* user) const;
    void SendSaveItemInfo(User* user) const;
    void SendLogout(User* user) const;
    void RequestLoadItemAgathionEnergy(User* user, int itemServerId, int itemDatabaseId);
    void ReqeustRecentProduct(User* user, int count);
    void ReqeustInterestedProduct(User* user);
    void ReqeustAddInterestedProduct(User* user, int productId);
    void ReqeustDeleteInterestedProduct(User* user, int productId);
    void RequestSaveItemAgathionEnergy(User* user, int itemServerId, int itemDatabaseId, double energy);
    void SaveItemAgathionEnergy(User* user);
    void RequestSaveSummon(User& user);
    void RequestLoadSummonAbnormalStatus(User& user, int summonClassId, int summonIndex);
    void ReqLogoutCharacter(User* user, int characterUseTime, _SYSTEMTIME* localTime);
    void ReqLoginCharacter(User& user);
    void RequestSetCreateDate(User* user, int year, int month, int day, int hour, int minute);
    void RequestResetShopLimit(User* user, int shopIndex);
    void RequestSaveBattleCampHp(CBattleCamp* battleCamp);
    void EvolvePet(
        User* user,
        CEtcItem* petCollar,
        int itemClassId,
        int npcClassId,
        std::int64_t experience,
        int level,
        int meal,
        double x,
        double y,
        int unknown,
        std::int64_t value1,
        std::int64_t value2,
        std::int64_t value3,
        std::int64_t value4
    );
    void EvolvePetWithSameExp(
        User* user,
        CEtcItem* petCollar,
        int itemClassId,
        int npcClassId,
        std::int64_t experience,
        int level,
        int meal,
        int unknown
    );
    void RequestUreadMailCount(User* user);
    void RequestLoadUserPremiumItems(User* user);
    void RequestBuyAirShip(
        User& user, int residenceId, int airShipId, unsigned int itemId, int itemCount, unsigned int price
    );
    void RequestCopyMacro(User* user, const char16_t* characterName);
    void RequestCopyBookmark(User* user, const char16_t* characterName);
    void RequestLoadCardUpdownGameRecord(User& user);
    void SendAcquiredSkill(User& user, int skillId, int skillLevel, int skillSubLevel, bool saveToDatabase);
    void RequestNewbieCheck(User* user);
    void RequestSaveNoblessType(User* user, int noblessType);
    void RequestUpdateSystem(User* user, int packetLength, char* packet);
    void RequestLoadSkill(User* user) const;
    void RequestPartyTelBookMark(User* user, User* target, const char16_t* bookmarkName);
    void RequestMPCCTelBookMark(User* user, User* target, const char16_t* bookmarkName);
    void RequestEstablishFriendship(User* user, User* target);
    void RequestChangeItemData(User* user, int value, int packetLength, char* packet);
    void SendSetBuilder(User& user, int builderLevel);
    void ReloadCharacter(User& user, int value1, int value2, int value3);
    void Ping(User* user);
    void PingOneAdenaDrop(
        User* sender, std::uint32_t correlationId, CItem* item, User* owner, FVector position, SlotType slot, int amount
    );
    void PingNetwork(User* user);
    void SaveInfectedSkills(User& user, int count, int packetLength, char* packet) const;
    void RequestSaveCharExtraInfo(User* user, int socialClass, double carryingWeightPercent) const;
    void LoadInfectedSkills(User* user) const;
    void RequestLoadMenteeList(User& user);
    void RequestLoadMentor(User& user);
    void RequestAddMentee(User& user, int menteeDatabaseId);
    void RequestUpdateMentorStatus(
        User& user, int mentorDatabaseId, int menteeDatabaseId, int type, std::int32_t updateTime
    );
    void RequestUpdateMenteePlaytime(User& user, int mentorDatabaseId, int menteeDatabaseId, std::uint32_t playtime);
    void RequestUpdateMentorPlaytime(User& user, int mentorDatabaseId, std::uint32_t playtime);
    void RequestResetMentorPenalty(User& user, int mentorDatabaseId);
    void RequestSetTutorial(User& user, unsigned int tutorialState);
    void RequestPackageLoadTargetList(User* user) const;
    void RequestWareHouseItemList(User& target, User& requester, int type) const;
    void RequestPackageSendToTarget(User* user, int value1, int value2, int packetLength, char* packet, int value3) const;
    void RequestLoadWareHouseItems(
        User* user,
        int warehouseId,
        CWareHouseWithdraw* warehouse,
        int npcClassId,
        int memoryId,
        WareHouseClass warehouseClass,
        int itemListGetType
    ) const;
    void RequestWithdrawWareHouseItems(
        User* user, int warehouseId, int npcClassId, int itemCount, int packetLength, char* packet
    ) const;
    void RequestDepositWareHouseItems(
        User* user, int warehouseId, int npcClassId, int itemCount, int warehouseItemLimit, int packetLength, char* packet
    ) const;
    void RequestLoadWarehouseEx(int value1, int value2, User* user, int value3, int value4) const;
    void RequestWithdrawItemEx(
        int value1, int value2, User* user, int value3, int value4, int value5, int packetLength, char* packet
    ) const;
    void RequestDepositItemEx(
        int value1, int value2, User* user, int value3, int value4, int packetLength, char* packet
    ) const;
    void UseRecipeItem(User& user, int recipeId);
    void RecipeMakeItems(
        User* user,
        int recipeId,
        int success,
        int critical,
        int assembledCount,
        int packetLength,
        char* packet,
        bool eventLucky
    ) const;
    bool RecipeShopMakeItems(
        User* customer,
        User* maker,
        int recipeId,
        int success,
        std::int64_t makingFee,
        int assembledCount,
        int packetLength,
        char* packet,
        bool eventLucky
    ) const;
    void SaveRecipeBookChapter(User& user, int chapter);
    void DeleteRecipeBookChapter(User& user, int chapter);
    void LoadRecipeBookChapter(User* user);
    void RequestAccountStamp(User* user, int stampType, bool flag, CPacketHelperEx<20480>& packet);
    void RequestAccountStampBuilder(User* user, int value1, int value2);
    void RequestDeleteAccountStampLogBuilder(User* user, int stampId);
    void RequestInitializeBossRecord(User& user);
    void SaveGet5YearGiftTime(User& user, ::_SYSTEMTIME* time);
    void SaveGetBirthdayGiftTime(User& user, ::_SYSTEMTIME* time);
    void GetLastBirthdayGiftDate(User& user);
    void RequestLoadNRMemo(User& user);
    void RequestDeleteNRMemo(User& user, int memoId);
    void RequestLoadDailyQuest(User& user);
    void RequestGetDailyCount(User* user, int countType);
    void RequestSetDailyCount(User* user, int countType);
    void RequestClearDailyCount(User* user, int countType);
    void SetDailyQuest(User& user, int questId, std::int32_t completeTime);
    void RequestAcademyGraduateList(User& user, unsigned int pledgeDatabaseId);
    void RequestLoadUserMonsterBook(User& user);
    void IncreaseUserDuelCount(int value, int worldId);

    void RequestCreateMonRaceMon(int value1, int value2, int value3, int value4);
    void RequestCreateMonRace(
        int value1, int value2, int value3, int value4, int value5, int value6, int value7, int value8, int value9
    );
    void RequestGetMonRaceBet(int value);
    void RequestEndMonRace(
        int value1,
        double value2,
        double value3,
        double value4,
        double value5,
        double value6,
        double value7,
        double value8,
        double value9,
        double value10,
        double value11,
        int value12,
        int value13,
        double value14,
        double value15
    );
    void RequestUpdateMonRaceMon(int value1, int value2, int value3, int value4);
    void RequestGetMonRaceTicket(User* user, int value);
    void RequestGetMonRaceTicketInfo(User* user, int value);
    void RequestDelMonRaceTicket(User* user, int value1, int value2, int value3);
    void RequestGetMonRaceResult(User* user, int value);
    void RequestUpdateMercenary(
        int mercenaryKey, FVector position, FAngle direction, double collisionRadius, double collisionHeight
    ) const;
    void RequestSavePledgeContribution(int residenceId, bool siegeAgit, bool clear, std::map<int, double>* contributions);
    void RequestResetContribution(CResidence& residence);

    void RequestMoveToNextBossRecordRound();
    void RequestPCCafeCouponUsed(
        const int value1, unsigned int value2, const int value3, const int value4, const int value5, const int value6
    );
    void RequestLoadTransferredPledgeMaster();
    void RequestPledgeMasterTransferByNpc(CNPC* npc, User* user, CPledge* pledge, const char16_t* masterName);
    void RequestPledgeMasterTransferCancelByNpc(CNPC* npc, User* user, CPledge* pledge);
    void RequestCreatePledgeByNpc(CNPC* npc, User* user, const char16_t* pledgeName);
    void RequestCreateAcademyByNpc(int npcId, User* user, CPledge* pledge, const char16_t* academyName);
    void RequestQueryCastleSiegeForDismiss(User* user, CPledge* pledge, unsigned int npcClassId);
    void UpdateItemDuration(unsigned int value1, int worldId, unsigned int value3, int value4);
    void UpdateItemDuration(User& user, unsigned int itemDatabaseId, int duration);
    void RequestLoadFortressSiege(CFortress* fortress);
    void RequestLoadFortressSiegeRegistry(CFortress* fortress);
    void RequestPayFortressRent(CFortress* fortress, int fortressOwnerId, int castleOwnerId, std::int64_t rent);
    void RequestSaveFortressState(CFortress* fortress, int state, int stateChangedTime, int stateElapsedTime);
    void RequestSaveFortressSiegeState(int value1, int value2, int value3, int value4, bool value5);
    void RequestOwnFortress(CFortress* fortress, int ownerId, int lastOwnerChangeTime);
    void RequestSaveFortressContractInfo(CFortress* fortress, int contractStatus, int parentCastleId);
    void RequestSaveFortressRewardInfo(CFortress* fortress, int lastRewardTime, int rewardCycle, int treasureLevel);
    void RequestSaveFortressSiegeRegistry(CFortress* fortress, int pledgeCount, int packetLength, char* packet, bool clear);
    void RequestSaveFortressSiegeRegisterPledge(CFortress* fortress, int pledgeId);
    void RequestSaveFortressSiegeUnregisterPledge(CFortress* fortress, int pledgeId);
    void RequestDeletePremiumItem(std::int64_t value);
    void RequestLoadPremiumItemForBatch();
    void SetRimPoint(User& user, int pointType, int updateTime, int resetTime);
    void GetRimPointTop10(int value);
    void RequestSaveDominionRegistry(CDominion&, int, int, int);
    void RequestSaveDominion(CDominion& dominion, int dominionStatus, int residenceState);
    void RequestLoadDominionRegistry(CDominion&);
    void RequestSaveOwnthing(int value1, int value2, int value3, int value4, int value5);
    void RequestSaveDominionSiege(int value1, int value2, int value3, int value4);
    void ReqOwnAgit(CWorldAgit* agit, int ownerId);
    void RequestSetNextAgitBattleTime(CWorldAgit* agit);
    void RequestLoadAllControlTower(CResidence* residence);
    void RequestLoadDoor(CDoor* door);
    void RequestSaveDoorStatus(CDoor* door);
    void RequestInstallResidenceDeco(CResidence& residence, int decoId, int productId, int duration);
    void RequestLoadTeamBattleAgit(CWorldAgit& agit);
    void RequestSetTeamBattleNpcType(CTeamBattleAgit& agit, unsigned int pledgeDatabaseId, int npcType);
    void RequestLoadMiniGameAgit(CWorldAgit& agit);
    void RequestSetMiniGameAgitStatus(CMiniGameAgit& agit, unsigned int status);
    void RequestResetMiniGameAgit(CMiniGameAgit& agit);
    void RequestLoadCompeteAgit(CWorldAgit& agit);
    void RequestNewTeamBattleAgitPledge(
        CTeamBattleAgit* agit, CPledge* pledge, User* user, CNPC* npc, int color, int npcType
    );
    void RequestNewTeamBattleAgitMember(CTeamBattleAgit* agit, User* user, CNPC* npc);
    void RequestUnregisterTeamBattleAgitPledge(CTeamBattleAgit* agit, CPledge* pledge, User* user);
    void RequestUnregisterTeamBattleAgitMember(CTeamBattleAgit* agit, User* user);
    void RequestLoadSiegeAgit(CSiegeAgit* agit);
    void RequestRegisterSiegeAgitPledge(User* user, CSiegeAgit* agit, CPledge* pledge);
    void RequestUnregisterSiegeAgitPledge(User* user, CSiegeAgit* agit, CPledge* pledge);
    void RequestAddMiniGameAgitPledge(unsigned int agitId, CPledge* pledge, int value);
    void RequestDelMiniGameAgitPledge(unsigned int agitId, CPledge* pledge);
    void RequestAddMiniGameAgitMemeber(unsigned int agitId, CPledge* pledge, User* user);
    void RequestSetTeamBattleStatus(CTeamBattleAgit* agit, int status);
    void RequestResetSiegeAgitPledge(CSiegeAgit* agit);
    void RequestResetTeamBattleAgitPledge(CTeamBattleAgit* agit);
    void RequestLoadAllCastle();
    void RequestLoadAllAgit();
    void RequestLoadAllFortress();
    void RequestSaveResidenceStatus(CResidence* residence);
    void RequestResetCastleSiegePledge(CCastle* castle);
    void RequestSaveCastleSiegeElapsedTime(CCastle& castle, int elapsedTime);
    void RequestEndSiege(CCastle& castle, int ownerPledgeId, int attackerScore, int defenderScore);
    void RequestLoadPledgeSiegeKillDeathCount(CCastle* castle);
    void RequestSetPreviousCastleOwnerID(CCastle& castle);
    void RequestSaveControlTowerStatus(CControlTower* tower);
    void RequestSaveAllControlTowerStatus(CResidence* residence);
    void RequestPrepareCastleWar(User* user, CCastle* castle, CPledge* pledge, int operation, int proposeTime);
    void RequestConfirmCastleDefence(
        User* user, CCastle* castle, unsigned int pledgeId, unsigned int operation, int worldId
    );
    void RequestQueryCastleSiegeForRegister(User* user, CCastle* castle, int operation);
    void RequestSetNextCastleSiege(User* user, CCastle* castle, int siegeTime);
    void RequestQueryCastleSiegeForUnregister(User* user, CCastle* castle);
    void ReqOwnCastle(CCastle* castle, int ownerId, int oldOwnerId, int castleType, int worldId, int oldWorldId);
    void ReqWriteTax(CResidence* residence, int kind, int oldRate, int newRate, bool isCastle);
    void RequestSetCastleCampaign(CResidence* residence, int result);
    void RequestSetCastleType(CCastle& castle, int residenceId, int castleType);
    void RequestSetCompeteAgitStatus(CCompeteAgit& competeAgit, unsigned int status);
    void RequestResetCompeteAgitStatus(CCompeteAgit& competeAgit);
    void RequestCompeteAgitWinner(CCompeteAgit& competeAgit, unsigned int pledgeId, bool accepted);
    void RequestSetPreviouseAgitOwnerID(CCompeteAgit& competeAgit, int previousOwnerId);
    void RequestSaveDominionRenamed(User& user, int renamed);
    void RequestDeleteAllDominionRenamed();
    void SendCachePing();

    void RequestCommissionTradeRegTotalCount(int value1, int value2);
    void RequestCommissionTradeDeleteAll(int value);
    void RequestCommissionTradeLoadInfo();
    void RequestCommissionTradeLoadAll(std::int64_t value);
    void RequestCommissionTradeRegister(
        CommissionOwner::Enum owner,
        Commission::Enum commissionType,
        int characterId,
        int itemId,
        SharedUtilsWideChar* itemName,
        std::int64_t itemPrice,
        std::int64_t itemAmount,
        int itemConsumeType,
        std::int64_t commissionFee,
        CommissionExpired::Enum expiredType,
        const char16_t* userName,
        CrystalType crystalType,
        GItemType::Enum generalType,
        CommissionFirst::Enum firstType,
        CommissionDiscountInfo::Enum discountType,
        int premiumItemClassId,
        int premiumItemId,
        std::int64_t premiumItemAmount,
        int enchanted
    );
    void RequestCommissionTradeRegisteredList(int value);
    void RequestCommissionTradeDelete(
        CommissionOwner::Enum owner,
        std::int64_t commissionId,
        int characterId,
        int itemId,
        std::int64_t itemAmount,
        Commission::Enum commissionType
    );
    void RequestCommissionTradeBuyItem(
        int buyerId, std::int64_t commissionId, std::int64_t requestedAmount, CommissionOwner::Enum owner
    );
    void RequestCommissionTradeSearch(
        int requestId,
        int depth,
        Commission::Enum commissionType,
        int itemGeneral,
        CrystalType itemGrade,
        SharedUtilsWideChar* itemName
    );
    void RequestLoadNPCQuestLog(int value1, int value2, int value3);
    void RequestCheckCharName(int value1, int value2, const char16_t* value3);
    void RequestSaveUserJobGroup(User& user);
    void RequestSaveUserJobGroup(int worldId, int value2, int value3);
    void RequestSetDailyResetTime(const char16_t* value1, const char16_t* value2);

    void RequestEndPledgeGame(int value1, int value2, int value3, int value4, int value5);
    void RequestUpdatePledgeGameResult(int value1, int value2, int value3);
    void SetGotItemPledgeGame(int value);
    void RequestIncreasePledgeGameSurrenderCount(int value1, int value2, int value3);
    bool RequestLoadMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type, std::vector<UserMatchingPoint>& users, int flag);
    bool RequestSaveMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type, std::vector<UserMatchingPoint>& users);
    void RequestGetPledgeGameRecord(int value1, int value2);
    void RequestUpdateKalieEvent(int value1, int value2, int value3);
    void RequestUpdateKalieEventForUser(User& user, int remainToken);
    void RequestUpdateKalieEventForUser(CPacketHelperEx<20480>& packet);
    void RequestLoadKalieEventForUser(User& user);
    void RequestPledgeJoinSystemInit(int value);
    void RequestLoadPledgeOnPledgeRecruitBoard();
    void RequestWritePledgeRecruitBoard(
        User* user, int recruitType, const char16_t* title, const char16_t* description, int sortCondition, PledgeType pledgeType
    );
    void RequestModifyPledgeRecruitBoard(
        User* user, int recruitType, const char16_t* title, const char16_t* description, int sortCondition, PledgeType pledgeType
    );
    void RequestDeletePledgeRecruitBoard(User* user);
    void RequestDeletePledgeRecruitBoard(CPledge* pledge);
    void RequestSearchPledgeRecruitBoard(
        User* user,
        const char16_t* pledgeName,
        int minimumLevel,
        int maximumLevel,
        const char16_t* masterName,
        int pledgeType,
        int sortCondition,
        int orderCondition,
        int searchCondition
    );
    void RequestPledgeOnPledgeRecruitBoard(User* user, int pledgeId);
    void RequestApplyInfoOnPledgeWaitingList(User* user);
    void RequestWritePledgeWaitingUserList(User* user, int pledgeId, const char16_t* comment);
    void RequestDeletePledgeWaitingUserList(User* user, int worldId, int characterId);
    void RequestDeletePledgeWaitingUserList(const char16_t* value, int worldId);
    void RequestPledgeWaitingUserList(User* user, int pledgeId);
    void RequestUserOnPledgeWaitingList(User* user, int pledgeId, int characterId);
    void RequestWritePledgeDraftUserList(User* user, int value);
    void RequestDeletePledgeDraftUserList(User* user);
    void RequestPledgeDraftUserList(
        User* user, int minimumLevel, int maximumLevel, int classGroup, const char16_t* name, int sortCondition, int orderCondition
    );
    void RequestPledgeRecruitApplyInfo(User* user);
    void RequestSetPledgeRecruitCount(User* user, CPledge* pledge, int count);
    void RequestPledgeRemainTime(User* user);
    void RequestUserRemainTime(User* user);
    void RequestSavePetitionExternalNotify(const char16_t* value1, unsigned int value2, int value3);
    void RequestResetDormantHistory(int value1, int value2);
    void RequestGetDormantInfo(int value1, int value2, int value3, int value4);
    void RequestLoadWOlympiadMatchCount();

    void RequestLoadPledge(int worldId, unsigned int value, bool flag);
    void RequestLoadPledge(int worldId, const char16_t* value);
    void RequestLoadSubPledge(CPledge& pledge);
    void RequestLoadPledgeSkill(CPledge& pledge);
    void RequestLoadBattleCamp(CResidence* residence, int value);
    void RequestInstallBattleCamp(
        CPledge* pledge, unsigned int residenceId, unsigned int maximumHp, FVector position, int baseType
    );
    void RequestUninstallBattleCampBy(CResidence* residence);
    void RequestUninstallBattleCampBy(CPledge* pledge, int baseType);
    void RequestDeletePledgeByTimer(CPledge* pledge);
    void RequestResetUserNickname(int worldId, unsigned int value);
    void SendAcquiredSkillForAdmin(int value1, int value2, int value3, int value4, bool value5);
    void RequestChangePos(const char16_t* value1, int value2, int value3, int value4);
    void SendSetLoginAnnounce(int value1, const char16_t* value2);
    void RequestSetPledgeInfo(int worldId, unsigned int value2, unsigned int value3, unsigned int value4);
    void RequestUpdateUserPledge(User* user, CPledge* pledge, int updateType, PledgeType pledgeType);
    void RequestReorganizePledgeMember(
        User* user,
        CPledge* pledge,
        int action,
        unsigned int firstCharacterDatabaseId,
        PledgeType pledgeType,
        unsigned int secondCharacterDatabaseId
    );
    void RequestSigninPledge(CSPointer<User> user, CSPointer<CPledge> pledge, PledgeType pledgeType);
    void RequestOustPledgeMember(User* user, CPledge* pledge, const char16_t* memberName) const;
    void RequestSetPledgeCrest(User* user, CPledge* pledge, const unsigned char* data, int size);
    void RequestSetPledgeEmblem(
        User* user,
        CPledge* pledge,
        int pieceIndex,
        int totalFileSize,
        int pieceSize,
        const unsigned char* pieceData,
        bool skipCanRegister
    );
    void RequestSetAllianceCrest(User* user, CAlliance* alliance, const unsigned char* data, int size);
    void RequestJoinAlliance(User* user, CAlliance* alliance, CPledge* masterPledge, CPledge* joiningPledge);
    void RequestDismissAlliance(User* user, CAlliance* alliance, CPledge* masterPledge);
    void RequestWithdrawAlliance(User* user, CAlliance* alliance, CPledge* memberPledge);
    void RequestOustAllianceMemberPledge(User* user, CAlliance* alliance, CPledge* masterPledge, CPledge* oustedPledge);
    void RequestSavePledgeSkill(CPledge& pledge, unsigned int skillId, int skillLevel, std::int32_t expiryTime);
    void RequestDelPledgeSkill(CPledge& pledge, unsigned int skillId);
    void RequestSavePledgePower(CPledge& pledge, int grade, std::uint32_t power);
    void RequestModifyPledgeInfoOnBoard(CPledge* pledge);
    void RequestGetPledgeCrest(User* user, unsigned int crestId, unsigned int worldId);
    void RequestGetAllianceCrest(User* user, unsigned int crestId, unsigned int worldId);
    void RequestGetPledgeEmblem(unsigned int value1, unsigned int value2, unsigned int worldId, unsigned int value4);
    void RequestDeletePledgeEmblem(unsigned int value);
    void RequestModifyPledgeName(unsigned int value1, const char16_t* value2);
    void RequestModifyAllianceName(unsigned int value1, const char16_t* value2);
    void CreatePet(
        User& user, int itemClassId, int npcClassId, std::int64_t experience, int level, double x, double y, int meal
    );
    void CreatePetForAdmin(
        int value1, int value2, int value3, std::int64_t value4, int value5, double value6, double value7, int value8
    );
    void DeletePet(int worldId, int value2, int value3, int value4, int value5, int value6);

    void RequestSaveTimeLimitUsed(const char16_t* value1, int value2, int worldId, int value4, ::_SYSTEMTIME* value5);
    void RequestSaveCastleIncome(
        CResidence& residence, std::int64_t income, std::int64_t temporaryIncome, int isCastle, int reason
    );
    bool RequestBuySeed(
        int manorId, int residenceId, User* user, std::int64_t totalPrice, int itemCount, int dataSize, char* data
    );
    bool RequestProcureCrop(int manorId, int residenceId, User* user, int itemCount, int dataSize, char* data);
    void RequestSaveManorNextSeed(
        int value1,
        int value2,
        int value3,
        int value4,
        int value5,
        int value6,
        int value7,
        int value8,
        int value9,
        int value10
    );
    void RequestSaveManorNowSeed(
        int value1,
        int value2,
        int value3,
        int value4,
        int value5,
        int value6,
        int value7,
        int value8,
        int value9,
        int value10,
        int value11,
        int value12,
        int value13
    );
    void RequestSaveManorInfo(int value1, int value2, int value3, int value4, unsigned char value5, ::_SYSTEMTIME* value6);
    void RequestSetPledgeAnnounce(unsigned int value1, unsigned int value2, int value3, const char16_t* value4);
    void TestRequest(int value, int dataSize, const char* data);
    void RequestLoadBlockedIP(bool load);
    void RequestWritePost(
        User& user,
        const char16_t* receiver,
        int type,
        const char16_t* subject,
        const char16_t* body,
        int itemCount,
        int dataSize,
        char* data,
        std::int64_t money,
        int haveItem,
        std::int64_t fee
    );
    void RequestWritePostBirth(
        int value1, const char16_t* value2, const char16_t* value3, int value4, int value5, int value6, int value7, int value8
    );
    void RequestDeletePostBirth(int value1, int value2, int value3, int value4, int value5);

    void RequestManorMoveCrop(int value1, int value2, int value3, int value4, int dataSize, char* data);
    void RequestLoadAllManor(int value, int dataSize, char* data);
    void RequestUpdateCommPledgeMasterResult(const unsigned char* packet);
    void RequestSavePremiumItemForChar(
        int value1,
        int value2,
        const char16_t* value3,
        int value4,
        int value5,
        const char16_t* value6,
        int value7,
        int dataSize,
        char* data
    );
    void RequestSavePremiumItemsForServer(int value, int dataSize, char* data);
    void RequestGiveItem(int requestType, CPacketHelperEx<20480>* packet);
    void RequestSendGiftPost(CPacketHelperEx<20480>* packet);
    void RequestReceivePost(
        int value1, int value2, int value3, int value4, int value5, int value6, int dataSize, char* data, std::int64_t value9
    );
    void RequestCancelPost(int value1, int value2, int value3, int value4, int value5, int dataSize, char* data);
    void RequestDeletePost(int value1, int dataSize, char* data, int value4, int value5, int value6, int value7);
    void SendCacheVersion();
    bool WaitForCheckVersion(unsigned long timeoutSeconds);
    void SetDeletedInIBServer(int value, int dataSize, char* data) const;

    void RequestLoadAllWOlympiadUser(bool startUp, bool newSeason);
    void RequestUpdateBalthusEventUserInfo(CSPointer<User> user);
    void RequestUpdateBalthusEventUserInfo(protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO& packet);
    bool RequestLoadWOlympiadMatchingPoint(
        const std::vector<int>& worldIds, const std::vector<int>& userDatabaseIds, ::GameRuleType gameRule
    );
    void RequestBuilderCommand(int userSid, const std::vector<std::u16string>& params);
    void LoadReservationAnnounce();
    void SetReservationAnnounce(protocol::RQ_EX_SET_RESERVATION_ANNOUNCE& packet);
    void DelReservationAnnounce(int announceNumber);
    void ShowReservationAnnounce(CUserSocket* socket);
    void RequestUpdateItemAuctionInfoForL2Assist(
        int auctionId,
        int currentItemType,
        std::int64_t currentInitPrice,
        std::int64_t currentItemAmount,
        int nextItemType,
        std::int64_t nextInitPrice,
        std::int64_t nextItemAmount,
        int nextAuctionDisplayStartTime
    );

    void NotifyFinishCheckVersion();

    ::CISocket* m_pDBSockets[2];
    SingleJobFinishSignal* m_pCheckCacheDSignal;
};


extern CDB db;
