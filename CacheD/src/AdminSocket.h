#pragma once

#include "CacheSocket.h"
#include "Item.h"

#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CAdminSocket;

using AdminPacketCallback = bool (*)(CAdminSocket*, const std::uint8_t*);
using AdminCallbackMap = FixedSizeCallbackMap<191, AdminPacketCallback>;

extern AdminCallbackMap gPacketTableFromAdmin;

bool DummyPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AdminVersionPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CheckCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetCharacterLocationPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetBuilderCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ChangeCharacterNamePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool KickCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetOnetimeQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetQuest2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool AddItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool AddPremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddItemsPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int ownerId,
    int warehouse,
    int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int variationOption1,
    int variationOption2,
    int intensiveItemType,
    int duration,
    short attackType,
    short attackValue,
    short defendValues[],
    int appearanceType,
    int period,
    CItem::EnsoulInfo ensoulInfo[],
    short hLocked,
    short useCount,
    const wchar_t* auditText
);
bool DelItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModChar2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool ModCharPledgePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool PunishCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetBuilderAccountPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DisableCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool EnableCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCharactersPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCharacters2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCharacters3Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCharacters4Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCharactersExceptBanPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModChar3Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool SetBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelPledgeCrestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelPledgeEmblemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelPledgeAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModPledgeNamePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetProhibitedPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SeizeItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool MoveItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool MoveItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool MoveCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CommentWritePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CommentDeletePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DeleteCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RestoreCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RestoreChar2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool PledgeOustPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool BanCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CreatePetPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SendHomePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CreatePledgePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetSkillAllPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ManAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ManIntAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModWeekPlayPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SeizeItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool PrintAllAccountData(CAdminSocket* socket, const std::uint8_t* packet);
bool PrintAllItemData(CAdminSocket* socket, const std::uint8_t* packet);
bool CopyCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CreateCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CreateCharacter2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool SetInstantAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SendPrivateAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetPartyLocationPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CreateSubjobPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetPledgeNameValuePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool TransferPledgeMaster(CAdminSocket* socket, const std::uint8_t* packet);
bool AddRecipeBookPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetSubjobLevelPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModUserColorPremiumPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DeleteReceivedMail(CAdminSocket* socket, const std::uint8_t* packet);
bool GetSSQStatusPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetSSQMainEventRecordPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetAgitListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCharCreateDatePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetSSQMainEventRecordPacket2(CAdminSocket* socket, const std::uint8_t* packet);
bool DeletePostPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ReloadCharInfo(CAdminSocket* socket, const std::uint8_t* packet);
bool WithdrawPremiumItemAtNPC(CAdminSocket* socket, const std::uint8_t* packet);
bool DelItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool MoveItem3Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool SeizeItem3Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool SeizePremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelPremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool MovePremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetUserBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelUserBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SeizePostItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DeletePostItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool MovePostItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool PledgeChangeOwnerPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool PledgeDeletePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ChangePledgeLevelPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelMacroPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelRecipePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetSocialityPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ChangeSubJobPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddSubjobsPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelLottoPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelMonRaceTicketPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool EventPointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AllEventPointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetPledgePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetPledge2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool GetPledgeMemberPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetPledgeMemberPacket2(CAdminSocket* socket, const std::uint8_t* packet);
bool GetPledge3Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool GetFriendPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetPetListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelSeizedItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModifyDepositedSSQItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ChangePetNamePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddSkillsPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelHennaPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddHennaPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddHennasPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ManageUserNameReservedPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetCastleListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModifyCharPropertyPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModifyCharAbilityDeltaPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModifyCharAbilityPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelHenna2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool AddHenna2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool AddSkill2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool DelSkill2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool ModifySkill2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool AddMacroPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddMacroInfoPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddSkills2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool AddHennas2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool StopCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CancelPersonalShopPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CheckPCBangCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetNoblessPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetHeroPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool EternalBanPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModOlympiadPointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetPccafePointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetPccafePointRelativePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModVitalityPointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModItemPeriod(CAdminSocket* socket, const std::uint8_t* packet);
bool SetColorPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DisableAddedServicePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RegisterAccountPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GmRegPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GetAccountId(CAdminSocket* socket, const std::uint8_t* packet);
bool AddSubPledgeSKillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelSubPledgeSKillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModSubPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModPvpPointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModBOTPointPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CheckCacheD(CAdminSocket* socket, const std::uint8_t* packet);
bool SetPetitionDisableTime(CAdminSocket* socket, const std::uint8_t* packet);
bool SetRewardNpc(CAdminSocket* socket, const std::uint8_t* packet);
bool GMAnnounceSend(CAdminSocket* socket, const std::uint8_t* packet);
bool GMAnnounceDel(CAdminSocket* socket, const std::uint8_t* packet);
bool GMAnnounceSendToServer(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestWritePost(CAdminSocket* socket, const std::uint8_t* packet);
bool BroadcastInstantAnnounce(CAdminSocket* socket, const std::uint8_t* packet);
bool BroadcastScreenAnnounce(CAdminSocket* socket, const std::uint8_t* packet);
bool CheckFreeUserPacket(CAdminSocket* socket, const std::uint8_t* packet);

bool SeizeItemProc(CAdminSocket* socket, int replyOpcode, int option, int itemId, std::int64_t amount, wchar_t auditText[]);
bool DelItemProc(CAdminSocket* socket, int replyOpcode, int itemId, std::int64_t amount, int deletePet, wchar_t* auditText);
bool MoveItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int sourceCharacterId,
    int itemId,
    std::int64_t amount,
    int destinationCharacterId,
    int destinationWarehouse,
    const wchar_t* auditText
);

namespace AQHandler {
bool RequestSeizeCommissionItem(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestDeleteCommissionItem(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestDeleteBuff(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddDualClass(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestCancelDualClass(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestUpdateEventData(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestResetInzoneRestrict(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestSendPost(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestValidatePostUser(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddRecipeListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddShortcutListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestModifyPledgeGameInfoPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddActiveSkill(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestDeleteSkillDelay(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestCheckRaidUser(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestSetInformar(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestResetAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestResetWorldOlympiadPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestModifyRaidPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestModifyWorldchatPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddAbilitySkill(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestModifyVipPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestSetRewardNpcVer2(CAdminSocket* socket, const std::uint8_t* packet);
} // namespace AQHandler

// Shipping owners: CacheD/adminSocket.h and CacheD/adminsocket.cpp.
// Current Cached.exe fixes the 0x190-byte extent, the CSocket prefix and the
// sole byte at +0x188 used by the AQ handler gate.
class CAdminSocket : public CSocket {
public:
    explicit CAdminSocket(std::uint64_t socket, int bufferSize = 0x5000);
    ~CAdminSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static CSocket* Allocate(std::uint64_t socket);

    void SetAddress(::in_addr address) override;
    void OnCreate() override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;

    // The original inline identifier is absent from the usable symbol
    // records. Every current AQ handler directly tests the byte at +0x188.
    // this semantic spelling exposes that production condition without
    // adding state or a second dispatch path.
    bool RejectsAdminRequests() const noexcept { return m_rejectAdminRequests; }

protected:
    void OnClose() override;
    void OnRead() override;

private:
    friend bool AdminVersionPacket(CAdminSocket* socket, const std::uint8_t* packet);

    L2_DECLARE_OBJECT_REGISTRATION(CAdminSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAdminSocket)

    bool m_rejectAdminRequests = false; // target +0x188
    [[maybe_unused]] std::array<std::byte, 7> m_padding189{};
};
