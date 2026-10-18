#pragma once

#include "Shared/src/L2Time.h"
#include "Shared/src/MemorySub.h"

#include "Protocol.h"
#include "Shared/src/Socket.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace l2npc {

class CServerSocket;

using L2ServerCallback = bool (*)(CSocket*, const std::uint8_t*);

using CServerPacketBuffer = CIOBufferEx<20480>;

using L2ServerCallbackMap = FixedSizeCallbackMap<200, L2ServerCallback>;

extern L2ServerCallbackMap gPacketTableFromServer;


// The target exports these as free __cdecl callbacks with the signature
// bool(CSocket*, const unsigned char*). The portable build preserves that
// raw packet boundary. Each callback advances the pointer with the target's
// These current-image bodies have no surviving symbols, but the adjacent
// matching PDB identifies their original serversocket.cpp callback names.
bool FortressMakerEvent(CSocket* socket,
    const unsigned char* packet); // EXE 0x0051c998
bool FortressAIEvent(CSocket* socket,
    const unsigned char* packet); // EXE 0x0051ca38
bool PledgeAIEvent(CSocket* socket,
    const unsigned char* packet); // EXE 0x0051cba8
bool InstantZoneMakerEvent(CSocket* socket,
    const unsigned char* packet); // EXE 0x0051ccc0
bool InstantZoneAIEvent(CSocket* socket,
    const unsigned char* packet); // EXE 0x0051cd38
// Original PDB symbol in serversocket.obj. Shipping EXE 0x0052d4cc.
bool BuilderCmdShowPacket(CSocket* socket, const unsigned char* packet);

