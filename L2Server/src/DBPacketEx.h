#pragma once

#include "DBSocket.h"
#include "Shared/src/proto/Stream.h"

#include <cstddef>

class User;

using CDBExCallbackMap = ::FixedSizeCallbackMap<308, CDBCallback>;

extern CDBExCallbackMap g_DBPacketTableEx;


bool ReplyCachePing(CDBSocket* socket, const unsigned char* packet);
bool ReplyShutDownRequestByServerStopper(CDBSocket* socket, const unsigned char* packet);
bool ReplyWritePost(CDBSocket* socket, const unsigned char* packet);
bool ReplySendPost(CDBSocket* socket, const unsigned char* packet);
bool ReplyReceivePost(CDBSocket* socket, const unsigned char* packet);
bool ReplyRejectPost(CDBSocket* socket, const unsigned char* packet);
bool ReplyCancelPost(CDBSocket* socket, const unsigned char* packet);
bool ReplyDeletePost(CDBSocket* socket, const unsigned char* packet);
bool ReplySendBirthdayMail(CDBSocket* socket, const unsigned char* packet);
bool ReplyDeleteBirthdayMail(CDBSocket* socket, const unsigned char* packet);
bool ReplyOpenPost(CDBSocket* socket, const unsigned char* packet);
bool ReplyMakeReceivedPostList(CDBSocket* socket, const unsigned char* packet);
bool ReplyMakeSentPostList(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadSentPostList(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadReceivedPostList(CDBSocket* socket, const unsigned char* packet);
bool ReplyCheckRaidWorldEx(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadAllianceEx(CDBSocket* socket, const unsigned char* packet);
bool ReplySavePremiumItemForChar(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadUserPremiumItemPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyManipulatePremiumItemsPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadPremiumItemsForBatchPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyGetPointtop10Packet(CDBSocket* socket, const unsigned char* packet);
bool ReplySaveBotReportPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadBotReportPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadDominion(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadOwnthing(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadDominionRegistry(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadDominionSiege(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadDominionRenamed(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadBirthInfo(CDBSocket* socket, const unsigned char* packet);
bool ReplyGetLastBirthdayGiftDate(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoad5YearInfo(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadDailyQuestPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyRenewSubJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadNobless(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadPledgeSiegeKillDeathCount(CDBSocket* socket, const unsigned char* packet);
bool TeleportPartyPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadPCCafePointPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplySavePCCafePointPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyNPCPickItemPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyCreateAcademy(CDBSocket* socket, const unsigned char* packet);
bool ReplyCreateSubPledge(CDBSocket* socket, const unsigned char* packet);
bool ReplyUpdateQuest(CDBSocket* socket, const unsigned char* packet);
bool ReplyUpdateSubPledgeMaster(CDBSocket* socket, const unsigned char* packet);
bool ReplyUpgradeSubPledgeMemberCount(CDBSocket* socket, const unsigned char* packet);
bool ReplyGraduateAcademy(CDBSocket* socket, const unsigned char* packet);
bool ReplySetAcademyMaster(CDBSocket* socket, const unsigned char* packet);
bool ReplyPledgeMasterTransfer(CDBSocket* socket, const unsigned char* packet);
bool ReplyPledgeMasterTransferCancel(CDBSocket* socket, const unsigned char* packet);
bool ReplyPCCafeCouponCheck(CDBSocket* socket, const unsigned char* packet);
bool ReplyRenameSubpledge(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadTransferredPledgeMaster(CDBSocket* socket, const unsigned char* packet);
bool ReplyAcademyGraduateList(CDBSocket* socket, const unsigned char* packet);
bool ReplyPickCursedWeapon(CDBSocket* socket, const unsigned char* packet);
bool ReplyReorganizePledgeMember(CDBSocket* socket, const unsigned char* packet);
bool ReplyVariationItem(CDBSocket* socket, const unsigned char* packet);
bool ReplyCancelVariationItem(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadFieldCycleInfo(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadAllAuction(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadAllAuctionBid(CDBSocket* socket, const unsigned char* packet);
bool ReplyManipulateWarehouseItem(CDBSocket* socket, const unsigned char* packet);
bool ReplyConvertItem(CDBSocket* socket, const unsigned char* packet);
bool ReplyChangeMergedName(CDBSocket* socket, const unsigned char* packet);
bool SendTutorial(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadInZoneRestriction(CDBSocket* socket, const unsigned char* packet);
bool SendUISetting(CDBSocket* socket, const unsigned char* packet);
bool ReplySetCastleSiegeDefenceCountEx(CDBSocket* socket, const unsigned char* packet);
bool ReplyWaitingTransferPledgeMasterEx(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadUserPointPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplySaveUserPointPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadPVPPointRestrainPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadLastColorPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadBossRecordRanking(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadBossRecordRound(CDBSocket* socket, const unsigned char* packet);
bool ReplyMoveToNextBossRecordRound(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadSubPledge(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadPledgeSkill(CDBSocket* socket, const unsigned char* packet);
bool ReplySetPledgeEmblem(CDBSocket* socket, const unsigned char* packet);
bool ReplyGetPledgeEmblem(CDBSocket* socket, const unsigned char* packet);
bool ReplyDelPledgeEmblem(CDBSocket* socket, const unsigned char* packet);

namespace PeriodicItemJobHandler {

void OnLoadInfo(User* user, const unsigned char* packet);
void OnExpired(User* user, const unsigned char* packet);

} // namespace PeriodicItemJobHandler

namespace PeriodicShapeShiftedItemJobHandler {

void OnLoadInfo(User* user, const unsigned char* packet);

} // namespace PeriodicShapeShiftedItemJobHandler

namespace PeriodicHennaJobHandler {

void OnLoadInfo(User* user, const unsigned char* packet);
void OnExpired(User* user, const unsigned char* packet);
void OnSetPeriod(User* user, const unsigned char* packet);

} // namespace PeriodicHennaJobHandler

bool ReplyPeriodicItemJob(CDBSocket* socket, const unsigned char* packet);

namespace DBPacketExHandler {

bool ReplyCreateItemAuction(CDBSocket* socket, const unsigned char* packet);
bool ReplyUpdatePledgeNameValue(CDBSocket* socket, const unsigned char* packet);

bool ReplyCheckCharName(CDBSocket* socket, const unsigned char* packet);
bool ReplyAgathionElapsedTimeAck(CDBSocket* socket, const unsigned char* packet);
bool ReplySaveDailyPCCafePoint(CDBSocket* socket, const unsigned char* packet);
bool DeleteBuffByAdmin(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadPledgeGameEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseStartPledgeGameEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseEndPledgeGameEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseUpdatePledgeGameResultEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseGetPledgeGameRecordEx(CDBSocket* socket, const unsigned char* packet);
bool LoadTimeToGiveKarmaPoint(CDBSocket* socket, const unsigned char* packet);
bool ResponseChangeAttributeItem(CDBSocket* socket, const unsigned char* packet);
bool ResponseCancelDualClass(CDBSocket* socket, const unsigned char* packet);
bool ResponseBrCanBuyShopItem(CDBSocket* socket, const unsigned char* packet);
bool ResponseBrSendShopGiftMail(CDBSocket* socket, const unsigned char* packet);
bool ResponseShapeShiftItemEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseShapeShiftItemFixedEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseRestoreShapeShiftItemEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseExpireShapeShiftItemEx(CDBSocket* socket, const unsigned char* packet);
bool ReplyCancelEquipmentAddedEffectAndDropItem(CDBSocket* socket, const unsigned char* packet);
bool ReplyPeriodicShapeShiftedItemJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyPeriodicHennaJob(CDBSocket* socket, const unsigned char* packet);
bool ReplySetQuest(CDBSocket* socket, const unsigned char* packet);
bool ReplyAccountStamp(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadMatchingPoint(CDBSocket* socket, const unsigned char* packet);
bool ReplyCreateDualJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyPremiumKamarokaRound(CDBSocket* socket, const unsigned char* packet);
bool ReplyPremiumKamarokaRankingList(CDBSocket* socket, const unsigned char* packet);
bool ReplyPremiumKamarokaAddNewRecord(CDBSocket* socket, const unsigned char* packet);
bool ReplyPremiumKamarokaPersonalRankInfo(CDBSocket* socket, const unsigned char* packet);
bool ResponseUpdateWeddingEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadWeddingEx(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadFriendDetailInfo(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadDroidNPCAppearance(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_SIGNIN_PLEDGE(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadFactionPoint(CDBSocket* socket, const unsigned char* packet);
bool ReplyUpdateFactionPoint(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadMenteeListEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadMentorEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadMentorPenaltytEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseAddNewMenteeEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseUdateMentorStatusEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadKalieEventEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadKalieEventUserEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadBalthusEventUserInfoEx(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_WOLYMPIAD_LOAD_ALL_USER(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_WOLYMPIAD_GET_MATCH_COUNT(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_WOLYMPIAD_GET_MATCH_RESULT(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadOneDayRewardInfo(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadAutoSoulShotUI(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadUserPledgeBonus(CDBSocket* socket, const unsigned char* packet);
bool ResponseBeautyLoad(CDBSocket* socket, const unsigned char* packet);
bool ResponseMPCCTelBookmarkEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseBuyBeautyItem(CDBSocket* socket, const unsigned char* packet);
bool ResponseBeautyPurchaseList(CDBSocket* socket, const unsigned char* packet);
bool TradeDivideAdena(CDBSocket* socket, const unsigned char* packet);
bool NewManipulateItemPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyAcquirePotentialSkill(CDBSocket* socket, const unsigned char* packet);
bool AcquiredAbilitySkillList(CDBSocket* socket, const unsigned char* packet);
bool ResetAbilitySkill(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_CHANGE_AP(CDBSocket* socket, const unsigned char* packet);
bool ReplyStartTrainingZone(CDBSocket* socket, const unsigned char* packet);
bool ReplyEndTrainingZone(CDBSocket* socket, const unsigned char* packet);
bool ReplyRewardTrainingZone(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadTrainingZone(CDBSocket* socket, const unsigned char* packet);
bool ResponseResetBeautyItem(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadExDropCountEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseBrInterestedProduct(CDBSocket* socket, const unsigned char* packet);
bool ReplyPledgeLoadOnPledgeRecruitBoard(CDBSocket* socket, const unsigned char* packet);
bool ReplyPledgeNotifyTableInit(CDBSocket* socket, const unsigned char* packet);
bool ReplySearchPledgeRecruitBoard(CDBSocket* socket, const unsigned char* packet);
bool ReplyItemCheck(CDBSocket* socket, const unsigned char* packet);
bool ReplyPledgeOnPledgeRecruitBoard(CDBSocket* socket, const unsigned char* packet);
bool ReplyApplyInfoOnPledgeWaitingList(CDBSocket* socket, const unsigned char* packet);
bool ReplyPledgeJoinSystemCode(CDBSocket* socket, const unsigned char* packet);
bool ReplyClearPledgeTimeTable(CDBSocket* socket, const unsigned char* packet);
bool ReplySaveSummonInfectedSkills(CDBSocket* socket, const unsigned char* packet);
bool ReplyDeleteSummon(CDBSocket* socket, const unsigned char* packet);
bool ReplySavePet(CDBSocket* socket, const unsigned char* packet);
bool ReplyRegisterXmasWish(CDBSocket* socket, const unsigned char* packet);
bool ReplyConfirmXmasWish(CDBSocket* socket, const unsigned char* packet);
bool ReplyGetDormantGiftInfo(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeLoadInfo(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeLoadAll(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeRegister(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeDelete(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeBuyItem(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeRegisteredList(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeSearchList(CDBSocket* socket, const unsigned char* packet);
bool ResponseCommissionTradeRegTotalCount(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_LOAD_RESERVATION_ANNOUNCE(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_SHOW_RESERVATION_ANNOUNCE(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_REFRASH_RESERVATION_ANNOUNCE(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadEventData(CDBSocket* socket, const unsigned char* packet);

} // namespace DBPacketExHandler

namespace protocol {


#pragma pack(push, 1)

struct CS_ClientINI final : network::IPacket {
    ~CS_ClientINI() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cType; // +0x08
    std::string strIniStr; // +0x09
};

#pragma pack(pop)


} // namespace protocol
