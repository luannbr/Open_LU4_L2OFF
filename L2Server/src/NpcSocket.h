#pragma once

#include "Shared/src/Socket.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CNPC;
struct FVector;

extern FixedSizeCallbackMap<255, NpcPacketCallback> gPacketTableFromNpc;


class CL2ServerProtector {
public:
    static CL2ServerProtector& inst();

private:
    friend class CProtectionRequest;

    CL2ServerProtector() = default;
    void BeginProtection();
    void EndProtection();

    void* m_hMutex = nullptr;
    volatile std::int32_t m_nProtectionCount = 0;
};

class CProtectionRequest {
public:
    CProtectionRequest();
    ~CProtectionRequest();
};

class CEnterWorldSerializer : public CIOObject {
public:
    struct NpcEnterWorldData {
        std::int32_t index;
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        std::int32_t dir;
        std::int32_t nSetDefaultParam;
        std::int32_t nHP;
        std::int32_t nMP;
        std::uint32_t inzoneSID;
    };

    CEnterWorldSerializer();
    ~CEnterWorldSerializer() override;

    int GetModifiedHeight(int x, int y, int z);
    void TimerExpired(int id) override;
    void Push(int index, int x, int y, int z, int dir, int setDefaultParam, int hp, int mp, unsigned int inzoneSid);
    void ClearDataQueue();

private:
    ::std::deque<NpcEnterWorldData> m_queue;
    CYieldLock m_Sect;
    volatile std::int32_t m_Started;
    [[maybe_unused]] volatile std::int32_t m_MakerFinished;

public:
    // Current NpcMakerFinished decodes the peer's second dword directly
    // into this public source field at object offset +0x60.
    std::int32_t m_InitSpawnCount;
};

class CNpcRemover : public CIOObject {
public:

    CNpcRemover();
    ~CNpcRemover() override;
    void TimerExpired(int id) override;

private:
    std::int32_t i;
    std::int32_t m_nTotalCount;
    std::uint32_t m_nStartTick;
    ::std::vector<CNPC*> ClearList;
    CProtectionRequest m_ProtectionReq;
};

// Current L2Server RTTI/vtable at 0x0108d0a8 proves a direct CIOObject
// owner. The global construction envelope at 0x00d9ea48 fixes the target
// extent and the vector/lock layout below.
class CUserNotifier : public CIOObject {
public:

    CUserNotifier();
    ~CUserNotifier() override;

    void TimerExpired(int id) override;
    void Notify();

private:
    ::std::vector<std::uint32_t> m_user;
    std::int32_t i;
    CYieldLock m_lock;
};

extern CEnterWorldSerializer g_EnterWorldSerializer;
extern CUserNotifier g_UserNotifier;

// Current L2Server RTTI and the complete 28-slot vtable prove direct,
// zero-offset CSocket inheritance. Allocate/constructor evidence fixes the
// derived extent at 0x110 and the sole field at +0x108.
class NpcSocket : public CSocket {
public:

    explicit NpcSocket(std::uint64_t socket);
    ~NpcSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static CSocket* Allocate(std::uint64_t socket);
    bool CheckPendingWrite() override;
    void OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const override;

