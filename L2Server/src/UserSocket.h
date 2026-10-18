#pragma once

#include "CConfig.h"
#include "Creature.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Socket.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"
#include "Shared/src/platform/Runtime.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class CDBSocket;
class CUserSocket;
class User;

int GetProtocolVersion();

extern int g_nClientFileDecryptionKeyLen;
extern char g_nClientFileDecryptionKey[1024];
extern volatile std::int32_t g_nSayCount;
extern volatile std::int32_t g_nSaySize;

void InitClientDecryptionKey();
bool CheckIPIsAdmin(wchar_t* address);

bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);

using UserPacketCallbackMap = FixedSizeCallbackMap<209, PacketFunc>;

bool DummyUserPacket(CSocket* socket, const std::uint8_t* packet);
bool UserExPacket(CSocket* socket, const std::uint8_t* packet);
bool ItemListPacket(CSocket* socket, const std::uint8_t* packet);
bool SkillListPacket(CSocket* socket, const std::uint8_t* packet);
bool WithdrawPledgePacket(CSocket* socket, const std::uint8_t* packet);
bool OustPledgeMemberPacket(CSocket* socket, const std::uint8_t* packet);
bool PledgeCrest(CSocket* socket, const std::uint8_t* packet);
bool ShowPledgeMemberListOpenPacket(CSocket* socket, const std::uint8_t* packet);
bool AllianceCrestPacket(CSocket* socket, const std::uint8_t* packet);
bool AllianceInfoPacket(CSocket* socket, const std::uint8_t* packet);
bool JoinAlliancePacket(CSocket* socket, const std::uint8_t* packet);
bool AnswerJoinAlliancePacket(CSocket* socket, const std::uint8_t* packet);
bool WithdrawAlliancePacket(CSocket* socket, const std::uint8_t* packet);
bool OustAllianceMemberPacket(CSocket* socket, const std::uint8_t* packet);
bool DismissAlliancePacket(CSocket* socket, const std::uint8_t* packet);
bool SetAllianceCrestPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestManagePledgePower(CSocket* socket, const std::uint8_t* packet);
bool AcquireSkillInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestAcquireSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestHennaEquip(CSocket* socket, const std::uint8_t* packet);
bool RequestHennaUnequipList(CSocket* socket, const std::uint8_t* packet);
bool RequestHennaUnequipInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestHennaUnequip(CSocket* socket, const std::uint8_t* packet);
bool RequestHennaItemList(CSocket* socket, const std::uint8_t* packet);
bool RequestHennaItemInfo(CSocket* socket, const std::uint8_t* packet);
bool QuestList(CSocket* socket, const std::uint8_t* packet);
bool FriendList(CSocket* socket, const std::uint8_t* packet);
bool GiveNicknamePacket(CSocket* socket, const std::uint8_t* packet);
bool SayPacket2(CSocket* socket, const std::uint8_t* packet);
bool ReplyL2FriendList(CSocket* socket, const std::uint8_t* packet);
bool L2FriendSayPacket(CSocket* socket, const std::uint8_t* packet);
bool MSNChatLogPacket(CSocket* socket, const std::uint8_t* packet);
bool FriendAddRequest(CSocket* socket, const std::uint8_t* packet);
bool FriendAddReply(CSocket* socket, const std::uint8_t* packet);
bool FriendRemove(CSocket* socket, const char16_t* friendName);
bool FriendRemove(CSocket* socket, const std::uint8_t* packet);
bool GMListPacket(CSocket* socket, const std::uint8_t* packet);
bool SnoopQuit(CSocket* socket, const std::uint8_t* packet);
bool ObserverEndPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestVoteSociality(CSocket* socket, const std::uint8_t* packet);
bool GetItemFromPetPacket(CSocket* socket, const std::uint8_t* packet);
bool PetitionPacket(CSocket* socket, const std::uint8_t* packet);
bool PetitionCancelPacket(CSocket* socket, const std::uint8_t* packet);
bool ChangePetNamePacket(CSocket* socket, const std::uint8_t* packet);
bool PetUseItemPacket(CSocket* socket, const std::uint8_t* packet);
bool GiveItemToPetPacket(CSocket* socket, const std::uint8_t* packet);
bool PetGetItem(CSocket* socket, const std::uint8_t* packet);
bool RequestPetitionVote(CSocket* socket, const std::uint8_t* packet);
bool RequestBlockPacket(CSocket* socket, const std::uint8_t* packet);
bool RemainCoolTimePacket(CSocket* socket, const std::uint8_t* packet);
bool LinkHtmlPacket(CSocket* socket, const std::uint8_t* packet);
bool WriteHtmlPacket(CSocket* socket, const std::uint8_t* packet);
bool ShowBBSBoard(CSocket* socket, const std::uint8_t* packet);
bool TutorialLinkHTML(CSocket* socket, const std::uint8_t* packet);
bool TutorialPassCmdToServer(CSocket* socket, const std::uint8_t* packet);
bool TutorialMarkPressed(CSocket* socket, const std::uint8_t* packet);
bool TutorialClientEvent(CSocket* socket, const std::uint8_t* packet);
bool RequestCastleSiegeInfoPacket(CSocket* socket, unsigned int residenceId);
bool RequestCastleSiegeInfoPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestCastleSiegeAttackerListPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestCastleSiegeDefenderListPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestJoinCastleSiegePacket(CSocket* socket, const std::uint8_t* packet);
bool RequestConfirmCastleSiegeWaitingListPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestSetCastleSiegeTimePacket(CSocket* socket, const std::uint8_t* packet);
bool ReloadPacket(CSocket* socket, const std::uint8_t* packet);
bool RestoreCharacter(CSocket* socket, const std::uint8_t* packet);
bool SekCostume(CSocket* socket, const std::uint8_t* packet);
bool LogoutPacket(CSocket* socket, const std::uint8_t* packet);
bool MoveToLocationBackPacket(CSocket* socket, const std::uint8_t* packet);
bool StartPledgeWarPacket(CSocket* socket, const std::uint8_t* packet);
bool StopPledgeWarPacket(CSocket* socket, const std::uint8_t* packet);
bool SetPledgeCrestPacket(CSocket* socket, int size, const std::uint8_t* data);
bool SetPledgeCrestPacket(CSocket* socket, const std::uint8_t* packet);
bool UnequipItemPacket(CSocket* socket, const std::uint8_t* packet);
bool UseItemPacket(CSocket* socket, const std::uint8_t* packet);
bool SellPacket(CSocket* socket, const std::uint8_t* packet);
bool BuyPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestBuySeed(CSocket* socket, const std::uint8_t* packet);
bool BuyPreviewPacket(CSocket* socket, const std::uint8_t* packet);
bool ChangeMoveTypePacket(CSocket* socket, const std::uint8_t* packet);
bool ChangeStopTypePacket(CSocket* socket, const std::uint8_t* packet);
bool MagicSkillListPacket(CSocket* socket, const std::uint8_t* packet);
bool MagicSkillUsePacket(CSocket* socket, const std::uint8_t* packet);
bool ShortCutRegisterPacket(CSocket* socket, const std::uint8_t* packet);
bool ShortCutDeletePacket(CSocket* socket, const std::uint8_t* packet);
bool WithdrawPartyPacket(CSocket* socket, const std::uint8_t* packet);
bool OustPartyMemberPacket(CSocket* socket, const std::uint8_t* packet);
bool DismissPartyPacket(CSocket* socket, const std::uint8_t* packet);
bool ReplyListPartyWaitingPacket(CSocket* socket, const std::uint8_t* packet);
bool ReplyManagePartyRoom(CSocket* socket, const std::uint8_t* packet);
bool ReplyJoinPartyRoom(CSocket* socket, const std::uint8_t* packet);
bool AttackPacket(CSocket* socket, const std::uint8_t* packet);
bool ActionPacket(CSocket* socket, const std::uint8_t* packet);
bool TargetUnselectedPacket(CSocket* socket, const std::uint8_t* packet);
bool CannotMoveAnymorePacket(CSocket* socket, const std::uint8_t* packet);
bool MoveWithDeltaPacket(CSocket* socket, const std::uint8_t* packet);
bool GetOnVehiclePacket(CSocket* socket, const std::uint8_t* packet);
bool GetOffVehiclePacket(CSocket* socket, const std::uint8_t* packet);
bool TargetUserFromMenu(CSocket* socket, const std::uint8_t* packet);
bool MoveToLocationInVehiclePacket(CSocket* socket, const std::uint8_t* packet);
bool CannotMoveAnymoreInVehiclePacket(CSocket* socket, const std::uint8_t* packet);
bool WarehouseDepositPacket(CSocket* socket, const std::uint8_t* packet);
bool WarehouseWithdrawPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestPackageSendableItemList(CSocket* socket, const std::uint8_t* packet);
bool SysCMD2Packet(CSocket* socket, const std::uint8_t* packet);
bool DestroyQuest(CSocket* socket, const std::uint8_t* packet);
bool OpenMinimap(CSocket* socket, const std::uint8_t* packet);
bool GMCommand(CSocket* socket, const std::uint8_t* packet);
bool RequestPackageSend(CSocket* socket, const std::uint8_t* packet);
bool RequestSSQStatus(CSocket* socket, const std::uint8_t* packet);
bool IconAction(CSocket* socket, const std::uint8_t* packet);
bool EnterWorldPacket(CSocket* socket, const std::uint8_t* packet);
bool AppearingPacket(CSocket* socket, const std::uint8_t* packet);
bool RestartPacket(CSocket* socket, const std::uint8_t* packet);
bool StartRotatingPacket(CSocket* socket, const std::uint8_t* packet);
bool FinishRotatingPacket(CSocket* socket, const std::uint8_t* packet);
bool RestartPointPacket(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreListSet(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreManageCancel(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreBuyListSend(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreQuit(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreSetMsg(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreBuyListSet(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreBuyManageCancel(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreBuyQuit(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreBuySetMSG(CSocket* socket, const std::uint8_t* packet);
bool PrivateStoreBuyBuyListSend(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeBookOpen(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeBookItemDelete(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeBookItemInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeBookItemMake(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopSetMsg(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopSetList(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopQuit(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopCancel(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopItemInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopItemMake(CSocket* socket, const std::uint8_t* packet);
bool RequestMultiSellChoose(CSocket* socket, const std::uint8_t* packet);
bool RequestRemainTime(CSocket* socket, const std::uint8_t* packet);
bool ReplyGameGuardInfo(CSocket* socket, const std::uint8_t* packet);
bool RequestMakeMacro(CSocket* socket, const std::uint8_t* packet);
bool RequestDeleteMacro(CSocket* socket, const std::uint8_t* packet);
bool RequestRecipeShopSellList(CSocket* socket, const std::uint8_t* packet);
extern UserPacketCallbackMap gPacketTableFromClient;


using UserSocketAccountMap = ::std::map<std::uint32_t, CUserSocket*>;
using ReadyAccountMap = ::std::map<std::uint32_t, int>;
using AccountLoginStepMap = ::std::map<std::uint32_t, std::int32_t>;
using AccountToNpGuidMap = ::std::map<std::uint32_t, std::string>;
using NpGuidToAccountMap = ::std::map<std::string, std::uint32_t>;

extern AccountLoginStepMap g_accountLoginStep;
extern AccountToNpGuidMap g_account2NPGuidMap;
extern NpGuidToAccountMap g_npGuid2AccountMap;
extern UserSocketAccountMap accountMap;
extern CYieldLock accountMapSect;
extern ReadyAccountMap g_ReadyAccountMap;
extern CYieldLock g_ReadyAccountMapSect;

extern volatile std::int64_t g_nSumOfAvgPacketReadCount;
extern volatile std::int32_t g_nCountOfAvgPacketReadSum;
extern volatile std::int64_t g_nSumOfMaxPacketReadCount;
extern volatile std::int32_t g_nCountOfMaxPacketReadSum;
extern volatile std::int32_t g_nMaxPacketReadCountPerSec[16];

// Current UserSocket.cpp packet-policy storage at 0x21076a60..0x21077170.
// All arrays are one-byte bool tables populated by MakePacketBlockList.
extern bool aPacketActionFailTable[209];
extern bool aPacketExBlockTable_ForRaidUser[514];
extern bool aPacketBlockTable_ForRaidUser[255];
extern bool aPacketExAllowTable_ForRaidUser[352];
extern bool aPacketAllowTable_ForRaidUser[209];
extern bool aPacketBlockTable_CannotControl[209];

extern std::uint64_t g_nCheckInterval;
extern volatile std::int32_t g_nTotalIncomingPacket;
extern std::int32_t g_nMostTimeConsumingJob1;
extern std::uint32_t g_nMostTimeConsumingJob1Time;
extern std::int32_t g_nMostTimeConsumingJob2;
extern std::uint32_t g_nMostTimeConsumingJob2Time;
extern std::int32_t g_nMostTimeConsumingJob3;
extern std::uint32_t g_nMostTimeConsumingJob3Time;


// Current L2Server.exe owns one process-wide limiter at 0x21287660. Its
// 2018 creation expands the matching-source donor's lock and two maps
// to CYieldLock=0x10 and target std::map=0x20, producing this exact 0x50
// envelope.
class CSocketLimiter {
public:
    CSocketLimiter();
    ~CSocketLimiter();

    int IncUserSocketPerIP(std::uint32_t address);
    int DecUserSocketPerIP(std::uint32_t address);
    void DeleteUserSocketPerIP(std::uint32_t address);
    void DeleteAllIP();

    int IncUserSocketPerMAC(std::int64_t address);
    int DecUserSocketPerMAC(std::int64_t address);
    void DeleteUserSocketPerMAC(std::int64_t address);
    void DeleteAllMAC();

private:
    CYieldLock m_lock;
    ::std::map<std::uint32_t, int> m_mapIP;
    ::std::map<std::int64_t, int> m_mapMAC;
};


extern CSocketLimiter g_SocketLimiter;

enum SOCKET_CLOSE_REASON : std::int32_t {
    SCR_NONE = 0,
    SCR_NORMAL = 1,
    SCR_QUIT = 2,
    SCR_RESTART = 3,
    SCR_REMAIN = 4,
    SCR_PENDING = 5,
    SCR_ILLEGAL_BEHAVIOR = 6,
    SCR_RAID_QUIT = 7,
    SCR_RETURN_TO_ORIGIN = 8,
    SCR_AUTH_KICK = 9,
};


namespace CharacterDeactivateReason {
// enum DeactivateReason : std::int32_t {
//     CDR_DEACTIVATE_DEFAULT = 0,
//     CDR_ACTIVATE = 1,
//     CDR_DEACTIVATE_SLOT = 2,
//     CDR_DEACTIVATE_OVER_LEVEL = 3,
//     CDR_DEACTIVATE_CLASS = 4,
// }
} // namespace CharacterDeactivateReason


enum UserRaidStatus : std::int32_t {
    URS_NONE = 1,
    URS_REQUEST_RAID = 2,
    URS_ACCEPTED_REQUEST = 4,
    URS_ON_RAID = 8,
    URS_RETURN_FROM_RAID = 16,
    URS_CLIENT_RETURN = 32,
    URS_RESTART_ON_RAID = 64,
    URS_CLIENT_RESTART = 128,
    URS_MAX = 256,
};


// Current L2Server physical owner. Every named field below has a current
// producer/consumer. The donor-only m_Kind candidate at +0x918 remains in
// an honest four-byte gap because the current image exposes no access to it.
class CUserSocket : public CSocket {
public:
    using GradeSkillMap = ::std::map<int, InfectedSkillType>;

    struct BuilderSetting {
        bool m_bPingCountOff;
        bool m_bNoSeondaryAuth;
    };

    enum SECONDARY_AUTH_STATE : std::int32_t {
        SAS_NONE = 0,
        SAS_REQUIRED = 1,
        SAS_NOT_NEED = 2,
        SAS_COMPLETED = 3,
    };

    CUserSocket();
    explicit CUserSocket(std::uint64_t socket);
    ~CUserSocket() override;

    L2_DECLARE_MEMORY_ALLOCATION(CUserSocket)

    static CSocket* Allocate(std::uint64_t socket);
    static void ItemDebug(User* user, int itemServerId);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    bool CheckPendingWrite() override;
    bool CheckPendingWriteSafe(int pendingWrite) const override;
    void OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const override;
    CUserSocket* CastUserSocket() override;

    virtual User* GetUser() const;
    virtual unsigned int GetServerId() const;
    void IncUserCount(bool insertPlayingSocket);
    void DecUserCount(bool deletePlayingSocket);
    void BindUser(User* user);
    void UnBindUser();
    bool CheckAttendanceTime(bool sendRemainTime);
    bool CheckLoginTime();
    void ProcessQuit();
    void SetAtConstructor(bool active, unsigned int id);
    virtual void SendSystemMessage(const char16_t* context, const char16_t* message);
    void SendSystemMessage(const char16_t* context, const std::u16string& message) {
        SendSystemMessage(context, message.c_str());
    }
    virtual void SendSystemMessage_Char(unsigned int messageId, const char16_t* value);

    void SendSystemMessage(unsigned int messageId);
    void SendSystemMessage_Announce(const char16_t* message);
    void SendSystemMessage_S(unsigned int messageId, const char16_t* value);
    void SendSystemMessage_d(unsigned int messageId, int value);
    void SendSystemMessage_SysString(unsigned int messageId, unsigned int value);
    void SendSystemMessage_Q(unsigned int messageId, std::int64_t value);
    void SendSystemMessage_S_d(unsigned int messageId, const char16_t* stringValue, int integerValue);
    void SendSystemMessage_Friend(unsigned int messageId, const char16_t* value);
    void SendSystemMessage_i(unsigned int messageId, int value);

    void SetApplyChinaFatigueSystem(bool apply);
    bool IsApplyChinaFatigueSystem() { return m_bApplyChinaFatigueSystem; }
    int GetPlayTime_china();
    int GetRestTime_china() { return m_nRestTime_china; }
    void SetRestTime_china(int restTime);
    void SetPlayTime_china(int playTime);
    void ShowFatigueStatus_china();
    void SetSitTimeStart();
    void SetEndSitTimeStart();
    void ShowRemainTime();
    bool IsFullFatigue_china() {
        const std::uint32_t thresholdBits = static_cast<std::uint32_t>(g_ChinaFullFatigueTime) * 3600U;
        std::int32_t threshold = 0;
        std::memcpy(&threshold, &thresholdBits, sizeof(threshold));
        return IsApplyChinaFatigueSystem() && GetPlayTime_china() > threshold;
    }
    bool IsHalfFatigue_china() {
        const std::uint32_t fullThresholdBits = static_cast<std::uint32_t>(g_ChinaFullFatigueTime) * 3600U;
        const std::uint32_t halfThresholdBits = static_cast<std::uint32_t>(g_ChinaHalfFatigueTime) * 3600U;
        std::int32_t fullThreshold = 0;
        std::int32_t halfThreshold = 0;
        std::memcpy(&fullThreshold, &fullThresholdBits, sizeof(fullThreshold));
        std::memcpy(&halfThreshold, &halfThresholdBits, sizeof(halfThreshold));
        return IsApplyChinaFatigueSystem() && GetPlayTime_china() <= fullThreshold && GetPlayTime_china() > halfThreshold;
    }
    int GetSitTime();
    void SetLastSavePlayTime(_SYSTEMTIME* time);
    bool CheckWeekChanged(_SYSTEMTIME* time);
    void SetUseTime(int seconds);
    void SetLeftTime(int leftTime, int userStat, int warnFlag, char mode, int reservations);
    void ChangeTimeLeft(int leftTime, int reservations);
    void ChangeReservation(int reservations);
    void SetSocketUseStart();
    std::int32_t GetSocketUseTime();
    std::int32_t GetSocketCharacterUseTime();
    void SetSocketCharacterUseTime();
    int ClearSocketCharacterUseTime();
    void SetCloseTimer();

    static void MakePacketBlockList();
    static void LoadHtmlCmdHandler();
    void InitCheckPacketAccept();
    bool CheckMaxPacketAccept(int packetNo);
    int GetLastPacketNo() {
        std::uint32_t packed = 0;
        std::memcpy(&packed, &m_nPacketList[static_cast<std::size_t>(GetPrevPacketIndex())], sizeof(packed));
        std::uint32_t resultBits = packed >> 16U;
        if ((resultBits & 0x8000U) != 0) {
            resultBits |= 0xffff0000U;
        }
        std::int32_t result = 0;
        std::memcpy(&result, &resultBits, sizeof(result));
        return result;
    }
    int GetLastEXPacketNo() {
        std::uint32_t packed = 0;
        std::memcpy(&packed, &m_nPacketList[static_cast<std::size_t>(GetPrevPacketIndex())], sizeof(packed));
        return static_cast<int>(packed & 0xffffU);
    }
    void SetLastPacketNo(int packetNo) {
        const std::uint32_t packed = static_cast<std::uint32_t>(static_cast<std::uint16_t>(packetNo)) << 16U;
        std::memcpy(&m_nPacketList[static_cast<std::size_t>(GetCurrPacketIndex())], &packed, sizeof(packed));
        m_nPacketTime[static_cast<std::size_t>(GetCurrPacketIndex())] = runtime::crt::Time32Now();
        ++m_nPacketConunt;
    }
    void SetLastEXPacketNo(short packetNo) {
        std::int32_t& packedValue = m_nPacketList[static_cast<std::size_t>(GetPrevPacketIndex())];
        std::uint32_t packed = 0;
        std::memcpy(&packed, &packedValue, sizeof(packed));
        packed += static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(packetNo)));
        std::memcpy(&packedValue, &packed, sizeof(packedValue));
    }
    static void Decrypt(std::uint8_t* buffer, SocketSecureKey& key, int length);
    void GetShuffleSeedAndPublicKey(int& randomSeed, std::uint64_t& publicKey);
    void InitShuffledPacket();
    void MakeShuffledPacket(int randomSeed);
    void MaintainOriginalPacket(int packet);
    void MaintainOriginalExPacket(int packet);
    int GetOriginalPacket(int shuffledPacket);
    int GetOriginalExPacket(int shuffledPacket);
    bool SafeSend(int pendingWrite, const char* format, ...);
    void ReportPacketLog(bool force);
    void SaveINI(int type, std::string value);
    void SaveINIToDB();
    void SetAccountName(SharedUtilsWideChar* accountName) {
        if (accountName == nullptr) {
            m_sAccountName[0] = SharedUtilsWideChar{};
            errno = EINVAL;
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::size_t length = 0;
            while (length < m_sAccountName.size() && accountName[length] != SharedUtilsWideChar{}) {
                ++length;
            }
            if (length == m_sAccountName.size()) {
                m_sAccountName[0] = SharedUtilsWideChar{};
                errno = ERANGE;
                myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
            } else {
                std::memcpy(m_sAccountName.data(), accountName, (length + 1U) * sizeof(SharedUtilsWideChar));
            }
        }

        static_assert(sizeof(SharedUtilsWideChar) == sizeof(char16_t));
        UnicodeToAnsi(
            reinterpret_cast<char16_t*>(m_sAccountName.data()), static_cast<int>(m_sAccountName.size()),
            m_sAccountNameChar.data(), static_cast<int>(m_sAccountNameChar.size())
        );
    }
    SharedUtilsWideChar* GetAccountName() { return m_sAccountName.data(); }
    char* GetAccountNameChar() { return m_sAccountNameChar.data(); }
    void SetPCCafeCode(const char* code) {
        if (code == nullptr) {
            m_sPCCafeCode[0] = '\0';
            errno = EINVAL;
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::size_t length = 0;
            while (length < m_sPCCafeCode.size() && code[length] != '\0') {
                ++length;
            }
            if (length == m_sPCCafeCode.size()) {
                m_sPCCafeCode[0] = '\0';
                errno = ERANGE;
                myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
            } else {
                std::memcpy(m_sPCCafeCode.data(), code, length + 1U);
            }
        }

        AnsiToUnicode(code, 8, m_swPCCafeCode.data(), 8);
    }
    const char* GetPCCafeCode() { return m_sPCCafeCode.data(); }
    const SharedUtilsWideChar* GetPCCafeCodeW() { return m_swPCCafeCode.data(); }
    bool SetReloadState() { return runtime::raw_atomic::CompareExchangeI32(&m_RaidInfo.m_nIsUnderReloading, 1, 0) == 0; }
    bool IsUnderReloading() { return runtime::raw_atomic::LoadI32Relaxed(&m_RaidInfo.m_nIsUnderReloading) == 1; }
    void SetRaidStatus(UserRaidStatus status) {
        m_RaidInfo.m_eRaidStatus = status;
        m_RaidInfo.m_dRaidStatusTick = runtime::GetTickCount();
    }
    void AddRaidStatus(UserRaidStatus status) {
        m_RaidInfo.m_eRaidStatus = static_cast<UserRaidStatus>(
            static_cast<std::uint32_t>(m_RaidInfo.m_eRaidStatus) | static_cast<std::uint32_t>(status)
        );
        m_RaidInfo.m_dRaidStatusTick = runtime::GetTickCount();
    }
    UserRaidStatus GetRaidStatus() const { return m_RaidInfo.m_eRaidStatus; }
    std::uint32_t GetRaidStatusTick() const { return m_RaidInfo.m_dRaidStatusTick; }
    void SetRaidCharServerId(int serverId) { m_RaidInfo.m_nRaidCharServerId = serverId; }
    int GetRaidCharServerId() { return m_RaidInfo.m_nRaidCharServerId; }
    bool IgnoreTimer() {
        return (static_cast<std::uint32_t>(m_RaidInfo.m_eRaidStatus) &
                   (static_cast<std::uint32_t>(URS_ON_RAID) | static_cast<std::uint32_t>(URS_ACCEPTED_REQUEST))) != 0;
    }
    bool IsCompletelyReturned() {
        return (static_cast<std::uint32_t>(m_RaidInfo.m_eRaidStatus) &
                   (static_cast<std::uint32_t>(URS_RETURN_FROM_RAID) | static_cast<std::uint32_t>(URS_CLIENT_RETURN))) ==
            (static_cast<std::uint32_t>(URS_RETURN_FROM_RAID) | static_cast<std::uint32_t>(URS_CLIENT_RETURN));
    }
    bool IsReadyToRestart() {
        return (static_cast<std::uint32_t>(m_RaidInfo.m_eRaidStatus) &
                   (static_cast<std::uint32_t>(URS_RESTART_ON_RAID) | static_cast<std::uint32_t>(URS_CLIENT_RESTART))) ==
            (static_cast<std::uint32_t>(URS_RESTART_ON_RAID) | static_cast<std::uint32_t>(URS_CLIENT_RESTART));
    }
    SECONDARY_AUTH_STATE GetSecondaryAuthState() const { return m_nSecondaryAuthState; }
    void SetSecondaryAuthState(SECONDARY_AUTH_STATE state) { m_nSecondaryAuthState = state; }
    std::int32_t SetLastUsePacket(int packetId, bool checkUser);
    std::int32_t GetLastUsePacket() { return runtime::raw_atomic::LoadI32Relaxed(&m_nLastUsePacketId); }
    void SetAccountID(std::uint32_t accountId) {
        m_nAccountID = accountId;
        m_bLoggedIn = true;
    }
    std::uint32_t GetAccountID() { return m_nAccountID; }
    bool IsLoggedIn() { return m_bLoggedIn; }
    void SetCharacterAmount(std::uint32_t amount) { m_nCharAmount = amount; }
    std::uint32_t GetCharacterAmount() { return m_nCharAmount; }
    void ForceClose(SOCKET_CLOSE_REASON reason);
    void ResetRelaxTime(_SYSTEMTIME* time);
    bool CheckPremiumType(PremiumUserType::Enum type);
    void SetCharge(bool charge) { m_bAuthCharge = charge; }
    bool GetCharge() { return m_bAuthCharge; }
    int GetAge();
    void SetAge(int age);
    bool IsReadBBSBlocked();
    bool IsWriteBBSBlocked();
    void SetWebSessionId(char* webSessionId);
    int GetCurrentLeftTime();
    bool IsDormantUser();
    bool IsNewbieUser();
    void SetDormantUser(bool enabled, int dormantType);
    void SetDormantMarkingTime(int markingTime, int dormantType);
    int GetDormantMarkingTime() { return m_nDormantMarkingTime; }
    int GetDormantType() { return m_nDormantType; }
    bool CanDoAction_china();
    int GetSocketPlayTime();
    int GetCharacterPlayTime();
    void SetMacAddress(std::int64_t address) { m_nMacAddress = address; }
    std::int64_t GetMacAddress() { return m_nMacAddress; }
    std::u16string GetMacAddressString();
    static bool CharacterCreatePacket(CSocket* socket, const unsigned char* packet);
    static bool CharacterDeletePacket(CSocket* socket, const unsigned char* packet);
    static bool NewCharacterPacket(CSocket* socket, const unsigned char* packet);
    static bool VersionPacket(CSocket* socket, const unsigned char* packet);
    static bool CharacterSelectPacket(CSocket* socket, const unsigned char* packet);
    static bool LoginPacket(CSocket* socket, const unsigned char* packet);
    static bool JoinPledgePacket(CSocket* socket, const unsigned char* packet);
    static bool AnswerJoinPledgePacket(CSocket* socket, const unsigned char* packet);
    static bool JoinPartyPacket(CSocket* socket, const unsigned char* packet);
    static bool AnswerJoinPartyPacket(CSocket* socket, const unsigned char* packet);
    static bool ConfirmDialog(CSocket* socket, const unsigned char* packet);
    static bool HtmlCMDPacket(CSocket* socket, const unsigned char* packet);
    static bool RequestUserCommand(CSocket* socket, const unsigned char* packet);
    static bool RequestNetPing(CSocket* socket, const unsigned char* packet);
    bool DoJoinPledge(User* inviter, User* invitee, PledgeType pledgeType);
    static bool RequestEnchantItemPacket(CSocket* socket, const unsigned char* packet);
    static bool DestroyItem(CSocket* socket, const unsigned char* packet);
    static bool MoveToLocationPacket(CSocket* socket, const unsigned char* packet);
    static bool ValidateLocationPacket(CSocket* socket, const unsigned char* packet);
    static bool DropItemPacket(CSocket* socket, const unsigned char* packet);
    static bool TradeRequestPacket(CSocket* socket, const unsigned char* packet);
    static bool TradeStartPacket(CSocket* socket, const unsigned char* packet);
    static bool TradeAddItemsPacket(CSocket* socket, const unsigned char* packet);
    static bool TradeDonePacket(CSocket* socket, const unsigned char* packet);
    static bool ItemCrystallizePacket(CSocket* socket, const unsigned char* packet);
    bool _LoginPacket(
        SharedUtilsWideChar* accountName, int accountId, int oneTimeKey, int gameId, int loginKey, int countryCode, int pptp
    );
    static void OnLoginSuccess(CUserSocket* socket, int tokenLength, const char* token);
    static void OnLoginTimeout(CUserSocket* socket);
    static void OnLoginFailed(CUserSocket* socket, unsigned int accountId, int errorCode);
    void PopGradeSkill(const CSkillInfo* skillInfo);
    void PushGradeSkill(InfectedSkillType& infectedSkill);
    void PushGradeSkillToUser(InfectedSkillType& infectedSkill);
    void PopGradeSkillFromUser(const CSkillInfo* skillInfo);
    void CopyGradeSkillsToInfectedSkillList();
    void SetInspectorCRC(std::uint32_t crc) { m_crc = crc; }
    std::uint32_t GetInspectorCRC() { return m_crc; }
    std::uint32_t GetQueueTicketAccountID() { return m_nQueueTicketAccountID; }
    void SetQueueTicketAccountID(std::uint32_t accountId) { m_nQueueTicketAccountID = accountId; }
    bool IsPassQueueTicket() { return m_bPassQueueTicket; }
    void SetPassQueueTicket(bool pass) { m_bPassQueueTicket = pass; }
    void AccumulateWorldChatPacket(int count, std::uint32_t size);

protected:
    int GetCurrPacketIndex() { return static_cast<int>(static_cast<std::uint32_t>(m_nPacketConunt) & 0x7fU); }
    int GetPrevPacketIndex() { return static_cast<int>((static_cast<std::uint32_t>(m_nPacketConunt) - 1U) & 0x7fU); }
    void TimerExpired(int id) override;
    void OnRead() override;
    void OnClose() override;

private:
    struct RaidInfo {
        volatile std::int32_t m_nIsUnderReloading;
        UserRaidStatus m_eRaidStatus;
        std::uint32_t m_dRaidStatusTick;
        std::int32_t m_nRaidCharServerId;
    };


    struct HTML_PACKET_QUEUE {
        std::int32_t pushpos;
        std::array<std::uint32_t, 10> dwReceiveTime;
    };


    friend class User;
    friend class RaidServerInfo;
    friend bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);
    friend bool EnterWorldPacket(CSocket* socket, const std::uint8_t* packet);
    friend bool RestartPacket(CSocket* socket, const std::uint8_t* packet);

    static unsigned int s_nBaseId;

    RefHelpers m_RefHelpers; // +0x108..+0x587
    struct HackInfoState {
        std::uint32_t SendListTick;
        volatile std::int32_t EnterWorldHackCheckCount;
    } HackInfo; // +0x588..+0x58f


public:
    std::int32_t m_nAttendanceDelayTime; // +0x590
    volatile std::int32_t m_VipAttendnaceCheck; // +0x594..+0x597

    std::uint32_t m_nId; // +0x598

private:
    std::array<std::byte, 0x04> m_alignment059cTo05a0;

public:
    User* m_pUser; // +0x5a0
    bool m_bUserCountProcessed; // +0x5a8

protected:
    bool m_bAuthCharge; // +0x5a9

private:
    std::array<std::byte, 0x02> m_alignment05aaTo05ac;

public:
    std::uint32_t m_nLeftTime; // +0x5ac
    bool m_bIsNew; // +0x5b0

private:
    std::array<std::byte, 0x03> m_alignment05b1To05b4;

public:
    std::int32_t m_nWeekPlayTime; // +0x5b4
    _SYSTEMTIME m_tmLastSaveWeekPlayTime; // +0x5b8..+0x5c7
    std::int32_t m_nRelaxCheckInterval; // +0x5c8
    std::int32_t m_nPayKind; // +0x5cc
    std::uint32_t m_nUserStat; // +0x5d0
    std::uint32_t m_nWarnFlag; // +0x5d4
    std::uint32_t m_nLoginFlag; // +0x5d8
    std::int32_t m_nReservations; // +0x5dc
    std::int32_t m_nAge; // +0x5e0
    bool m_bForceLogin; // +0x5e4
    bool m_b22to6DisableGameForceLogin; // +0x5e5
    bool m_bEconomicRestriction; // +0x5e6

private:
    std::byte m_alignment05e7To05e8;

public:
    std::int32_t m_iPremiumLevel; // +0x5e8
    SOCKET_CLOSE_REASON m_nCloseReason; // +0x5ec
    std::uint32_t m_nLastTimeCheck; // +0x5f0
    volatile std::int32_t m_nRecvPingCount; // +0x5f4..+0x5f7
    bool m_bActive; // +0x5f8

private:
    std::array<std::byte, 0x03> m_alignment05f9To05fc;

public:
    CYieldLock mLock; // +0x5fc..+0x60b

    std::array<std::int32_t, 8> m_nCharacterDBIDs; // +0x60c..+0x62b
    std::array<std::int32_t, 8> m_nCharacterPrevServers; // +0x62c..+0x64b
    std::array<std::array<char16_t, 24>, 8> m_sCharacterOriginNames; // +0x64c..+0x7cb

    std::array<CharacterDeactivateReason::DeactivateReason,
        8> m_eCharacterDeactivateReasons; // +0x7cc..+0x7eb

    // Current LoadItemsPacket indexes all 63 entries at +0x7ec. The
    // original PDB identifier is m_nSlotItemDBID (donor +0x7dc). The current
    // CUserSocket prefix is 0x10 bytes larger before this field.
    std::array<std::int32_t, 63> m_nSlotItemDBID; // +0x7ec..+0x8e7

    std::int32_t m_nMaxCharId; // +0x8e8
    std::int32_t m_nMaxLevel; // +0x8ec

    std::int32_t m_ChargeStart; // +0x8f0
    bool m_bReenterWorld; // +0x8f4

private:
    std::array<std::byte, 0x03> m_alignment08f5To08f8;

public:
    std::int32_t m_nLeftTimeStamp; // +0x8f8

protected:
    std::int32_t m_nUseTimeSec; // +0x8fc
    std::int32_t m_nSocketUseTimeStamp; // +0x900
    std::int32_t m_nSocketCharacterTimeStamp; // +0x904
    std::int32_t m_nLastActionStamp; // +0x908
    std::int32_t m_nSocketCloseTimer; // +0x90c
    volatile std::int32_t m_nLastUsePacketId; // +0x910

    std::int32_t m_OldTime; // +0x914

private:
    std::array<std::byte, 0x04> m_partialKind0918To091c;

protected:
    std::int32_t m_nLastSayTime; // +0x91c

    std::uint32_t m_nCharAmount; // +0x920

    std::array<SharedUtilsWideChar, 0x0f> m_sAccountName; // +0x924..+0x941
    std::array<char, 0x0f> m_sAccountNameChar; // +0x942..+0x950

private:
    std::array<std::byte, 0x03> m_alignment0951To0954;

protected:
    std::uint32_t m_nAccountID; // +0x954
    bool m_bLoggedIn; // +0x958

private:
    std::array<std::byte, 0x03> m_alignment0959To095c;

protected:
    std::int32_t m_nSitTime; // +0x95c
    std::int32_t m_nSitTimeStamp; // +0x960
    std::int32_t m_nRestTime_china; // +0x964
    std::int32_t m_nPlayTime_china; // +0x968
    std::int32_t m_nStartPlayTimeStamp_china; // +0x96c
    bool m_bApplyChinaFatigueSystem; // +0x970

private:
    std::array<std::byte, 0x03> m_alignment0971To0974;

protected:
    std::array<std::int32_t, 0x80> m_nPacketList; // +0x974..+0xb73
    std::array<std::int32_t, 0x80> m_nPacketTime; // +0xb74..+0xd73

private:
    std::array<std::byte, 0x04> m_alignment0d74To0d78;

protected:
    std::uint64_t m_nPacketConunt; // +0xd78
    std::uint32_t m_nLastPacketLogTick; // +0xd80
    std::array<char, 0x09> m_sPCCafeCode; // +0xd84..+0xd8c

private:
    std::byte m_alignment0d8dTo0d8e;

protected:
    std::array<SharedUtilsWideChar, 0x09> m_swPCCafeCode; // +0xd8e..+0xd9f

public:
    BuilderSetting m_BuilderSetting; // +0xda0..+0xda1

private:
    std::array<std::byte, 0x06> m_alignment0da2To0da8;

protected:
    ::std::vector<int> m_vShuffledPacket; // +0xda8..+0xdbf

private:
    std::array<std::byte, 0x08> m_partialGap0dc0To0dc8;

protected:
    ::std::vector<int> m_vShuffledExPacket; // +0xdc8..+0xddf

private:
    std::array<std::byte, 0x08> m_partialGap0de0To0de8;

public:
    std::uint8_t m_CountryCode; // +0xde8

private:
    std::array<std::byte, 0x03> m_partialGap0de9To0dec;

public:
    std::uint32_t m_crc; // +0xdec

    std::int32_t m_MaxPacketReadCountPerTick; // +0xdf0
    std::int32_t m_SumCount; // +0xdf4
    std::int32_t m_PacketReadCount; // +0xdf8
    std::int32_t m_SumPacketReadCountPerTick; // +0xdfc
    std::uint32_t m_lastPacketReadTick; // +0xe00

    std::int32_t m_UseSendGMListCount; // +0xe04
    std::int32_t m_LastUseSendGMListTime; // +0xe08

private:
    std::array<std::byte, 0x04> m_alignment0e0cTo0e10;
    std::int64_t m_nMacAddress; // +0xe10
    std::int32_t m_nTimeForChangingMergedName; // +0xe18
    RaidInfo m_RaidInfo; // +0xe1c..+0xe2b

protected:
    SECONDARY_AUTH_STATE m_nSecondaryAuthState; // +0xe2c

private:
    HTML_PACKET_QUEUE kHtmlPacketQueue; // +0xe30..+0xe5b
    std::array<char, 0x10> m_WebSessionId; // +0xe5c
    std::uint32_t m_wsidReceivedTick; // +0xe6c
    std::int32_t m_nDormantMarkingTime; // +0xe70
    std::int32_t m_nDormantType; // +0xe74

public:
    std::uint32_t m_nQueueTicketAccountID; // +0xe78

private:
    void _PushGradeSkill(InfectedSkillType& infectedSkill);

    bool m_bPassQueueTicket; // +0xe7c

public:
    std::array<char, 0x25> m_szNPUserGUIDA; // +0xe7d..+0xea1

private:
    std::array<std::byte, 0x06> m_alignment0ea2To0ea8;

public:
    GradeSkillMap m_mGradeSkillList; // +0xea8..+0xec7
    ::CYieldLock m_GradeSkillLock; // +0xec8..+0xed7

    std::array<std::array<char, 0x5000>, 3> m_szClientINI; // +0xed8..+0xfed7
    std::array<bool, 3> m_bNeedUpdateClientINI; // +0xfed8..+0xfeda

private:
    std::array<std::byte, 0x05> m_partialGap0fedbTofee0;
};

// The current null-object is the complete CUserSocket plus the separate
// Singleton<NullUserSocket> vptr at +0xfee0. It is noisy: every
// operational virtual reports invalid use while AddRef/Release keep returning
// the process singleton instead of changing its lifetime.
class NullUserSocket final : public CUserSocket, public Singleton<NullUserSocket> {
public:
    NullUserSocket() = default;
    ~NullUserSocket() override = default;

    void Send(const char* format, ...) override;
    void Send(S_PROTOCOL protocol, network::IPacket& packet) override;
    void Send(S_PROTOCOL protocol, S_EX_PROTOCOL extendedProtocol, network::IPacket& packet) override;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static NullUserSocket* GetNullUserSocket();
    static void InitializeNullUserSocket();

protected:
    void TimerExpired(int id) override;
    void OnClose() override;
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
    void OnRead() override;
};


// Current UserSocket.cpp owns this exact process-global specialization at
// 0x21077630. DB replies address live sockets by their opaque socket ID and
// keep hits with ObjectRefType 26 while dispatching the client packet.
using UserSocketIdMap = CObjectMap<CUserSocket, 8192, 26, RWLock, int>;
using UserSocketPlayingMap = CObjectMap<CUserSocket, 8192, 27, RWLock, int>;
using UserSocketEnterCharMap = CObjectMap<CUserSocket, 8192, 28, RWLock, int>;

extern UserSocketIdMap g_UserSocketMap;
// Current UserSocket.cpp owner starts this second map at 0x21127640.
extern UserSocketPlayingMap g_UserSocketMapPlaying;
// Current UserSocket.cpp owner at 0x211d7650. Accounts remain here from the
// enter-character transition until ProcessQuit removes the socket.
extern UserSocketEnterCharMap g_UserSocketMapEnterChar;
