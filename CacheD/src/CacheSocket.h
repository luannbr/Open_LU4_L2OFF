#pragma once

#include "Shared/src/QsCheck.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/IOC.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CSocket;
class CAdminSocket;
class CNpcDbSocket;
bool CheckFreeUserPacket(CAdminSocket* socket, const std::uint8_t* packet);

namespace RQExHandler {
bool Request_RQ_EX_SEQUENCE_JOB(CSocket* socket, const unsigned char* packet);
}
namespace AQHandler {
bool RequestCheckRaidUser(CAdminSocket* socket, const std::uint8_t* packet);
}

using CacheDPacketCallback = bool (*)(CSocket*, const std::uint8_t*);
using CacheDCallbackMap = FixedSizeCallbackMap<235, CacheDPacketCallback>;
using CacheDExtendedCallbackMap = FixedSizeCallbackMap<461, CacheDPacketCallback>;

extern CacheDCallbackMap gPacketTableFromServer;
extern CacheDExtendedCallbackMap gPacketTableFromServerEx;




using CacheDIOSocket = CIOSocketEx<DefaultIOBuffer, CIOSocketCriticalSectionWriteControl>;

// Shipping owner: CacheD/CacheSocket.{h,cpp}. This is not Shared/socket.h's
// L2NPC CSocket: current Cached.exe derives directly from the critical-section
// CIOSocketEx specialization and keeps the complete 0x188-byte layout.
class CSocket : public CacheDIOSocket {
public:
    explicit CSocket(std::uint64_t socket, int bufferSize = 0x5000);
    ~CSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static CSocket* Allocate(std::uint64_t socket);

    static bool DropItemPacket(CSocket* socket, const std::uint8_t* packet);
    static bool SaveCharacter(CSocket* socket, const std::uint8_t* packet);
    static bool SaveSkillUse(CSocket* socket, const std::uint8_t* packet);
    static bool RequestLoadAgits(CSocket* socket, const std::uint8_t* packet);
    static bool SaveAbnormalStatus(CSocket* socket, const std::uint8_t* packet);

    void OnCreate() override;
    bool CheckPendingWriteSafe(int pendingWrite) const override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;

    virtual void SetAddress(::in_addr address);
    virtual void OnSending(std::uint8_t protocol, std::uint16_t extendedProtocol, int packetSize) const;

protected:
    void OnClose() override;
    void OnRead() override;

private:
    friend class CAdminSocket;
    friend class CNpcDbSocket;
    friend bool CheckFreeUserPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestCheckRaidUser(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool RQExHandler::Request_RQ_EX_SEQUENCE_JOB(CSocket* socket, const unsigned char* packet);

    L2_DECLARE_OBJECT_REGISTRATION(CSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CSocket)

    // Current offsets and initialization are exact. The unnamed spans are
    // kept in-place until a surviving symbol or a complete access set
    // establishes their original private member spellings.
    std::uint64_t m_socket; // +0x0a8
    std::uint64_t m_opaqueB0; // +0x0b0
    std::int32_t m_threadCount; // +0x0b8
    std::array<std::byte, 0x14> m_opaqueBC; // +0x0bc
    std::uint64_t m_opaqueD0; // +0x0d0
    std::uint64_t m_opaqueD8; // +0x0d8
    std::array<std::byte, 0x10> m_opaqueE0; // +0x0e0
    std::uint64_t m_opaqueF0; // +0x0f0
    std::uint64_t m_taggedEvent; // +0x0f8
    std::uint64_t m_event; // +0x100
    CacheDPacketCallback* m_packetTable; // +0x108
    ::in_addr m_address; // +0x110
    CIOCriticalSection m_packetLock; // +0x118
    CIOCriticalSection m_sendLock; // +0x140
    SocketMode m_mode; // +0x168
    std::int32_t m_packetLength; // +0x16c
    std::int32_t m_packetCount; // +0x170
    std::int32_t m_oldPacketCount; // +0x174
    std::uint64_t m_key; // +0x178
    std::uint32_t m_createdTick; // +0x180
    // MSVC does not reuse this base-class tail padding for a derived member.
    // Spell it out so the portable Itanium ABI keeps CAdminSocket's first
    // byte at the shipping +0x188 offset as well.
    std::array<std::byte, 4> m_padding184{}; // +0x184
};





// Current stateless owner at 0x0082e9d8. Its surviving method signature is
// exact. The global data identifier and source translation unit do not
// survive, so the semantic global name below is non-original.
class CSocketHandler {
public:
    bool OnRequestSetCastleOwner(
        int pledgeWorldId,
        int previousPledgeWorldId,
        int castleId,
        int pledgeId,
        int previousPledgeId,
        int castleType,
        CSocket* socket
    );
};

extern CSocketHandler g_socketHandler;

using CacheDCPacketEx =
    CPacketEx<DefaultIOBuffer, CSocket, CacheDPacketCallback, packet_ex_detail::CPacketExCacheDInstrumentation>;


class CServerPacket final : public CacheDCPacketEx {
public:
    CServerPacket();

    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;