    static bool NpcGetItem(NpcSocket* socket, const unsigned char* packet);
    static bool NpcWhisper(NpcSocket* socket, const unsigned char* packet);
    static bool NpcWhisperFStr(NpcSocket* socket, const unsigned char* packet);
    static bool NpcIsToggleSkillOnOff(NpcSocket* socket, const unsigned char* packet);
    static bool NpcClientEffectInfo(NpcSocket* socket, const unsigned char* packet);
    static bool NpcFollow(NpcSocket* socket, const unsigned char* packet);
    static bool NpcSetAbilityDropItem(NpcSocket* socket, const unsigned char* packet);
    static bool NpcChangeName(NpcSocket* socket, const unsigned char* packet);
    static bool NpcChangeState(NpcSocket* socket, const unsigned char* packet);
    static bool NpcTeleportTo(NpcSocket* socket, const unsigned char* packet);
    static bool NpcShowChannelingEffect(NpcSocket* socket, const unsigned char* packet);
    static bool NpcChangeDir(NpcSocket* socket, const unsigned char* packet);
    static bool NpcIncreaseNPCLog(NpcSocket* socket, const unsigned char* packet);
    static bool NpcDynamicQuest(NpcSocket* socket, const unsigned char* packet);
    static bool NpcSetCalledPledgeDBId(NpcSocket* socket, const unsigned char* packet);
    static bool NpcGetPledgeRaidRewardRate(NpcSocket* socket, const unsigned char* packet);
    static bool NpcInstantAgitPacket(NpcSocket* socket, const unsigned char* packet);
    static bool NpcCallToChangeClass(NpcSocket* socket, const unsigned char* packet);
    static bool ShowCommission(NpcSocket* socket, const unsigned char* packet);
    static bool NpcChangeToAwakenedClass(NpcSocket* socket, const unsigned char* packet);
    static bool NpcBlockUpset(NpcSocket* socket, const unsigned char* packet);
    static bool NpcCleftUserEnter(NpcSocket* socket, const unsigned char* packet);
    static bool NpcCleftCenterDestroyed(NpcSocket* socket, const unsigned char* packet);
    static bool CreatureInfoUpdated(NpcSocket* socket, const unsigned char* packet);
    static bool NpcStartScenePlayer(NpcSocket* socket, const unsigned char* packet);
    static bool NpcShowBuySellPagePacket(NpcSocket* socket, const unsigned char* packet);
    static bool NpcObserveEventMatch(NpcSocket* socket, const unsigned char* packet);
    static bool ShowStatPage(NpcSocket* socket, const unsigned char* packet);
    static bool RequestTotalSkillEnchantCount(NpcSocket* socket, const unsigned char* packet);
    static bool FindRandomUser(NpcSocket* socket, const unsigned char* packet);
    static bool GetEventScheduledTime(NpcSocket* socket, const unsigned char* packet);
    static bool RequestEventInfo(NpcSocket* socket, const unsigned char* packet);
    static bool RegisterOlympiadMaker(NpcSocket* socket, const unsigned char* packet);
    static bool NpcCleftManagerEnter(NpcSocket* socket, const unsigned char* packet);
    static bool NpcEventManagetEnter(NpcSocket* socket, const unsigned char* packet);
    static bool NpcYearCatchEventManagerEnter(NpcSocket* socket, const unsigned char* packet);
    static bool NpcShowInTerritory(NpcSocket* socket, const unsigned char* packet);

protected:
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void TimerExpired(int id) override;

private:
    bool bMustLogDataTransmissionLog;