bool VersionPacket(CSocket* socket, const unsigned char* packet);
bool CreatePacket(CSocket* socket, const unsigned char* packet);
bool RegisterPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool UnRegisterPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool StartPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool EndPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool CheckRegisterPartyResult(CSocket* socket, const unsigned char* packet);
bool RegisterResurrectionTowerResult(CSocket* socket, const unsigned char* packet);
bool CheckRegisterUserResult(CSocket* socket, const unsigned char* packet);
bool IsUserPVPMatchingResult(CSocket* socket, const unsigned char* packet);
bool RegisterUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool UnregisterUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool RegisterUserResurrectionTowerResult(CSocket* socket, const unsigned char* packet);
bool killedUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool GetRankUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool LetInUserPVPMatch(CSocket* socket, const unsigned char* packet);
bool StartUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool WithdrawUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool EndUserPVPMatchResult(CSocket* socket, const unsigned char* packet);
bool ChangeNPCStatusResult(CSocket* socket, const unsigned char* packet);
bool RegisterDominionSiege(CSocket* socket, const unsigned char* packet);
bool DeclareLord(CSocket* socket, const unsigned char* packet);
bool GetRegistrySize(CSocket* socket, const unsigned char* packet);
bool DominionSiegeEvent(CSocket* socket, const unsigned char* packet);
bool UserLevelUpEvent(CSocket* socket, const unsigned char* packet);
bool DominionScriptEvent(CSocket* socket, const unsigned char* packet);
bool DieSet(CSocket* socket, const unsigned char* packet);
bool RestartSet(CSocket* socket, const unsigned char* packet);
bool AirShipJobEvent(CSocket* socket, const unsigned char* packet);
bool SetTopDesire(CSocket* socket, const unsigned char* packet);
bool NpcPing(CSocket* socket, const unsigned char* packet);
bool ServerPing(CSocket* socket, const unsigned char* packet);
bool SiegeEvent(CSocket* socket, const unsigned char* packet);
bool HolyThingSpelled(CSocket* socket, const unsigned char* packet);
bool PingForCastleSiege(CSocket* socket, const unsigned char* packet);
bool DeleteHatePacket(CSocket* socket, const unsigned char* packet);
bool DeleteHateOfUserPacket(CSocket* socket, const unsigned char* packet);
bool RandomizeHatePacket(CSocket* socket, const unsigned char* packet);
bool ConfusePacket(CSocket* socket, const unsigned char* packet);
bool RunAwayPacket(CSocket* socket, const unsigned char* packet);
bool ContributionWinnerPledgePacket(CSocket* socket, const unsigned char* packet);
bool GeneralEventPacket(CSocket* socket, const unsigned char* packet);
bool AgitEventPacket(CSocket* socket, const unsigned char* packet);
bool TutorialMenuSelectEventPacket(CSocket* socket, const unsigned char* packet);
bool TutorialMarkClicked(CSocket* socket, const unsigned char* packet);
bool TutorialEvent(CSocket* socket, const unsigned char* packet);
bool ManageCastleSiege(CSocket* socket, const unsigned char* packet);
bool PetAction(CSocket* socket, const unsigned char* packet);
bool RequestNPCInfo(CSocket* socket, const unsigned char* packet);
bool DoorHpLevelInfo(CSocket* socket, const unsigned char* packet);
bool ConrolTowerLevelInfo(CSocket* socket, const unsigned char* packet);
bool NpcDoNothing(CSocket* socket, const unsigned char* packet);
bool BroadcastScriptEvent(CSocket* socket, const unsigned char* packet);
bool TeamBattleAgitEvent(CSocket* socket, const unsigned char* packet);
bool BuyLottoTicketEvent(CSocket* socket, const unsigned char* packet);
bool RegisterTeamBattleEvent(CSocket* socket, const unsigned char* packet);
bool QueryTeamBattleNpcEvent(CSocket* socket, const unsigned char* packet);
bool QueryTeamBattlePledgeEvent(CSocket* socket, const unsigned char* packet);
bool GetBattleRoyalPledgeList(CSocket* socket, const unsigned char* packet);
bool GetSubJobList(CSocket* socket, const unsigned char* packet);
bool CreateSubJob(CSocket* socket, const unsigned char* packet);
bool ChangeSubJob(CSocket* socket, const unsigned char* packet);
bool RenewSubJob(CSocket* socket, const unsigned char* packet);
bool SetAgitDecoReturned(CSocket* socket, const unsigned char* packet);
bool ResetAgitDecoReturned(CSocket* socket, const unsigned char* packet);
bool NpcAddTimer(CSocket* socket, const unsigned char* packet);
bool OnSSQSysEvent(CSocket* socket, const unsigned char* packet);
bool GetTerritoryInfo(CSocket* socket, const unsigned char* packet);
bool EnchantSkillRequest(CSocket* socket, const unsigned char* packet);
bool OneEnchantSkillSelect(CSocket* socket, const unsigned char* packet);
bool SetNoblessReturned(CSocket* socket, const unsigned char* packet);
bool SetHeroReturned(CSocket* socket, const unsigned char* packet);
bool DeletePreviousOlympiadPointReturned(CSocket* socket, const unsigned char* packet);
bool DeleteOlympiadTradePointReturned(CSocket* socket, const unsigned char* packet);
bool CreateAcademy(CSocket* socket, const unsigned char* packet);
bool MiniGameAgitEvent(CSocket* socket, const unsigned char* packet);
bool CreateSubPledge(CSocket* socket, const unsigned char* packet);
bool CheckCursedUserReturned(CSocket* socket, const unsigned char* packet);
bool UpdateSubPledgeMaster(CSocket* socket, const unsigned char* packet);
bool MPCCTeleported(CSocket* socket, const unsigned char* packet);
bool PledgeMasterTransfer(CSocket* socket, const unsigned char* packet);
bool PledgeMasterTransferCancel(CSocket* socket, const unsigned char* packet);
bool TBCheckMemberRegisterStatus(CSocket* socket, const unsigned char* packet);
bool RenameSubPledge(CSocket* socket, const unsigned char* packet);
bool AuctionAgitGetCostInfoReturned(CSocket* socket, const unsigned char* packet);
bool AbnormalStatusChangedPacket(CSocket* socket, const unsigned char* packet);
bool EscapeFromBetraying(CSocket* socket, const unsigned char* packet);
bool TeleportToUserRes(CSocket* socket, const unsigned char* packet);
bool TrapCreatureStepInOut(CSocket* socket, const unsigned char* packet);
bool TrapInteracted(CSocket* socket, const unsigned char* packet);
bool FieldCycleChanged(CSocket* socket, const unsigned char* packet);
bool TransterHate(CSocket* socket, const unsigned char* packet);
bool OneEnchantSkillSelectDetail(CSocket* socket, const unsigned char* packet);
bool UpgradeSubPledgeMemberCount(CSocket* socket, const unsigned char* packet);
bool OlympiadFieldStepChanged(CSocket* socket, const unsigned char* packet);
bool FireCannon(CSocket* socket, const unsigned char* packet);
bool CreateNpcToWinner(CSocket* socket, const unsigned char* packet);
bool NpcCreateCow(CSocket* socket, const unsigned char* packet);
bool FieldCycleStepExpired(CSocket* socket, const unsigned char* packet);
bool GetEventScheduledTime(CSocket* socket, const unsigned char* packet);
bool LoadNPCAI(CSocket* socket, const unsigned char* packet);
bool GetEventInfo(CSocket* socket, const unsigned char* packet);
bool GetDBSavingValue(CSocket* socket, const unsigned char* packet);
bool RegisterOlympiadMaker(CSocket* socket, const unsigned char* packet);
bool OlympiadMatchResult(CSocket* socket, const unsigned char* packet);
bool SetAbsoluteDesire(CSocket* socket, const unsigned char* packet);
bool DummyPacket(CSocket* socket, const unsigned char* packet);
bool SetEventValue(CSocket* socket, const unsigned char* packet);
bool StartUseSkill(CSocket* socket, const unsigned char* packet);
bool RegisterAIStatus(CSocket* socket, const unsigned char* packet);
bool ResetProfile(CSocket* socket, const unsigned char* packet);
bool ReportProfile(CSocket* socket, const unsigned char* packet);
bool RegisterPledgeGameMaker(CSocket* socket, const unsigned char* packet);
bool CreateDualJob(CSocket* socket, const unsigned char* packet);
bool FishingEvent(CSocket* socket, const unsigned char* packet);
bool EnterWorldPacket(CSocket* socket, const unsigned char* packet);
bool MovePacket(CSocket* socket, const unsigned char* packet);
bool AttackPacket(CSocket* socket, const unsigned char* packet);
bool DiePacket(CSocket* socket, const unsigned char* packet);
bool TalkPacket(CSocket* socket, const unsigned char* packet);
bool QuestAcceptedEventPacket(CSocket* socket, const unsigned char* packet);
bool MenuSelectEventPacket(CSocket* socket, const unsigned char* packet);
bool QuestChoice(CSocket* socket, const unsigned char* packet);
bool TalkSelect(CSocket* socket, const unsigned char* packet);
bool LevelUpPledge(CSocket* socket, const unsigned char* packet);
bool ManorMenuSelectEventPacket(CSocket* socket, const unsigned char* packet);
bool TeleportRequestedEventPacket(CSocket* socket, const unsigned char* packet);
bool BuyRequestedEventPacket(CSocket* socket, const unsigned char* packet);
bool SellRequestedEventPacket(CSocket* socket, const unsigned char* packet);
bool ResurrectionPacket(CSocket* socket, const unsigned char* packet);
bool CheckMapIndex(CSocket* socket, const unsigned char* packet);
bool RequestNPCSpawn(CSocket* socket, const unsigned char* packet);
bool SeeCreature(CSocket* socket, const unsigned char* packet);
bool OneSkillSelected(CSocket* socket, const unsigned char* packet);
bool LearnSkillRequest(CSocket* socket, const unsigned char* packet);
bool SkillQuestAccept(CSocket* socket, const unsigned char* packet);
bool SeeItem(CSocket* socket, const unsigned char* packet);
bool ClassChangeRequest(CSocket* socket, const unsigned char* packet);
bool TimePacket(CSocket* socket, const unsigned char* packet);
bool DeleteNPCPacket(CSocket* socket, const unsigned char* packet);
bool FinishUseSkill(CSocket* socket, const unsigned char* packet);
bool ClanAttack(CSocket* socket, const unsigned char* packet);
bool ClanSpelled(CSocket* socket, const unsigned char* packet);
bool ClanDiedEvent(CSocket* socket, const unsigned char* packet);
bool SetAI(CSocket* socket, const unsigned char* packet);
bool TimeHour(CSocket* socket, const unsigned char* packet);
bool DesireManipulation(CSocket* socket, const unsigned char* packet);
bool CreatePledge(CSocket* socket, const unsigned char* packet);
bool DismissPledge(CSocket* socket, const unsigned char* packet);
bool RevivePledge(CSocket* socket, const unsigned char* packet);
bool CreateAlliance(CSocket* socket, const unsigned char* packet);