    std::int32_t m_databaseId;
};


extern std::atomic<CSocket*> g_pActiveServerSocket;

// Portable termination transport invoked only after the two current IOCP
// item-check counters reach zero following an authorized ServerStopper.
void CompleteCacheDShutdownPersistence(const char* completionMessage);

void Decrypt(std::uint8_t* buffer, std::int64_t key, int length);

// Current helper 0x0045b0a8 has no surviving source identifier. This
// semantic name keeps its single process-wide three-way configuration rule.
int GetCharacterDeleteWaitSeconds() noexcept;

namespace ScriptFileUtil::CacheToServer {

int AssembleFileInfo(char* buffer, int bufferSize);

} // namespace ScriptFileUtil::CacheToServer

bool DummyPacket(CSocket* socket, const std::uint8_t* packet);
bool DummyServerExPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestDxPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestQueryPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestExPacket(CSocket* socket, const std::uint8_t* packet);
bool LoadCharacterPacket(CSocket* socket, const std::uint8_t* packet);
bool CreateCharacterPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadSummons(CSocket* socket, const std::uint8_t* packet);
bool LoadItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool CreateInitItems(CSocket* socket, const std::uint8_t* packet);
bool SaveCharacterPacket(CSocket* socket, const std::uint8_t* packet);
bool SaveItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool DeleteCharacterPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestUpdateUserPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestDeletePledge(CSocket* socket, const std::uint8_t* packet);
bool LoadWarehouseItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool WithdrawWarehouseItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool DepositWarehouseItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool SellItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool BuyItemsPacket(CSocket* socket, const std::uint8_t* packet);
bool UseItemPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestManipulateItem(CSocket* socket, const std::uint8_t* packet);
bool RequestOustPledgeMember(CSocket* socket, const std::uint8_t* packet);
bool RequestTrade(CSocket* socket, const std::uint8_t* packet);
bool RequestSetPledgeCrest(CSocket* socket, const std::uint8_t* packet);
bool RequestSetUserNickname(CSocket* socket, const std::uint8_t* packet);
bool RequestAquireSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestDestroyItem(CSocket* socket, const std::uint8_t* packet);
bool RequestEnchantItem(CSocket* socket, const std::uint8_t* packet);
bool RequestEnchantItemFail(CSocket* socket, const std::uint8_t* packet);
bool RequestSetItemCountLimit(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadFriendList(CSocket* socket, const std::uint8_t* packet);
bool RequestEstablishFriendship(CSocket* socket, const std::uint8_t* packet);
bool RequestBreakFriendship(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeItemData(CSocket* socket, const std::uint8_t* packet);
bool SetPunishPacket(CSocket* socket, const std::uint8_t* packet);
bool GetPunishPacket(CSocket* socket, const std::uint8_t* packet);
bool ChangePosPacket(CSocket* socket, const std::uint8_t* packet);
bool GetLoginAnnouncePacket(CSocket* socket, const std::uint8_t* packet);
bool SetBuilderPacket(CSocket* socket, const std::uint8_t* packet);
bool LoadSelectedCharacter(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestRestoreCharacter(CSocket* socket, const std::uint8_t* packet);
bool RequestSetPledgeInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestGetPledgeCrest(CSocket* socket, const std::uint8_t* packet);
bool RequestCreatePledgeByNpc(CSocket* socket, const std::uint8_t* packet);
bool RequestDeletePledgeByTimer(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadDismissReservedPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestPetiGetCharList(CSocket* socket, const std::uint8_t* packet);
bool RequestPetiSaveMsg(CSocket* socket, const std::uint8_t* packet);
bool RequestSetUserPledgeInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestLogCharInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestResetUserNickname(CSocket* socket, const std::uint8_t* packet);
bool RequestManageBattleCamp(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadBattleCamp(CSocket* socket, const std::uint8_t* packet);
bool RequestPrepareCastleWar(CSocket* socket, const std::uint8_t* packet);
bool RequestViewSiegeList(CSocket* socket, const std::uint8_t* packet);
bool RequestApproveBattle(CSocket* socket, const std::uint8_t* packet);
bool RequestConfirmCastleDefence(CSocket* socket, const std::uint8_t* packet);
bool RequestQueryCastleSiegeForRegister(CSocket* socket, const std::uint8_t* packet);
bool RequestQueryCastleSiegeForUnregister(CSocket* socket, const std::uint8_t* packet);
bool RequestQueryCastleSiegeForInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestQueryCastleSiegeForDismiss(CSocket* socket, const std::uint8_t* packet);
bool RequestDelPledgeCrest(CSocket* socket, const std::uint8_t* packet);
bool RequestModPledgeName(CSocket* socket, const std::uint8_t* packet);
bool ResetCastleSiegePledge(CSocket* socket, const std::uint8_t* packet);
bool RequestPartyTelBookMarkPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadDoor(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveCastleStatus(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveDoorStatus(CSocket* socket, const std::uint8_t* packet);

bool RequestLoadWarehouseItemList(CSocket* socket, const std::uint8_t* packet);
bool RequestCrystallizeItem(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveCastleIncome(CSocket* socket, const std::uint8_t* packet);
bool RequestInstallAgitDeco(CSocket* socket, const std::uint8_t* packet);
bool RequestSavePledgeContribution(CSocket* socket, const std::uint8_t* packet);
bool RequestGetContributionRelatedPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestSetAgitInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestPrivateStoreCommit(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveDieDropExp(CSocket* socket, const std::uint8_t* packet);
bool RequestResetContribution(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveObjectHp(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveCastleSiegeElapsedTime(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveSkillUse(CSocket* socket, const std::uint8_t* packet);
bool SetLoginAnnouncePacket(CSocket* socket, const std::uint8_t* packet);
bool DelLoginAnnouncePacket(CSocket* socket, const std::uint8_t* packet);
bool RequestShowIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
bool RequestSetIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
bool RequestDelIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadPledge2(CSocket* socket, const std::uint8_t* packet);
bool RequestPing(CSocket* socket, const std::uint8_t* packet);
bool RequestPingOneAdenaDrop(CSocket* socket, const std::uint8_t* packet);
bool RequestPingNetwork(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveAbnormalStatus(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadAbnormalStatus(CSocket* socket, const std::uint8_t* packet);
bool RequestCreateAllianceByNpc(CSocket* socket, const std::uint8_t* packet);
bool RequestJoinAlliance(CSocket* socket, const std::uint8_t* packet);
bool RequestDismissAlliance(CSocket* socket, const std::uint8_t* packet);
bool RequestOustAllianceMemberPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestWithdrawAlliance(CSocket* socket, const std::uint8_t* packet);
bool RequestCreatePet(CSocket* socket, const std::uint8_t* packet);
bool RequestDeletePet(CSocket* socket, const std::uint8_t* packet);
bool RequestEvolvePet(CSocket* socket, const std::uint8_t* packet);
bool RequestWithdrawPet(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadPetItems(CSocket* socket, const std::uint8_t* packet);
bool RequestDepositPet(CSocket* socket, const std::uint8_t* packet);
bool RequestSavePet(CSocket* socket, const std::uint8_t* packet);
bool RequestChangePetName(CSocket* socket, const std::uint8_t* packet);
bool RequestDismissPet(CSocket* socket, const std::uint8_t* packet);
bool RequestPetGetItem(CSocket* socket, const std::uint8_t* packet);
bool RequestPetDropItem(CSocket* socket, const std::uint8_t* packet);
bool RequestGiveItemToPet(CSocket* socket, const std::uint8_t* packet);
bool RequestGetItemFromPet(CSocket* socket, const std::uint8_t* packet);
bool RequestPetUseItem(CSocket* socket, const std::uint8_t* packet);
bool RequestSetAllianceCrest(CSocket* socket, const std::uint8_t* packet);
bool RequestGetAllianceCrest(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadAllAlliance(CSocket* socket, const std::uint8_t* packet);
bool RequestSetAllianceInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestGetDbRelatedCounts(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadAllPledgeWar(CSocket* socket, const std::uint8_t* packet);
bool RequestIncreaseUserPKCount(CSocket* socket, const std::uint8_t* packet);
bool RequestIncreaseUserDuelCount(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeMakeItem(CSocket* socket, const std::uint8_t* packet);
bool RequestPickItem(CSocket* socket, const std::uint8_t* packet);
bool RequestPackageLoadTargetList(CSocket* socket, const std::uint8_t* packet);
bool RequestPackageSendToTarget(CSocket* socket, const std::uint8_t* packet);
bool RequestAddBlockList(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteBlockList(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadWarehouseItemsEx(CSocket* socket, const std::uint8_t* packet);
bool RequestWithdrawWarehouseItemsEx(CSocket* socket, const std::uint8_t* packet);
bool RequestDepositWarehouseItemsEx(CSocket* socket, const std::uint8_t* packet);
bool RequestSetNextCastleSiege(CSocket* socket, const std::uint8_t* packet);
bool RequestCreateAgitAuction(CSocket* socket, const std::uint8_t* packet);
bool RequestCreateAgitBid(CSocket* socket, const std::uint8_t* packet);
bool RequestSetAgitAuction(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadCastleGuard(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadAgitBid(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadControlTower(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveControlTower(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveMercenary(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadMercenary(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteMercenary(CSocket* socket, const std::uint8_t* packet);
bool RequestUpdateMercenary(CSocket* socket, const std::uint8_t* packet);
bool RequestAgitPledgeAdena(CSocket* socket, const std::uint8_t* packet);
bool RequestCancelAgitAuction(CSocket* socket, const std::uint8_t* packet);
bool RequestAgitCost(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteCastleGuard(CSocket* socket, const std::uint8_t* packet);
bool RequestAgitAutoSell(CSocket* socket, const std::uint8_t* packet);
bool RequestAgitBidCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestEndSiege(CSocket* socket, const std::uint8_t* packet);
bool RequestPetiSaveMsgForce(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadLastResidenceTaxUpdate(CSocket* socket, const std::uint8_t* packet);
bool RequestSetLastResidenceTaxUpdate(CSocket* socket, const std::uint8_t* packet);
bool RequestMngComment(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadPlayTime(CSocket* socket, const std::uint8_t* packet);
bool RequestBookMark(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadTeamBattleAgit(CSocket* socket, const std::uint8_t* packet);
bool RequestNewTeamBattleAgitPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestNewTeamBattleAgitMember(CSocket* socket, const std::uint8_t* packet);
bool RequestTeamBattleAgitStatus(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeTimeLimit(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveUsedTime(CSocket* socket, const std::uint8_t* packet);
bool RequestUnregisterTeamBattleAgitPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestUnregisterTeamBattleAgitMember(CSocket* socket, const std::uint8_t* packet);
bool RequestMonrace(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadSiegeAgit(CSocket* socket, const std::uint8_t* packet);
bool RequestRegisterSiegeAgitPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestUnregisterSiegeAgitPledge(CSocket* socket, const std::uint8_t* packet);
bool RequstSetTeamBattleStatus(CSocket* socket, const std::uint8_t* packet);
bool RequestSetTeamBattleWinner(CSocket* socket, const std::uint8_t* packet);
bool RequestUseRecipeItem(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveRecipeBookInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteRecipeBookInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadRecipeBookInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopMakeItem(CSocket* socket, const std::uint8_t* packet);
bool RequestUpdateSociality(CSocket* socket, const std::uint8_t* packet);
bool RequestSetTeamBattleNpcType(CSocket* socket, const std::uint8_t* packet);
bool RequestResetSiegeAgitPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestResetTeamBattleAgitPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestEquipHenna(CSocket* socket, const std::uint8_t* packet);
bool RequestUnequipHenna(CSocket* socket, const std::uint8_t* packet);
bool RequestManageMacro(CSocket* socket, const std::uint8_t* packet);
bool RequestManPledgeBBS(CSocket* socket, const std::uint8_t* packet);
bool RequestManorBuySeed(CSocket* socket, const std::uint8_t* packet);
bool RequestManorSellCrop(CSocket* socket, const std::uint8_t* packet);
bool RequestManorMoveCrop(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveManorNextSeed(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveManorNowSeed(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveManorInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadAllManor(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadEventPoint(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveEventPoint(CSocket* socket, const std::uint8_t* packet);
bool RequestBBSClanAction(CSocket* socket, const std::uint8_t* packet);
bool RequestModAllianceName(CSocket* socket, const std::uint8_t* packet);
bool RequestCreateSubJob(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteSubJob(CSocket* socket, const std::uint8_t* packet);
bool RequestChangeSubJob(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadSubJob(CSocket* socket, const std::uint8_t* packet);
bool EventAmountPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveCharacterForSubJob(CSocket* socket, const std::uint8_t* packet);
bool RequestReloadSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestSaveAcquiredSkills(CSocket* socket, const std::uint8_t* packet);
bool RequestSavePunishmentHistory(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadSupendPledge(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadMainClassSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestDeletePledgeSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestCharacterLogin(CSocket* socket, const std::uint8_t* packet);
bool RequestCharacterLogout(CSocket* socket, const std::uint8_t* packet);
bool RequestQuestSet(CSocket* socket, const std::uint8_t* packet);
bool RequestSetAgitOwner(CSocket* socket, const std::uint8_t* packet);
bool RequestSetCastleOwner(CSocket* socket, const std::uint8_t* packet);
bool RequestLoadCastles(CSocket* socket, const std::uint8_t* packet);
bool RequestWriteCastleTax(CSocket* socket, const std::uint8_t* packet);
bool RequestShortCutInfoPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestSetShortCutPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestBuilderAccountList(CSocket* socket, const std::uint8_t* packet);
bool CacheVersionPacket(CSocket* socket, const std::uint8_t* packet);