    L2_DECLARE_OBJECT_REGISTRATION(NpcSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(NpcSocket)
};

// Current L2Server RTTI at 0x013f1808 records CNPCPacket as a direct
// CPacketEx<CIOBufferEx<20480>> derivative. The object adds no storage.
// its sole distinct virtual body is the JOB_NPCSOCKET__TOTAL wrapper at
// 0x007cd600 around the inherited synchronous callback/lifetime contract.
class CNPCPacket final : public CNPCPacketBase {
public:
    CNPCPacket() = default;
    ~CNPCPacket() override = default;

protected:
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
};

bool NpcRegisterTelPosListPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcVersionPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcCreatePacket(NpcSocket* socket, const unsigned char* packet);
bool NpcEnterWorldPacket(NpcSocket* socket, const unsigned char* packet);
void NpcEnterWorld_Do(int index, FVector* position, int dir, int setDefaultParam, int hp, int mp, int inzoneSid);
bool NpcTeleportPacket(NpcSocket* socket, const unsigned char* packet);
bool InsertEventRanking(NpcSocket* socket, const unsigned char* packet);
bool NpcMovePacket(NpcSocket* socket, const unsigned char* packet);
bool NpcSayPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcSayFStringPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcAttackPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcSetTaxRate(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterAgit(NpcSocket* socket, const unsigned char* packet);
bool NpcShowHTMLString(NpcSocket* socket, const unsigned char* packet);
bool NpcChangeMoveType(NpcSocket* socket, const unsigned char* packet);
bool NpcEquipItem(NpcSocket* socket, const unsigned char* packet);
bool NpcMoveToPawn(NpcSocket* socket, const unsigned char* packet);
bool NpcShowSkillList(NpcSocket* socket, const unsigned char* packet);
bool NpcGrowSkillSuggestion(NpcSocket* socket, const unsigned char* packet);
bool NpcClassChange(NpcSocket* socket, const unsigned char* packet);
bool NpcSetWeightPoint(NpcSocket* socket, const unsigned char* packet);
bool NpcLeaveWorld(NpcSocket* socket, const unsigned char* packet);
bool NpcUseSkill(NpcSocket* socket, const unsigned char* packet);
bool NpcUseItem(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterBuySellListPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterCastleSiege(NpcSocket* socket, const unsigned char* packet);
bool NpcUnregisterCastleSiege(NpcSocket* socket, const unsigned char* packet);
bool NpcSetCastleSiegeTime(NpcSocket* socket, const unsigned char* packet);
bool NpcCreatePrivates(NpcSocket* socket, const unsigned char* packet);
bool NpcCreateOnePrivate(NpcSocket* socket, const unsigned char* packet);
bool NpcCreateOneAnother(NpcSocket* socket, const unsigned char* packet);
bool NpcInstantTeleport(NpcSocket* socket, const unsigned char* packet);
bool NpcInstantTeleportInMyTerritory(NpcSocket* socket, const unsigned char* packet);
bool NpcKillMe(NpcSocket* socket, const unsigned char* packet);
bool NpcPledgeLevelUp(NpcSocket* socket, const unsigned char* packet);
bool NpcPing(NpcSocket* socket, const unsigned char* packet);
bool NpcViewSiegeList(NpcSocket* socket, const unsigned char* packet);
bool NpcApproveBattle(NpcSocket* socket, const unsigned char* packet);
bool NpcCastleGateOpenClose(NpcSocket* socket, const unsigned char* packet);
bool NpcShowSystemMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcShowSystemMessageFStr(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastSystemMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastSystemMessageFStr(NpcSocket* socket, const unsigned char* packet);
bool NpcCheckSiege(NpcSocket* socket, const unsigned char* packet);
bool NpcSocialAction(NpcSocket* socket, const unsigned char* packet);
bool NpcSocialAction2(NpcSocket* socket, const unsigned char* packet);
bool NpcDespawn(NpcSocket* socket, const unsigned char* packet);
bool NpcPongForCastleSiege(NpcSocket* socket, const unsigned char* packet);
bool NpcSound(NpcSocket* socket, const unsigned char* packet);
bool NpcCastleBanishOthers(NpcSocket* socket, const unsigned char* packet);
bool NpcCastleHpRegen(NpcSocket* socket, const unsigned char* packet);
bool NpcCastleMpRegen(NpcSocket* socket, const unsigned char* packet);
bool NpcChangeStopType(NpcSocket* socket, const unsigned char* packet);
bool ShowTutorialPage(NpcSocket* socket, const unsigned char* packet);
bool ShowNewTutorialPage(NpcSocket* socket, const unsigned char* packet);
bool ShowTutorialMark(NpcSocket* socket, const unsigned char* packet);
bool CloseTutorialHTML(NpcSocket* socket, const unsigned char* packet);
bool EnableTutorialEvent(NpcSocket* socket, const unsigned char* packet);
bool RadarControl(NpcSocket* socket, const unsigned char* packet);
bool NpcAtomicDelGiveItemParam(NpcSocket* socket, const unsigned char* packet);
bool NpcCastleGateOpenClose2(NpcSocket* socket, const unsigned char* packet);
bool InzoneDoorBreakable(NpcSocket* socket, const unsigned char* packet);
bool NpcEarthquake(NpcSocket* socket, const unsigned char* packet);
bool NpcSpecialCamera(NpcSocket* socket, const unsigned char* packet);
bool NpcSpecialCamera2(NpcSocket* socket, const unsigned char* packet);
bool NpcSpecialCamera3(NpcSocket* socket, const unsigned char* packet);
bool NpcSpecialCameraZLimit(NpcSocket* socket, const unsigned char* packet);
bool NpcNormalCamera(NpcSocket* socket, const unsigned char* packet);
bool NpcAgitAuction(NpcSocket* socket, const unsigned char* packet);
bool NpcOpenSiegeInfo(NpcSocket* socket, const unsigned char* packet);
bool NpcSetDoorHpLevel(NpcSocket* socket, const unsigned char* packet);
bool NpcGetDoorHpLevel(NpcSocket* socket, const unsigned char* packet);
bool NpcSetControlTowerLevel(NpcSocket* socket, const unsigned char* packet);
bool NpcGetControlTowerLevel(NpcSocket* socket, const unsigned char* packet);
bool NpcAgitTeleportLevel(NpcSocket* socket, const unsigned char* packet);
bool NpcMusicEffect(NpcSocket* socket, const unsigned char* packet);
bool NpcSetResidenceTaxRate(NpcSocket* socket, const unsigned char* packet);
bool NpcMoveTaxToResidenceBank(NpcSocket* socket, const unsigned char* packet);
bool NpcMoveMoneyToResidenceBank(NpcSocket* socket, const unsigned char* packet);
bool NpcStaticObjectAttackPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcSetResidenceTaxRateEach(NpcSocket* socket, const unsigned char* packet);
bool NpcSetVaultEmpty(NpcSocket* socket, const unsigned char* packet);
bool NpcRequestLookNeighbor(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastScriptEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcDominionScriptEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcLottoBuyTicket(NpcSocket* socket, const unsigned char* packet);
bool NpcSuicide(NpcSocket* socket, const unsigned char* packet);
bool NpcSystemMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterTeamBattle(NpcSocket* socket, const unsigned char* packet);
bool NpcQueryTeamBattleNpc(NpcSocket* socket, const unsigned char* packet);
bool NpcHennaItemListOpenForEquip(NpcSocket* socket, const unsigned char* packet);
bool NpcHennaEquipListOpenForUnequip(NpcSocket* socket, const unsigned char* packet);
bool NpcShowManorMenu(NpcSocket* socket, const unsigned char* packet);
bool NpcSetNextSeedInfo(NpcSocket* socket, const unsigned char* packet);
bool NpcSetNextProcureInfo(NpcSocket* socket, const unsigned char* packet);
bool NpcShowSellSeedList(NpcSocket* socket, const unsigned char* packet);
bool NpcShowProcureCropList(NpcSocket* socket, const unsigned char* packet);
bool NpcAreaSetOnOff(NpcSocket* socket, const unsigned char* packet);
bool NpcStopMove(NpcSocket* socket, const unsigned char* packet);
bool NpcShowSellPreviewPagePacket(NpcSocket* socket, const unsigned char* packet);
bool NpcGetSubJobList(NpcSocket* socket, const unsigned char* packet);
bool NpcCreateSubJob(NpcSocket* socket, const unsigned char* packet);
bool NpcChangeSubJob(NpcSocket* socket, const unsigned char* packet);
bool NpcRenewSubJob(NpcSocket* socket, const unsigned char* packet);
bool NpcShowDirection(NpcSocket* socket, const unsigned char* packet);
bool NpcShout(NpcSocket* socket, const unsigned char* packet);
bool NpcShoutFStringPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcShowMultisellPage(NpcSocket* socket, const unsigned char* packet);
bool NpcJoinTimeAttack(NpcSocket* socket, const unsigned char* packet);
bool NpcLeaveTimeAttack(NpcSocket* socket, const unsigned char* packet);
bool NpcGiveContribution(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastToParty(NpcSocket* socket, const unsigned char* packet);
bool NpcQueryTeamBattlePledge(NpcSocket* socket, const unsigned char* packet);
bool NpcGetBattleRoyalPledgeList(NpcSocket* socket, const unsigned char* packet);
bool NpcShowTerritoryInfo(NpcSocket* socket, const unsigned char* packet);
bool NpcShowEnchantSkillList(NpcSocket* socket, const unsigned char* packet);
bool NpcEnchantSkillSuggestion(NpcSocket* socket, const unsigned char* packet);
bool NpcShowQuestInfo(NpcSocket* socket, const unsigned char* packet);
bool NpcShowQuestMark(NpcSocket* socket, const unsigned char* packet);
bool NpcManorFunc(NpcSocket* socket, const unsigned char* packet);
bool NpcUseSoulSpiritShot(NpcSocket* socket, const unsigned char* packet);
bool NpcSetSkillAll(NpcSocket* socket, const unsigned char* packet);
bool NpcLookItems(NpcSocket* socket, const unsigned char* packet);
bool NpcShowTelposList(NpcSocket* socket, const unsigned char* packet);
bool NpcAnnouncePacket(NpcSocket* socket, const unsigned char* packet);
bool NpcOlympiadFunc(NpcSocket* socket, const unsigned char* packet);
bool SendBufferToUser(NpcSocket* socket, const unsigned char* packet);
bool DeletePreviousOlympiadPoint(NpcSocket* socket, const unsigned char* packet);
bool NpcSetEnchantOfWeapon(NpcSocket* socket, const unsigned char* packet);
bool NpcUpdatePCCafePoint(NpcSocket* socket, const unsigned char* packet);
bool NpcGiveItemByPCCafePoint(NpcSocket* socket, const unsigned char* packet);
bool NpcInstantTeleportMPCC(NpcSocket* socket, const unsigned char* packet);
bool NpcSetMPCCMasterPartyRouting(NpcSocket* socket, const unsigned char* packet);
bool NpcMakerFinished(NpcSocket* socket, const unsigned char* packet);
bool NpcRegMakerEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterAIStatus(NpcSocket* socket, const unsigned char* packet);
bool NpcMiniGameAgitFunc(NpcSocket* socket, const unsigned char* packet);
bool NpcShowScreenMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcShowScreenFStrMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastScreenMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastScreenFStrMessage(NpcSocket* socket, const unsigned char* packet);
bool NpcSetParam(NpcSocket* socket, const unsigned char* packet);
bool NpcCheckCursedUser(NpcSocket* socket, const unsigned char* packet);
bool NpcShowCursedWeaponStatus(NpcSocket* socket, const unsigned char* packet);
bool NpcTBAgitCheckMemberRegisterStatus(NpcSocket* socket, const unsigned char* packet);
bool NpcAuctionAgitGetCostInfo(NpcSocket* socket, const unsigned char* packet);
bool DeleteOlympiadTradePoint(NpcSocket* socket, const unsigned char* packet);
bool AddOlympiadTradePoint(NpcSocket* socket, const unsigned char* packet);
bool NpcAddPCSocial(NpcSocket* socket, const unsigned char* packet);
bool NpcShowFishingRanking(NpcSocket* socket, const unsigned char* packet);
bool NpcGiveFishingPrize(NpcSocket* socket, const unsigned char* packet);
bool ShowVariationMakeWindow(NpcSocket* socket, const unsigned char* packet);
bool ShowVariationCancelWindow(NpcSocket* socket, const unsigned char* packet);
bool NPCPlayAnimation(NpcSocket* socket, const unsigned char* packet);
bool NPCTeleportToUser(NpcSocket* socket, const unsigned char* packet);
bool NpcInstantZoneEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcShowChangePledgeNameUI(NpcSocket* socket, const unsigned char* packet);
bool NpcSetStaticMeshStatus(NpcSocket* socket, const unsigned char* packet);
bool ShowBaseAttributeCancelWindow(NpcSocket* socket, const unsigned char* packet);
bool NpcAreaSetBannedTerritoryOnOff(NpcSocket* socket, const unsigned char* packet);
bool GiveItemByCastleSiegeDefence(NpcSocket* socket, const unsigned char* packet);
bool NpcCreatePVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcUnregisterPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcStartPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcEndPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcCheckRegisterParty(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterResurrectionTower(NpcSocket* socket, const unsigned char* packet);
bool NpcDispel(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterTeleporterType(NpcSocket* socket, const unsigned char* packet);
bool NpcAbsoluteReturn(NpcSocket* socket, const unsigned char* packet);
bool NpcSetRimPoint(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterDominionSiege(NpcSocket* socket, const unsigned char* packet);
bool NpcDeclareLord(NpcSocket* socket, const unsigned char* packet);
bool NpcGetRegistrySize(NpcSocket* socket, const unsigned char* packet);
bool NpcShowPremiumItemList(NpcSocket* socket, const unsigned char* packet);
bool NpcCheckRegisterUser(NpcSocket* socket, const unsigned char* packet);
bool NpcIsPVPMatching(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterUserPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcUnregisterUserPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcAddKillPointUserPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcGetRankUserPVPMatch(NpcSocket* socket, const unsigned char* packet);
bool NpcRegisterUserResurrectionTower(NpcSocket* socket, const unsigned char* packet);
bool NPCUnequipWeapon(NpcSocket* socket, const unsigned char* packet);
bool NPCGiveEventItem(NpcSocket* socket, const unsigned char* packet);
bool NPCSetTrapWorldVisible(NpcSocket* socket, const unsigned char* packet);
bool NPCActivateWorldTrap(NpcSocket* socket, const unsigned char* packet);
bool NPCSetVisible(NpcSocket* socket, const unsigned char* packet);
bool NPCPlayScene(NpcSocket* socket, const unsigned char* packet);
bool NpcFortressEvent(NpcSocket* socket, const unsigned char* packet);
bool NPCFieldCycle(NpcSocket* socket, const unsigned char* packet);
bool NPCShowEnchantSkillListDrawer(NpcSocket* socket, const unsigned char* packet);
bool NpcChangeStatus(NpcSocket* socket, const unsigned char* packet);
bool NpcSaveGetbirthdayGiftTime(NpcSocket* socket, const unsigned char* packet);
bool NpcSaveGet5YearGiftTime(NpcSocket* socket, const unsigned char* packet);
bool NpcSetXMasEventState(NpcSocket* socket, const unsigned char* packet);
bool NpcSetYearCatchEventState(NpcSocket* socket, const unsigned char* packet);
bool SendUIEvent(NpcSocket* socket, const unsigned char* packet);
bool SendUIEventFStr(NpcSocket* socket, const unsigned char* packet);
bool BroadcastUIEventFStr(NpcSocket* socket, const unsigned char* packet);
bool BroadcastUIEventFStrInTerritory(NpcSocket* socket, const unsigned char* packet);
bool SendUSMEvent(NpcSocket* socket, const unsigned char* packet);
bool BroadcastUSMEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcSetDailyQuestFlag(NpcSocket* socket, const unsigned char* packet);
bool NpcSetWeeklyQuestFlag(NpcSocket* socket, const unsigned char* packet);
bool NpcSunRise(NpcSocket* socket, const unsigned char* packet);
bool ReplyNpcPing(NpcSocket* socket, const unsigned char* packet);
bool NpcBroadcastSound(NpcSocket* socket, const unsigned char* packet);
bool NPCFieldCyclePointLock(NpcSocket* socket, const unsigned char* packet);
