#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/proto/Stream.h"

#include <cstddef>
#include <cstdint>

class CSocket;
class CUserSocket;

template <typename Packet>
bool UserPacketHandle(CUserSocket* socket, const Packet& packet);

using UserPacketExCallback = bool (*)(CSocket*, const std::uint8_t*);
using UserPacketExCallbackMap = FixedSizeCallbackMap<352, UserPacketExCallback>;

bool DummyUserPacketEx(CSocket* socket, const std::uint8_t* packet);
bool AutoSoulshotEXPacket(CSocket* socket, const std::uint8_t* packet);
bool AskJoinMPCC(CSocket* socket, const std::uint8_t* packet);
bool AcceptJoinMPCC(CSocket* socket, const std::uint8_t* packet);
bool OustFromMPCC(CSocket* socket, const std::uint8_t* packet);
bool EnchantSkillInfo(CSocket* socket, const std::uint8_t* packet);
bool EnchantSkillInfoDetail(CSocket* socket, const std::uint8_t* packet);
bool RequestEnchantSkill(CSocket* socket, const std::uint8_t* packet);
bool ReplyOustFromPartyRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyOustFromMPCCRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyWithdrawPartyRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyWithdrawMPCCRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyDismissPartyRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyDismissMPCCRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyMPCCPartyMasterList(CSocket* socket, const std::uint8_t* packet);
bool ReplyHandOverPartyMaster(CSocket* socket, const std::uint8_t* packet);
bool MPCCShowPartyMembersInfo(CSocket* socket, const std::uint8_t* packet);
bool RequsetViewFishingChampionshipRanking(CSocket* socket, const std::uint8_t* packet);
bool GetBossRecord(CSocket* socket, const std::uint8_t* packet);
bool RequestAntiFreeServer(CSocket* socket, const std::uint8_t* packet);
bool ExitPartyMatchingWaitingRoomPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadUI(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveUI(CSocket* socket, const std::uint8_t* packet);
bool RequestBrGamePoint(CSocket* socket, const std::uint8_t* packet);
bool RequestBrProductList(CSocket* socket, const std::uint8_t* packet);
bool RequestBrProductInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestBrBuyProduct(CSocket* socket, const std::uint8_t* packet);
bool RequestBrRecentProduct(CSocket* socket, const std::uint8_t* packet);
bool RequestBrMinigameLoadScores(CSocket* socket, const std::uint8_t* packet);
bool RequestBrLoadEventTopRankers(CSocket* socket, const std::uint8_t* packet);
bool RequestCharacterBackToLobby(CSocket* socket, const std::uint8_t* packet);
bool RequestSetTutorial(CSocket* socket, const std::uint8_t* packet);
bool RequestBrMinigameInsertScore(CSocket* socket, const std::uint8_t* packet);
bool ReplySeedPhase(CSocket* socket, const std::uint8_t* packet);
bool RequestPCCafeCouponUse(CSocket* socket, const std::uint8_t* packet);
bool ResponseNewInspectorPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestBrSetLectureMark(CSocket* socket, const std::uint8_t* packet);
bool RequestCursedWeaponList(CSocket* socket, const std::uint8_t* packet);
bool RequestExistingCursedWeaponLocation(CSocket* socket, const std::uint8_t* packet);
bool SetPledgeEmblem(CSocket* socket, const std::uint8_t* packet);
bool PledgeEmblem(CSocket* socket, const std::uint8_t* packet);
bool SetAcademyMaster(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgePowerList(CSocket* socket, const std::uint8_t* packet);
bool RequestViewPledgePower(CSocket* socket, const std::uint8_t* packet);
bool RequestSetPledgePowerGrade(CSocket* socket, const std::uint8_t* packet);
bool RequestViewPledgeMemberInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestViewPledgeWarList(CSocket* socket, const std::uint8_t* packet);
bool RequestReorganizePledgeMember(CSocket* socket, const std::uint8_t* packet);
bool RequestDuelAskStart(CSocket* socket, const std::uint8_t* packet);
bool RequestDuelAcceptStart(CSocket* socket, const std::uint8_t* packet);
bool RequestMagicSkillUseGround(CSocket* socket, const std::uint8_t* packet);
bool RequestDuelSurrender(CSocket* socket, const std::uint8_t* packet);
bool RequestShowCastleInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestShowFortressInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestShowAgitInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestShowFortressSiegeInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestFortressMapInfo(CSocket* socket, const std::uint8_t* packet);
bool WriteHeroWords(CSocket* socket, const std::uint8_t* packet);
bool Handle_C_EX_OLYMPIAD_OBSERVER_END(CSocket* socket, const std::uint8_t* packet);
bool Handle_C_EX_GFX_OLYMPIAD(CSocket* socket, const std::uint8_t* packet);
bool AskJoinPartyRoomPacket(CSocket* socket, const std::uint8_t* packet);
bool AnswerJoinPartyRoomPacket(CSocket* socket, const std::uint8_t* packet);
bool ListPartyMatchingWaitingRoomPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestInquiryGoodsInventory(CSocket* socket, const std::uint8_t* packet);
bool RequestRetrieveGoodsInventoryItem(CSocket* socket, const std::uint8_t* packet);
bool RequestItemLink(CSocket* socket, const std::uint8_t* packet);
bool TryToPutItemForVariationCancelPacket(CSocket* socket, const std::uint8_t* packet);
bool ClickVariationCancelButtonPacket(CSocket* socket, const std::uint8_t* packet);
bool ChooseInventoryAttributeItem(CSocket* socket, const std::uint8_t* packet);
bool CannotMoveAnymoreInAirShipPacket(CSocket* socket, const std::uint8_t* packet);
bool MoveToLocationInAirShipPacket(CSocket* socket, const std::uint8_t* packet);
bool CannotAirShipMoveAnymorePacket(CSocket* socket, const std::uint8_t* packet);
bool MoveToLocationAirShipPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestPVPMatchRecord(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeCharName(CSocket* socket, const std::uint8_t* packet);
bool RequestItemAuctionBid(CSocket* socket, const std::uint8_t* packet);
bool RequestItemAuctionInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestPrivateStoreWholeSetMsg(CSocket* socket, const std::uint8_t* packet);
bool RequestBaseAttributeCancel(CSocket* socket, const std::uint8_t* packet);

namespace UserPacketEx {

bool RequestOnReceiveNCGuardClientData(CSocket* socket, const std::uint8_t* packet);
bool UserHardwareInfoPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestSysString(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveClientINI(CSocket* socket, const std::uint8_t* packet);
bool ReqeustCaptchaAnswer(CSocket* socket, const std::uint8_t* packet);
bool RefreshCaptchaImage(CSocket* socket, const std::uint8_t* packet);
bool QueueTicketPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestSSOAuthNToken(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionItemList(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionRegister(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionDelete(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionSearch(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionBuyInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionBuyItem(CSocket* socket, const std::uint8_t* packet);
bool RequestCommissionRegisteredItem(CSocket* socket, const std::uint8_t* packet);

bool RequestRegisterDominion(CSocket* socket, const std::uint8_t* packet);
bool RequestDominionInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestShowPetition(CSocket* socket, const std::uint8_t* packet);
bool RequestShowStepTwo(CSocket* socket, const std::uint8_t* packet);
bool RequestShowStepThree(CSocket* socket, const std::uint8_t* packet);
bool FlyMove(CSocket* socket, const std::uint8_t* packet);
bool FlyMoveStart(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeTarget(CSocket* socket, const std::uint8_t* packet);
bool GetOnShuttlePacket(CSocket* socket, const std::uint8_t* packet);
bool GetOffShuttlePacket(CSocket* socket, const std::uint8_t* packet);
bool MoveToLocationInShuttlePacket(CSocket* socket, const std::uint8_t* packet);
bool CannotMoveAnyMoreInShuttlePacket(CSocket* socket, const std::uint8_t* packet);
bool AgitAuctionCmdPacket(CSocket* socket, const std::uint8_t* packet);

bool ReplyListMPCCWaitingPacket(CSocket* socket, const std::uint8_t* packet);
bool ReplyManageMPCCRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyJoinMPCCRoom(CSocket* socket, const std::uint8_t* packet);
bool RequestPartyMatchingRoomHistory(CSocket* socket, const std::uint8_t* packet);

bool ConnectToRaidServer(CSocket* socket, const std::uint8_t* packet);
bool ReturnFromRaid(CSocket* socket, const std::uint8_t* packet);
bool RequestRaidServerInfo(CSocket* socket, const std::uint8_t* packet);
bool EventMatch(CSocket* socket, const std::uint8_t* packet);
bool PartyLootingModify(CSocket* socket, const std::uint8_t* packet);
bool PartyLootingModifyAgreement(CSocket* socket, const std::uint8_t* packet);
bool Request2ndPasswordCheck(CSocket* socket, const std::uint8_t* packet);
bool Request2ndPasswordVerify(CSocket* socket, const std::uint8_t* packet);
bool Request2ndPasswordReq(CSocket* socket, const std::uint8_t* packet);
bool CheckCharName(CSocket* socket, const std::uint8_t* packet);
bool FriendDetailInfo(CSocket* socket, const std::uint8_t* packet);
bool FriendUpdateMemo(CSocket* socket, const std::uint8_t* packet);
bool BlockUpdateMemo(CSocket* socket, const std::uint8_t* packet);
bool RequestBlockDetailInfo(CSocket* socket, const std::uint8_t* packet);
bool BlockUpsetEnter(CSocket* socket, const std::uint8_t* packet);
bool BlockUpsetVote(CSocket* socket, const std::uint8_t* packet);
bool AnswerAddMentee(CSocket* socket, const std::uint8_t* packet);
bool RequestCancelMentor(CSocket* socket, const std::uint8_t* packet);
bool RequestVoteSystem(CSocket* socket, const std::uint8_t* packet);
bool AnswerCoupleAction(CSocket* socket, const std::uint8_t* packet);
bool AnswerMyMembership(CSocket* socket, const std::uint8_t* packet);
bool QueueTicketLoginPacket(CSocket* socket, const std::uint8_t* packet);
bool NotifyStartMinigamePacket(CSocket* socket, const std::uint8_t* packet);
bool CleftEnter(CSocket* socket, const std::uint8_t* packet);
bool RequestRegisterXmasWish(CSocket* socket, const std::uint8_t* packet);
bool RequestRewardCardCollectSystem(CSocket* socket, const std::uint8_t* packet);
bool RequestStartLuckyGame(CSocket* socket, const std::uint8_t* packet);
bool RequestBettingLuckyGame(CSocket* socket, const std::uint8_t* packet);
bool RequestTrainingZoneLeaving(CSocket* socket, const std::uint8_t* packet);
bool RequesToDoList(CSocket* socket, const std::uint8_t* packet);
bool RequesToDoListHtml(CSocket* socket, const std::uint8_t* packet);
bool RequestOneDayReceiveReward(CSocket* socket, const std::uint8_t* packet);
bool RequestPickCardUpdownGame(CSocket* socket, const std::uint8_t* packet);
bool RequestRetryCardUpdownGame(CSocket* socket, const std::uint8_t* packet);
bool RequestQuitCardUpdownGame(CSocket* socket, const std::uint8_t* packet);
bool SurrenderPledgeWar(CSocket* socket, const std::uint8_t* packet);
bool JoinPledgeByNamePacket(CSocket* socket, const std::uint8_t* packet);
bool Request_C_EX_PLEDGE_SIGNIN(CSocket* socket, const std::uint8_t* packet);
bool RequestCreatePledge(CSocket* socket, const std::uint8_t* packet);
bool CallToChangeClass(CSocket* socket, const std::uint8_t* packet);
bool ChangeToAwakenedClass(CSocket* socket, const std::uint8_t* packet);
bool RequestAcquirePotentailSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestPotentialSkillList(CSocket* socket, const std::uint8_t* packet);
bool RequestResetPotentialSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestAlchemySkillList(CSocket* socket, const std::uint8_t* packet);
bool TryMixCube(CSocket* socket, const std::uint8_t* packet);
bool RequestAlchemyConversion(CSocket* socket, const std::uint8_t* packet);
bool RequestTryEnsoul(CSocket* socket, const std::uint8_t* packet);
bool RequestEnsoulExtraction(CSocket* socket, const std::uint8_t* packet);
bool RequestShowAgitSiegeInfo(CSocket* socket, const std::uint8_t* packet);

bool NotifyPlayStart(CSocket* socket, const std::uint8_t* packet);
bool UserInterfaceInfoPacket(CSocket* socket, const std::uint8_t* packet);
bool ExecutedUIEventsCount(CSocket* socket, const std::uint8_t* packet);
bool DispelPacket(CSocket* socket, const std::uint8_t* packet);
bool EndScenePlayer(CSocket* socket, const std::uint8_t* packet);
bool StopScenePlayer(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeRecruitInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestSearchPledgeRecruitBoard(CSocket* socket, const std::uint8_t* packet);
bool RequestApplyPledgeRecruitBoard(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeOnPledgeRecruitBoard(CSocket* socket, const std::uint8_t* packet);
bool RequestApplyOnPledgeRecruit(CSocket* socket, const std::uint8_t* packet);
bool RequestApplyInfoOnPledgeWaitingList(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeWaitingUserList(CSocket* socket, const std::uint8_t* packet);
bool RequestUserOnPledgeWaitingList(CSocket* socket, const std::uint8_t* packet);
bool RequestAcceptPledgeWaitingUser(CSocket* socket, const std::uint8_t* packet);
bool RequestSearchPledgeDraftUserList(CSocket* socket, const std::uint8_t* packet);
bool RequestApplyPledgeDraftUserList(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeJoinRemainTime(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeRecruitApplyInfo(CSocket* socket, const std::uint8_t* packet);
bool ResponsePetitionExternalNotify(CSocket* socket, const std::uint8_t* packet);
bool RequestStopMove(CSocket* socket, const std::uint8_t* packet);
bool ChangeNicknameColor(CSocket* socket, const std::uint8_t* packet);
bool ResetNicknameColor(CSocket* socket, const std::uint8_t* packet);
bool StartRequestPVPMatchRank(CSocket* socket, const std::uint8_t* packet);
bool StopRequestPVPMatchRank(CSocket* socket, const std::uint8_t* packet);
bool ChangeAttributeCancelPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestCrystalItemCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestCrystalItemInfo(CSocket* socket, const std::uint8_t* packet);
bool TryToPutItemForVariationMakePacket(CSocket* socket, const std::uint8_t* packet);
bool TryToPutIntensiveForVariationMakePacket(CSocket* socket, const std::uint8_t* packet);
bool TryToPutCommissionForVariationMakePacket(CSocket* socket, const std::uint8_t* packet);
bool TryToMakeVariationPacket(CSocket* socket, const std::uint8_t* packet);
bool WithdrawPremiumItem(CSocket* socket, const std::uint8_t* packet);
bool ChangeAttributeItemPacket(CSocket* socket, const std::uint8_t* packet);
bool OnRequestShapeShifting(CSocket* socket, const std::uint8_t* packet);
bool TryToPutShapeShiftingTargetItem(CSocket* socket, const std::uint8_t* packet);
bool TryToPutShapeShiftingExtractionItem(CSocket* socket, const std::uint8_t* packet);
bool TryToPutEnchantTargetItem(CSocket* socket, const std::uint8_t* packet);
bool TryToPutEnchantSupportItem(CSocket* socket, const std::uint8_t* packet);
bool EnchantScrollItemAdd(CSocket* socket, const std::uint8_t* packet);
bool EnchantSupportItemRemove(CSocket* socket, const std::uint8_t* packet);
bool EnchantClose(CSocket* socket, const std::uint8_t* packet);
bool RequestAttendanceStamp(CSocket* socket, const std::uint8_t* packet);
bool RequestVipAttendanceItemList(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeInventorySlot(CSocket* socket, const std::uint8_t* packet);
bool RequestOpenMonsterBook(CSocket* socket, const std::uint8_t* packet);
bool RequestCloseMonsterBook(CSocket* socket, const std::uint8_t* packet);
bool RequestPostItemList(CSocket* socket, const std::uint8_t* packet);
bool WritePost(CSocket* socket, const std::uint8_t* packet);
bool RequestReceivedPostList(CSocket* socket, const std::uint8_t* packet);
bool RequestSentPostList(CSocket* socket, const std::uint8_t* packet);
bool DeleteReceivedPost(CSocket* socket, const std::uint8_t* packet);
bool RequestReceivedPost(CSocket* socket, const std::uint8_t* packet);
bool ReceivePost(CSocket* socket, const std::uint8_t* packet);
bool RejectPost(CSocket* socket, const std::uint8_t* packet);
bool DeleteSentPost(CSocket* socket, const std::uint8_t* packet);
bool RequestSentPost(CSocket* socket, const std::uint8_t* packet);
bool CancelSendPost(CSocket* socket, const std::uint8_t* packet);
bool SendFriendList(CSocket* socket, const std::uint8_t* packet);
bool ShowPostFriend(CSocket* socket, const std::uint8_t* packet);
bool OnCheckSpeedHack(CSocket* socket, const std::uint8_t* packet);
bool RequestNotifyExitBeautyShop(CSocket* socket, const std::uint8_t* packet);
bool AbilityWndOpen(CSocket* socket, const std::uint8_t* packet);
bool AbilityWndClose(CSocket* socket, const std::uint8_t* packet);
bool RequestSetSelectedQuestZondID(CSocket* socket, const std::uint8_t* packet);
bool RequestInteractModify(CSocket* socket, const std::uint8_t* packet);
bool RequestTryEnchantArtifact(CSocket* socket, const std::uint8_t* packet);
bool CancelEnchantItem(CSocket* socket, const std::uint8_t* packet);
bool RefundListReq(CSocket* socket, const std::uint8_t* packet);
bool RequestDivideAdenaStart(CSocket* socket, const std::uint8_t* packet);
bool RequestDivideAdenaCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestDivideAdena(CSocket* socket, const std::uint8_t* packet);
bool BuySellUICloseReq(CSocket* socket, const std::uint8_t* packet);
bool AddPostFriend(CSocket* socket, const std::uint8_t* packet);
bool DeletePostFriend(CSocket* socket, const std::uint8_t* packet);
bool RequestMentorList(CSocket* socket, const std::uint8_t* packet);
bool MenteeWaitingListPacket(CSocket* socket, const std::uint8_t* packet);
bool InzoneWaitngTime(CSocket* socket, const std::uint8_t* packet);
bool CancelShapeShifting(CSocket* socket, const std::uint8_t* packet);
bool RequestJoinPledgeGame(CSocket* socket, const std::uint8_t* packet);
bool RequestCancelJoinPledgeGame(CSocket* socket, const std::uint8_t* packet);
bool RequestLeavePledgeGame(CSocket* socket, const std::uint8_t* packet);
bool RequestObserveListPledgeGame(CSocket* socket, const std::uint8_t* packet);
bool RequestObservePledgeGame(CSocket* socket, const std::uint8_t* packet);
bool RequestExitObservePledgeGame(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeGameHTML(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeGameRecord(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeMissionReward(CSocket* socket, const std::uint8_t* packet);
bool RequesPledgeBonusUIOpen(CSocket* socket, const std::uint8_t* packet);
bool RequesPledgeBonusRewardList(CSocket* socket, const std::uint8_t* packet);
bool RequesPledgeBonusReceiveReward(CSocket* socket, const std::uint8_t* packet);
bool RequestPledgeMissionInfo(CSocket* socket, const std::uint8_t* packet);
bool EnchantOne(CSocket* socket, const std::uint8_t* packet);
bool EnchantOneRemove(CSocket* socket, const std::uint8_t* packet);
bool EnchantTwo(CSocket* socket, const std::uint8_t* packet);
bool EnchantTwoRemove(CSocket* socket, const std::uint8_t* packet);
bool EnchantTry(CSocket* socket, const std::uint8_t* packet);
bool EnchantRetryToPutItems(CSocket* socket, const std::uint8_t* packet);
bool Request_C_EX_DECO_NPC_INFO(CSocket* socket, const std::uint8_t* packet);
bool Request_C_EX_DECO_NPC_SET(CSocket* socket, const std::uint8_t* packet);
bool RequestFactionInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestRaidBossSpawnInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestUpgradeSystem(CSocket* socket, const std::uint8_t* packet);
bool RequestLockedItem(CSocket* socket, const std::uint8_t* packet);
bool RequestUnLockedItem(CSocket* socket, const std::uint8_t* packet);
bool RequestLockedItemCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestUnLockedItemCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestQuestNPCLogList(CSocket* socket, const std::uint8_t* packet);
bool RequestWebSessionID(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeAttributePacket(CSocket* socket, const std::uint8_t* packet);
bool RequestAddMentee(CSocket* socket, const std::uint8_t* packet);
bool RequestKalieToken(CSocket* socket, const std::uint8_t* packet);
bool RequestShowRegistBeauty(CSocket* socket, const std::uint8_t* packet);
bool RequestRegistBeauty(CSocket* socket, const std::uint8_t* packet);
bool RequestShowResetBeauty(CSocket* socket, const std::uint8_t* packet);
bool RequestResetBeauty(CSocket* socket, const std::uint8_t* packet);
bool ShowPCCafeHTML(CSocket* socket, const std::uint8_t* packet);
bool RequestShowCardCollectSystem(CSocket* socket, const std::uint8_t* packet);
bool RequestAttendanceStampReward(CSocket* socket, const std::uint8_t* packet);
bool RequestAutoFishing(CSocket* socket, const std::uint8_t* packet);
bool RequestVipAttendanceCheck(CSocket* socket, const std::uint8_t* packet);
bool RequestBalthusToken(CSocket* socket, const std::uint8_t* packet);
bool RequestMonsterBookReward(CSocket* socket, const std::uint8_t* packet);
bool RequestCardUpdownGameReward(CSocket* socket, const std::uint8_t* packet);
bool RequestItemAuctionStatus(CSocket* socket, const std::uint8_t* packet);
bool RequestBrBuyProductForGift(CSocket* socket, const std::uint8_t* packet);
bool RequestBrAddInterestedProduct(CSocket* socket, const std::uint8_t* packet);
bool RequestBrDeleteInterestedProduct(CSocket* socket, const std::uint8_t* packet);
bool RequestBrExistNewProduct(CSocket* socket, const std::uint8_t* packet);

} // namespace UserPacketEx

extern UserPacketExCallbackMap g_UserPacketEx;


namespace protocol {


#pragma pack(push, 1)

struct S_EX_OLYMPIAD_INFO final : network::IPacket {
    ~S_EX_OLYMPIAD_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    bool bOpen; // +0x08
};

struct S_EX_OLYMPIAD_RECORD final : network::IPacket {
    ~S_EX_OLYMPIAD_RECORD() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nPoint; // +0x08
    std::int32_t nWinCount; // +0x0c
    std::int32_t nLoseCount; // +0x10
    std::int32_t nMatchCount; // +0x14
    std::int32_t nPrevClassType; // +0x18
    std::int32_t nPrevRank; // +0x1c
    std::int32_t nPrevRankCount; // +0x20
    std::int32_t nPrevClassRank; // +0x24
    std::int32_t nPrevClassRankCount; // +0x28
    std::int32_t nPrevClassRankByServer; // +0x2c
    std::int32_t nPrevClassRankByServerCount; // +0x30
    std::int32_t nPrevPoint; // +0x34
    std::int32_t nPrevWinCount; // +0x38
    std::int32_t nPrevLoseCount; // +0x3c
    std::int32_t nPrevGrade; // +0x40
    std::int32_t nSeasonYear; // +0x44
    std::int32_t nSeasonMonth; // +0x48
    bool bMatchOpen; // +0x4c
};

struct S_EX_OLYMPIAD_MATCH_INFO final : network::IPacket {
    ~S_EX_OLYMPIAD_MATCH_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char16_t szTeam1Name[24]; // +0x08
    std::int32_t nTeam1WinCount; // +0x38
    char16_t szTeam2Name[24]; // +0x3c
    std::int32_t nTeam2WinCount; // +0x6c
    std::int32_t nCurrentSet; // +0x70
    std::int32_t nRemainSecond; // +0x74
};

// Current client-bound item-announcement packet. UserPacketEx packs this
// family on one-byte boundaries. The current 0x28-byte xwstring therefore
// begins immediately after cType.
struct S_EX_ITEM_ANNOUNCE final : network::IPacket {
    ~S_EX_ITEM_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cType; // +0x08
    std::u16string sUserName; // +0x09
    std::int32_t nItemClassID; // +0x31
    char cEnchantCount; // +0x35
    std::int32_t nFromItemClassID; // +0x36
};

struct S_EX_COMPLETED_DAILY_QUEST_LIST final : network::IPacket {
    ~S_EX_COMPLETED_DAILY_QUEST_LIST() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<std::int32_t> vQuestIds; // +0x08
};

struct S_EX_COMPLETED_DAILY_QUEST final : network::IPacket {
    ~S_EX_COMPLETED_DAILY_QUEST() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t questId; // +0x08
};

struct C_EX_DECO_NPC_INFO final : network::IPacket {
    ~C_EX_DECO_NPC_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nAgitCID; // +0x08
};

struct DecoNpcInfo {
    char cSlot; // +0x00
    std::int32_t nDecoCID; // +0x01
    std::int32_t nExpire; // +0x05
};

struct S_EX_DECO_NPC_INFO final : network::IPacket {
    ~S_EX_DECO_NPC_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nAgitCID; // +0x08
    std::list<DecoNpcInfo> vDecoNpcs; // +0x0c
    char nResidenceGrade; // +0x24
    std::list<char> vResidenceDomains; // +0x25
};

struct C_EX_DECO_NPC_SET final : network::IPacket {
    ~C_EX_DECO_NPC_SET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nAgitCID; // +0x08
    char cSlot; // +0x0c
    std::int32_t nDecoCID; // +0x0d
};

struct S_EX_DECO_NPC_SET final : network::IPacket {
    ~S_EX_DECO_NPC_SET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cResult; // +0x08
    std::int32_t nAgitCID; // +0x09
    char cSlot; // +0x0d
    std::int32_t nDecoCID; // +0x0e
    std::int32_t nExpire; // +0x12
};

struct C_EX_FACTION_INFO final : network::IPacket {
    ~C_EX_FACTION_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nUserID; // +0x08
    char nRequestType; // +0x0c
};

struct FactionInfo {
    char nFactionType; // +0x00
    char nFactionLevel; // +0x01
    char cIsLevelLimited; // +0x02
    float fFactionRate; // +0x03
};

struct S_EX_FACTION_INFO final : network::IPacket {
    ~S_EX_FACTION_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nUserID; // +0x08
    char nRequestType; // +0x0c
    std::list<FactionInfo> lFactionInfoList; // +0x0d
};

struct S_EX_BALTHUS_EVENT final : network::IPacket {
    ~S_EX_BALTHUS_EVENT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nCurrentState; // +0x08
    std::int32_t nProgress; // +0x0c
    std::int32_t nCurrentRewardItem; // +0x10
    std::int32_t nRewardTokenCount; // +0x14
    std::int32_t nParticipated; // +0x18
    bool bRunning; // +0x1c
    std::int32_t nTime; // +0x1d
};

struct S_EX_FIELD_EVENT_STEP final : network::IPacket {
    ~S_EX_FIELD_EVENT_STEP() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nStep; // +0x08
    std::int32_t nRemainSec; // +0x0c
    std::int32_t nGoalPoint; // +0x10
};

struct S_EX_FIELD_EVENT_POINT final : network::IPacket {
    ~S_EX_FIELD_EVENT_POINT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nPoint; // +0x08
};

struct C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO final : network::IPacket {
    ~C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nSlotOneItemServerID; // +0x08
    std::int32_t nSlotTwoItemServerID; // +0x0c
};

struct C_EX_SWAP_AGATHION_SLOT_ITEMS_INFO final : network::IPacket {
    ~C_EX_SWAP_AGATHION_SLOT_ITEMS_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int64_t SrcSlotBitType; // +0x08
    std::int32_t SrcItemID; // +0x10
    std::int64_t DestSlotBitType; // +0x14
    std::int32_t DestItemID; // +0x1c
};

struct C_EX_RAID_BOSS_SPAWN_INFO final : network::IPacket {
    ~C_EX_RAID_BOSS_SPAWN_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<std::int32_t> vRaidNpcIds; // +0x08
};

struct S_EX_RAID_BOSS_SPAWN_INFO final : network::IPacket {
    ~S_EX_RAID_BOSS_SPAWN_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<std::int32_t> vRaidSpawnNpcIds; // +0x08
};

struct S_EX_RAID_SERVER_INFO final : network::IPacket {
    ~S_EX_RAID_SERVER_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cDimensionRaid; // +0x08
    char cDimensionCastleSiege; // +0x09
};

struct S_EX_ITEM_AUCTION_STATUS final : network::IPacket {
    ~S_EX_ITEM_AUCTION_STATUS() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nPosX; // +0x08
    std::int32_t nPosY; // +0x0c
    std::int32_t nPosZ; // +0x10
    std::int32_t nInzoneId; // +0x14
    char cAuctionStatus; // +0x18
};

struct S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO final : network::IPacket {
    ~S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int64_t nCurrentPrice; // +0x08
};

struct S_EX_PRIVATE_STORE_BUYING_RESULT final : network::IPacket {
    ~S_EX_PRIVATE_STORE_BUYING_RESULT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nItemSid; // +0x08
    std::int64_t nAmount; // +0x0c
    char16_t charName[24]; // +0x14
};

struct S_EX_PRIVATE_STORE_SELLING_RESULT final : network::IPacket {
    ~S_EX_PRIVATE_STORE_SELLING_RESULT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nItemSid; // +0x08
    std::int64_t nAmount; // +0x0c
    char16_t charName[24]; // +0x14
};

struct C_EX_UPGRADE_SYSTEM_REQUEST final : network::IPacket {
    ~C_EX_UPGRADE_SYSTEM_REQUEST() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nItemSid; // +0x08
    std::int32_t nUpgradeSystemID; // +0x0c
};

struct S_EX_SHOW_UPGRADE_SYSTEM final : network::IPacket {
    ~S_EX_SHOW_UPGRADE_SYSTEM() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int16_t nFlag; // +0x08
};

struct S_EX_UPGRADE_SYSTEM_RESULT final : network::IPacket {
    ~S_EX_UPGRADE_SYSTEM_RESULT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int16_t nResult; // +0x08
    std::int32_t nID; // +0x0a
};

struct PkPledgeContributionRankInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nRank; // +0x00
    char16_t szName[25]; // +0x04
    std::int32_t nPledgeType; // +0x36
    std::int32_t nPeriod; // +0x3a
    std::int32_t nTotal; // +0x3e
};

struct C_EX_PLEDGE_CONTRIBUTION_INFO final : network::IPacket {
    ~C_EX_PLEDGE_CONTRIBUTION_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cReserved; // +0x08
};

struct C_EX_PLEDGE_CONTRIBUTION_RANK final : network::IPacket {
    ~C_EX_PLEDGE_CONTRIBUTION_RANK() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    bool bCurrent; // +0x08
};

struct S_EX_PLEDGE_CONTRIBUTION_INFO final : network::IPacket {
    ~S_EX_PLEDGE_CONTRIBUTION_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nPeriod; // +0x08
    std::int32_t nTotal; // +0x0c
    std::int32_t nTotalForNextReward; // +0x10
    std::int32_t nItemClassID; // +0x14
    std::int32_t nAmount; // +0x18
    std::int32_t nNameValue; // +0x1c
};

struct S_EX_PLEDGE_CONTRIBUTION_RANK final : network::IPacket {
    ~S_EX_PLEDGE_CONTRIBUTION_RANK() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    bool bCurrent; // +0x08
    std::list<PkPledgeContributionRankInfo> vMembers; // +0x09
};

struct PkPledgeRaidRankInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nWorldId_; // +0x00
    char16_t szPledgeName_[25]; // +0x04
    std::int32_t nRank_; // +0x36
    std::int64_t nPoint_; // +0x3a
};

struct S_EX_PLEDGE_RAID_INFO final : network::IPacket {
    ~S_EX_PLEDGE_RAID_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    bool bIsCurrent_; // +0x08
    char16_t szPledgeName_[25]; // +0x09
    std::int32_t nRank_; // +0x3b
    std::int32_t nTier_; // +0x3f
    std::int64_t nPoint_; // +0x43
    std::int32_t nRewardReason_; // +0x4b
    std::int32_t nSeason_; // +0x4f
    std::int16_t hStartTimeYear_; // +0x53
    std::int16_t hStartTimeMonth_; // +0x55
    std::int16_t hStartTimeDay_; // +0x57
    std::int16_t hEndTimeYear_; // +0x59
    std::int16_t hEndTimeMonth_; // +0x5b
    std::int16_t hEndTimeDay_; // +0x5d
};

struct S_EX_PLEDGE_RAID_RANK final : network::IPacket {
    ~S_EX_PLEDGE_RAID_RANK() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    bool bIsCurrent_; // +0x08
    std::list<PkPledgeRaidRankInfo> vList_; // +0x09
};

struct C_EX_PLEDGE_CONTRIBUTION_REWARD final : network::IPacket {
    ~C_EX_PLEDGE_CONTRIBUTION_REWARD() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nType; // +0x08
};

struct S_EX_PLEDGE_CONTRIBUTION_REWARD final : network::IPacket {
    ~S_EX_PLEDGE_CONTRIBUTION_REWARD() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nResult; // +0x08
};

struct C_EX_PLEDGE_LEVEL_UP final : network::IPacket {
    ~C_EX_PLEDGE_LEVEL_UP() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cReserved; // +0x08
};

struct S_EX_PLEDGE_LEVEL_UP final : network::IPacket {
    ~S_EX_PLEDGE_LEVEL_UP() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nResult; // +0x08
};

struct C_EX_PLEDGE_MISSION_REWARD final : network::IPacket {
    ~C_EX_PLEDGE_MISSION_REWARD() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nMissionId_; // +0x08
};

struct S_EX_PLEDGE_MISSION_REWARD_COUNT final : network::IPacket {
    ~S_EX_PLEDGE_MISSION_REWARD_COUNT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nCount_; // +0x08
    std::int32_t nMaxCount_; // +0x0c
};

struct S_EX_PLEDGE_SHOW_INFO_UPDATE final : network::IPacket {
    ~S_EX_PLEDGE_SHOW_INFO_UPDATE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nPledgeSId; // +0x08
    std::int32_t nNextLevelNameValue; // +0x0c
    std::int32_t nMaster; // +0x10
    std::int32_t nElite; // +0x14
};

struct PkPledgeMissionInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nMissionId_; // +0x00
    std::int32_t nCount_; // +0x04
    char nState_; // +0x08
};

struct S_EX_PLEDGE_MISSION_INFO final : network::IPacket {
    ~S_EX_PLEDGE_MISSION_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<PkPledgeMissionInfo> vList_; // +0x08
};

struct C_EX_PLEDGE_MASTERY_INFO final : network::IPacket {
    ~C_EX_PLEDGE_MASTERY_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cReserved; // +0x08
};

struct PkPledgeMasteryInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nID; // +0x00
    std::int32_t nPoint; // +0x04
    char nStatus; // +0x08
};

struct S_EX_PLEDGE_MASTERY_INFO final : network::IPacket {
    ~S_EX_PLEDGE_MASTERY_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nTotalPoint; // +0x08
    std::int32_t nMaxPoint; // +0x0c
    std::list<PkPledgeMasteryInfo> vMasteries; // +0x10
};

struct C_EX_PLEDGE_MASTERY_SET final : network::IPacket {
    ~C_EX_PLEDGE_MASTERY_SET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nID; // +0x08
};

struct S_EX_PLEDGE_MASTERY_SET final : network::IPacket {
    ~S_EX_PLEDGE_MASTERY_SET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nResult; // +0x08
};

struct C_EX_PLEDGE_MASTERY_RESET final : network::IPacket {
    ~C_EX_PLEDGE_MASTERY_RESET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cReserved; // +0x08
};

struct S_EX_PLEDGE_MASTERY_RESET final : network::IPacket {
    ~S_EX_PLEDGE_MASTERY_RESET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nResult; // +0x08
};

struct C_EX_PLEDGE_SKILL_INFO final : network::IPacket {
    ~C_EX_PLEDGE_SKILL_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nSkillID; // +0x08
    std::int32_t nSkillLevel; // +0x0c
};

struct S_EX_PLEDGE_SKILL_INFO final : network::IPacket {
    ~S_EX_PLEDGE_SKILL_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nSkillID; // +0x08
    std::int32_t nSkillLevel; // +0x0c
    std::int32_t nRemainSecond; // +0x10
    char nStatus; // +0x14
};

struct C_EX_PLEDGE_SKILL_ACTIVATE final : network::IPacket {
    ~C_EX_PLEDGE_SKILL_ACTIVATE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nSkillID; // +0x08
    std::int32_t nSkillLevel; // +0x0c
};

struct S_EX_PLEDGE_SKILL_ACTIVATE final : network::IPacket {
    ~S_EX_PLEDGE_SKILL_ACTIVATE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nResult; // +0x08
};

struct PkPledgeItemInfo final {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char cStatus; // +0x00
    std::int64_t nSellPriceAdena; // +0x01
    std::int32_t nSellPriceNameValue; // +0x09
    char nLevel; // +0x0d
    char nMasteryID; // +0x0e
    std::int64_t nActivePriceAdena; // +0x0f
    std::int32_t nActivePriceNameValue; // +0x17
    std::int32_t nRemainExpirySecond; // +0x1b
    std::int32_t nRemainResetSecond; // +0x1f
    std::int16_t nRemainAmount; // +0x23
    std::int16_t nRefillAmount; // +0x25
};

struct C_EX_PLEDGE_ITEM_LIST final : network::IPacket {
    ~C_EX_PLEDGE_ITEM_LIST() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cReserved; // +0x08
};

struct C_EX_PLEDGE_ITEM_INFO final : network::IPacket {
    ~C_EX_PLEDGE_ITEM_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nItemClassID; // +0x08
};

struct C_EX_PLEDGE_ITEM_BUY final : network::IPacket {
    ~C_EX_PLEDGE_ITEM_BUY() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nItemClassID; // +0x08
    std::int32_t nItemAmount; // +0x0c
};

struct S_EX_PLEDGE_ITEM_BUY final : network::IPacket {
    ~S_EX_PLEDGE_ITEM_BUY() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nResult; // +0x08
};

struct C_EX_PLEDGE_ITEM_ACTIVATE final : network::IPacket {
    ~C_EX_PLEDGE_ITEM_ACTIVATE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nItemClassId; // +0x08
};

struct S_EX_PLEDGE_ITEM_ACTIVATE final : network::IPacket {
    ~S_EX_PLEDGE_ITEM_ACTIVATE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nResult; // +0x08
};

struct C_EX_PLEDGE_ANNOUNCE final : network::IPacket {
    ~C_EX_PLEDGE_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char cReserved; // +0x08
};

struct S_EX_PLEDGE_ANNOUNCE final : network::IPacket {
    ~S_EX_PLEDGE_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::u16string content; // +0x08
    bool bShow; // +0x30
};

struct C_EX_PLEDGE_ANNOUNCE_SET final : network::IPacket {
    ~C_EX_PLEDGE_ANNOUNCE_SET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::u16string content; // +0x08
    bool bShow; // +0x30
};

struct S_EX_PLEDGE_ANNOUNCE_SET final : network::IPacket {
    ~S_EX_PLEDGE_ANNOUNCE_SET() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    bool bSuccess; // +0x08
};

struct AgitSiegeInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nAgitId; // +0x00
    std::int32_t nSiegeDate; // +0x04
    std::u16string wstrOwnerPledgeName; // +0x08
    std::u16string wstrOwnerPledgeMasterName; // +0x30
    char cAgitType; // +0x58
    char cSiegeState; // +0x59
};

struct S_EX_SHOW_AGIT_SIEGE_INFO final : network::IPacket {
    ~S_EX_SHOW_AGIT_SIEGE_INFO() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<AgitSiegeInfo> vAgitInfos; // +0x08
};

struct C_EX_REQUEST_LOCKED_ITEM final : network::IPacket {
    ~C_EX_REQUEST_LOCKED_ITEM() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nTargetItemId; // +0x08
};

struct C_EX_REQUEST_UNLOCKED_ITEM final : network::IPacket {
    ~C_EX_REQUEST_UNLOCKED_ITEM() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nTargetItemId; // +0x08
};

struct C_EX_LOCKED_ITEM_CANCEL final : network::IPacket {
    ~C_EX_LOCKED_ITEM_CANCEL() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char dummy; // +0x08
};

struct C_EX_UNLOCKED_ITEM_CANCEL final : network::IPacket {
    ~C_EX_UNLOCKED_ITEM_CANCEL() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char dummy; // +0x08
};

struct S_EX_CHOOSE_LOCKED_ITEM final : network::IPacket {
    ~S_EX_CHOOSE_LOCKED_ITEM() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nScrollItemClassID; // +0x08
};

// Current client-bound locked-item result packet. The two one-byte fields
// are serialized and in declaration order.
struct S_EX_LOCKED_RESULT final : network::IPacket {
    ~S_EX_LOCKED_RESULT() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char lock; // +0x08
    char result; // +0x09
};

struct ItemServerInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nItemServerId; // +0x00
    std::int64_t nAmount; // +0x04
};

struct ItemInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nItemClassId; // +0x00
    std::int64_t nAmount; // +0x04
};

struct ItemRollyResultInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char nType; // +0x00
    std::int32_t nItemClassId; // +0x01
    std::int64_t nAmount; // +0x05
};

struct AlchemySkillServerInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nSkillId; // +0x00
    std::int32_t nLev; // +0x04
    double dRemainTime; // +0x08
    char type; // +0x10
};

struct AlchemySkillClientInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nSkillId; // +0x00
    std::int32_t nLev; // +0x04
};

struct C_AlchemyTryMixCube final : network::IPacket {
    ~C_AlchemyTryMixCube() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<ItemServerInfo> itemInfoList; // +0x08
};

struct S_AlchemyTryMixCube final : network::IPacket {
    ~S_AlchemyTryMixCube() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nSuccessInfo; // +0x08
    std::list<ItemRollyResultInfo> resultList; // +0x09
};

struct S_AlchemySkillList final : network::IPacket {
    ~S_AlchemySkillList() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::list<AlchemySkillServerInfo> skillInfoList; // +0x08
};

struct C_AlchemyConversion final : network::IPacket {
    ~C_AlchemyConversion() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::int32_t nTryCount; // +0x08
    AlchemySkillClientInfo info; // +0x0c
    std::list<ItemInfo> recipeList; // +0x14
};

struct S_AlchemyConversion final : network::IPacket {
    ~S_AlchemyConversion() override = default;

    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    char nSuccessInfo; // +0x08
    std::int32_t nSuccessCount; // +0x09
    std::int32_t nFailCount; // +0x0d
};

#pragma pack(pop)


} // namespace protocol