// CDBNPCReset is declared beside CServerSocket in the original
// serversocket.cpp. Every GeneralEvent(-1) allocates an independent one-shot
// timer, while all instances write the same process-wide boolean.
extern bool g_bDBNPCReset;

class CDBNPCReset final : public CIOObject {
public:
    void TimerExpired(int timerId) override;
};


// Exact current instantiations. Shared/L2Time.h owns their function-local static
// instances at target addresses 0x019ef390 and 0x019f0700.
using ServerToNpcPacketStat = CPacketStat<200, 200>;
using NpcToServerPacketStat = CPacketStat<312, 254>;

// CServerSocket from ServerSocket.cpp. The exact target object ends at 0x110.
// portable process-owner notifications are kept out of it.
class CServerSocket : public CSocket {
public:
    // Current EXE 0x005221d4 and exact donor PDB:
    // CServerSocket(unsigned __int64).
    explicit CServerSocket(std::uint64_t socket);
    ~CServerSocket() override;

    L2_DECLARE_OBJECT_TYPE(CServerSocket)

    static CSocket* Allocate(std::uint64_t socket);

    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    bool CheckPendingWrite() override;
    void OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const override;
    // Intro virtual compiled by serversocket.obj. Current EXE's final
    // CServerSocket vtable slot points to the true-returning ICF body
    // at 0x00516b64.
    virtual bool IsServerSocket();
    void BroadcastSystemMessageW(char* format, ...);
    void TimerExpired(int timerId) override;
    static bool SkillUsePacket(CSocket* socket, const unsigned char* packet);
    static bool UserObjectEvent(CSocket* socket, const unsigned char* packet);
    static bool NpcPosEvent(CSocket* socket, const unsigned char* packet);
    static bool NpcPosesEvent(CSocket* socket, const unsigned char* packet);
    static bool CleftStateChangedEvent(CSocket* socket, const unsigned char* packet);
    static bool BlockUpsetStartedEvent(CSocket* socket, const unsigned char* packet);
    static bool BlockUpsetFinishedEvent(CSocket* socket, const unsigned char* packet);
    static bool SocialActionEvent(CSocket* socket, const unsigned char* packet);
    static bool PetDiedEvent(CSocket* socket, const unsigned char* packet);
    static bool WasCollectedEvent(CSocket* socket, const unsigned char* packet);
    static bool DebugAIEvent(CSocket* socket, const unsigned char* packet);
    static bool IsToggleSkillOnOff(CSocket* socket, const unsigned char* packet);
    static bool RequestNPCTimerInfo(CSocket* socket, const unsigned char* packet);
    static bool BuilderCmd(CSocket* socket, const unsigned char* packet);
    static bool DynamicQuestPacket(CSocket* socket, const unsigned char* packet);
    static bool DeleteTopAgroPacket(CSocket* socket, const unsigned char* packet);
    static bool SceneStoppedPacket(CSocket* socket, const unsigned char* packet);
    static bool GetPledgeRaidRewardRatePacket(CSocket* socket, const unsigned char* packet);
    static bool InstantAgitPacket(CSocket* socket, const unsigned char* packet);
    static bool CallToChangeClass(CSocket* socket, const unsigned char* packet);
    static bool ChangeToAwakenedClass(CSocket* socket, const unsigned char* packet);
    static bool DynamicQuestInfoPacket(CSocket* socket, const unsigned char* packet);
    static bool ReplyTotalSkillEnchantCount(CSocket* socket, const unsigned char* packet);
    static bool ChangeClassEvent(CSocket* socket, const unsigned char* packet);
    static bool CanChangeToSubclass(CSocket* socket, const unsigned char* packet);
    static bool StartDynamicQuestResult(CSocket* socket, const unsigned char* packet);
    static bool EventCampaignPacket(CSocket* socket, const unsigned char* packet);
    static bool AddUseSkillDesireFromServer(CSocket* socket, const unsigned char* packet);
    static bool FinishPledgeGamePacket(CSocket* socket, const unsigned char* packet);
    static bool WorldChatPacket(CSocket* socket, const unsigned char* packet);
    static bool GetEventGreateContributor(CSocket* socket, const unsigned char* packet);
    static bool Handle_NS_CAST_BUFF_TO_RANDOM_USER(CSocket* socket, const unsigned char* packet);
    static bool NpcPosEventWithReward(CSocket* socket, const unsigned char* packet);
    static bool Handle_NS_RESULT_PLEDGE_RAID_POINT(CSocket* socket, const unsigned char* packet);
    static bool Handle_NS_ON_HUNTER_GUILD_BLESS_EVENT(CSocket* socket, const unsigned char* packet);
    static bool Handle_NS_WORLD_RAID_START(CSocket* socket, const unsigned char* packet);
    static bool Handle_NS_WORLD_RAID_STOP(CSocket* socket, const unsigned char* packet);
    static bool Handle_NS_WORLD_RAID_BROADCAST_EVENT(CSocket* socket, const unsigned char* packet);
    static bool EventNpc(CSocket* socket, const unsigned char* packet);
    static bool RealDamagePacket(CSocket* socket, const unsigned char* packet);

private:
    void CheckHourForPacketStat();

    // Current target's only CServerSocket field, at +0x108.
    std::int32_t m_nLastCheckedHourForPacketStat = 0;
};


using CServerPacket = CPacketEx<CServerPacketBuffer, CServerSocket, L2ServerCallback>;

} // namespace l2npc
