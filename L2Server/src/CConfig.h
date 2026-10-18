#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class User;
class CUserSocket;

// Current config.obj Setting carriers. The pinned image fixes every write
// below in CConfig::LoadSettingSection and their consuming reads. The
// matching TotalChecker2015 config.obj supplies the original spellings.
extern bool g_bSnoop;
extern bool g_bGotDamageLog;
extern bool g_bDisable18Age;
extern bool g_bUserPacketMonitor;
extern bool g_bRunTestCase;
extern bool g_bShowTestCaseProgress;
extern bool g_bEnableDirectCommand;
extern bool g_bProcessUIEventCount;

// Current config.obj ports at 0237908c/02379064/0237903c/02379014. The
// matching-source PDB supplies the original names. Current setting reads and
// listener consumers close all four storage contracts.
extern int g_nUserPort;
extern int g_nNPCPort;
extern int g_nRaidPort;
extern int g_nOlympiadPort;

// Current config.obj bytes 015cd0ea/02379514/02379542. The exact setting
// reads are UseVehicle@005c95b7 (default true),
// UseServerStartMail@005ca5a6 (default false), and
// testeventserver@005cafad (default false). ListenThread_common consumes all
// three after LoadData.
extern bool g_bUseVehicle;
extern bool g_bUseServerStartMail;
extern bool g_bTestEventServer;

// Current config.obj storage 02379480. CConfig::LoadSettingSection reads
// Setting/newinspectorversion at 005c9e6e into this 50-target-wchar array.
// CNewInspectorDB::Load consumes it directly at 0087d546.
extern SharedUtilsWideChar g_newinspectorversion[50];

// Current config.obj byte 023794f1. The image initializes it to zero.
// BuilderCmdHandler toggles it and the world-chat paths consume it directly.
extern bool g_bQuietWorld;

// Current config.obj chat carriers. The PE initializes LogMsn and
// ChatLimitByOverChat to true, while the five level limits and the
// reply-only switch start at zero. CConfig::LoadSettingSection republishes
// Setting.LogMsn at 0x005ca112 and ChatLimit values at
// 0x005ca784..0x005ca937.
extern bool g_bLogMsnMsg;
extern int g_nChatLimitTarget;
extern int g_nChatLimitLevelAll;
extern int g_nChatLimitLevelNormal;
extern int g_nChatLimitLevelShout;
extern int g_nChatLimitLevelMarket;
extern int g_nChatLimitLevelWhisper;
extern bool g_bChatLimitWhisperReplyOnly;
extern bool g_bChatLimitByOverChat;

// Current config.obj storage 0x015cd08c. CConfig::Load publishes the
// World/BotInspectorTestCount root integer read at 0x005cbe45. Both the
// selected-character legacy inspector path and User::EnterWorld consume it.
extern int g_nBotInspectorTestCount;
// Current config.obj int32 at 0x02379254. CConfig::Load normalizes BOT/block
// to zero or one before selecting the exact Bot Inspector startup log.
// TotalChecker2015 supplies the original physical spelling.
extern int g_nBOTInspectorBlock;

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <netinet/in.h>
#endif

// Current address 015cd18b. The replacement spelling is corroborated by
// the exact TotalChecker2015 config.obj donor. The current PE independently
// proves the one-byte initialized value and every consuming read.
extern bool g_bSendSummonHPMPInfo;

// Current config.obj byte 0x015cd04a. The PE initializes it to true and
// CConfig::LoadSettingSection republishes Setting.canrolldice (default 1).
extern bool g_bCanRollDice;

// Current address 015cd108. CConfig::LoadSettingSection publishes
// Setting.DistanceBroadcastingHPMP (default 600) at 005ca5da. The current
// creature broadcast paths consume the live value instead of a literal.
extern int g_nDistanceBroadcastingHPMP;

// Current config.obj byte at 0x015cd107. The PE initializes it to true.
// CConfig::LoadSettingSection republishes Setting/NpcExpReset at 0x005cadba.
extern bool g_bNpcExpMatchingByUserExpTable;

// Current config.obj storage 015cd105/015cd10c. LoadSettingSection reads
// Setting.PKKarmaPenalty (default true) and Setting.PKPenaltyCount (default
// four) at 005ca73a/005ca6ac. The death/karma path consumes these exact
// process-wide carriers.
extern bool g_bPKKarmaPenalty;
extern int g_nPKPenaltyCount;

// Setting.InterludeKarma (portable addition, default false): no reputation
// above zero, and killing a player killer counts as a PvP kill instead of
// raising the killer's reputation.
extern bool g_bInterludeKarma;

// Current pet-death policy carriers. LoadEventSection publishes the direct
// Event/dontdropitem integer at 0x005c6d06. LoadSettingSection publishes
// Setting/DiePenalty and Setting/PKPenaltyDropItemLimit at
// 0x005c942d/0x005ca708.
extern int g_nNotDropItemOnUserDie;
// Current 4-byte carrier at 0x0237922c. LoadEventSection normalizes
// Event/dontdecexp into this original config.obj global.
extern int g_nNotDecExp;
extern int g_nMaxDropItemLimit;
extern bool g_bDiePenalty;

// Current address 02379502. Setting.MultiLang is read by the exact setting
// loader at 005c9f8a and consumed by the HTML cache manager.
extern bool g_bMultiLang;

// Current config.obj byte 02379504. CConfig::LoadSettingSection publishes
// Setting/IsMergedServer at 005c95da by normalizing the int32 value to bool.
// CPledge::SetProperty consumes the byte directly.
extern bool g_bMergedServer;

// Current config.obj byte 02379505. CConfig::LoadSettingSection publishes
// the nonzero Setting/ChangeWrongCharName int32 at 005c95fd. Character
// selection consumes it together with the merged-server rename gate.
extern bool g_bChangeWrongCharName;

// Current address 02379506. LoadSettingSection publishes the normalized
// Setting/TestServerPenalty value before CPledge::SetPledgePenalty applies
// the current test-server timeout table.
extern bool g_bTestServerPenalty;

// Current bytes 02379509..0237950b. The root loader publishes the three
// HtmlLinkCheck switches at 005cbeaf/005cbef6/005cbf3d.
extern bool g_bHtmlLinkCheck;
extern bool g_bLogOnInvalidLink;
extern bool g_bKickOnInvalidLink;

// Current address 015cd0f0. The current PE fixes the initialized value and
// the LoadEventSection producer plus every consuming read. The matching
// TotalChecker2015 config.obj donor keeps the original symbol spelling.
extern double g_dPledgeNameValuePer;

// Current address 015cd0a0. CConfig::LoadEventSection reads
// Event/SpoilRate (default 100) and publishes the normalized multiplier.
extern double g_dSpoilItemDropRatePer;

// Current addresses 015cd090/015cd098. CConfig::LoadEventSection publishes
// Event/AdenaAmountRate and Event/ItemRate as normalized multipliers.
extern double g_dAdenaAmountPer;
extern double g_dItemDropRatePer;
extern double g_dVitalItemDropPenalty;
extern bool g_bDropAdenaToInventory;

// Current address 02379230. CConfig::LoadEventSection publishes
// Event/AiAdenaRate as a normalized multiplier. NC 0x3d applies it only to
// positive quest-related adena grants.
extern double g_dAiAdenaRate;

// Current address 015cd0f8. LoadEventSection publishes the normalized
// Event/AdjustedGoalRate value. CEvent_AgathionDrop::Load consumes it.
extern double g_dEventAdjustGoalRate;

// Current config.obj storage 015cd100. CConfig::Load reads
// DynamicQuest/AdjustedGoalRate (default 100) at 005cc843. Generated
// dynamicquest parser action 33 consumes the unsigned percentage directly.
extern unsigned int g_DQuestGoalAdjRate;

// Current config.obj storage used by CContributeData's complete EXP/SP
// distribution family. LoadEventSection publishes the two normalized event
// rates and the three level-difference controls at 005c6ca2/005c6cd4 and
// 005c6f0c..005c6f9d. The fixed bonus/penalty tables are the literal current
// .rdata images at 01098ad0 and 010988f0.
extern double g_dExpSPGetRate;
extern double g_dPartyExpSPGetRate;
// Current initialized double at 015cd0b8. LoadEventSection publishes
// Event/aiexprate through an unsigned percentage conversion. The current
// store and consumers close the carrier. TotalChecker2015 supplies the
// original config.obj spelling.
extern double g_dAIGiveEXPRate;
extern int g_MAX_LEVELDIFF_PARTY_GIVEN_EXP;
extern int g_nPartyLevelDiffExpPenaltyIndex;
extern int g_MAX_LEVELDIFF_MPCC_GIVEN_EXP;
extern double g_PartyBonus[8];
extern double LEVELDIFF_EXP_RATE_origin[6][10];

// Current root-loader bytes at 02379560/015cd190. The normalized string
// inputs are Localization.MPCCRoutingOfContribute and Setting.UsePledgeBonus.
extern bool g_bSetMPCCRoutingOfContribute;
extern bool g_bUsePledgeBonus;
extern bool g_bDoNotUseInClassic_ExpPenalty;

// Current CConfig callsites 005ca6cf/005cca66 publish these values. Current
// User consumers fix their storage and the matching-source donor their names.
extern bool g_bPKPenaltyItemDrop;
extern double g_dDropPenaltyRate;
extern int g_nPremiumUserLevel;

// Current config.obj storage 015cd154 is initialized to three. CConfig::Load
// reads Localization/CurrentAllianceMemberLimit with default three at
// 005cca90 and applies only the signed upper clamp min(value, 12).
extern int g_CurrentAllianceMemberLimit;

// Current config.obj storage 0x02379538/0x015cd224. CConfig::Load publishes
// the two Localization integers at 0x005ccbb4/0x005ccce4. The matching-source
// donor supplies only the original spellings. The current stores and readers
// close their physical owners and widths.
extern int g_EasyPK;
extern int g_nPledgeLevelForPledgeGameAdmission;

struct PremiumUserBoost {
    std::int32_t expSpBoostMinLevel;
    double expBoost;
    double spBoost;
    double spoilBoost;
    double normalItemDropBoost;
    double vitalItemDropBoost;
};

extern PremiumUserBoost g_PremiumUserBoost;
extern int g_PremiumUserBuff;
extern int g_nPremiumItem[10];
extern bool g_PCCafeBonusBuff;
extern int g_nPKCountForFreeUser;
extern int g_nPKCountForPremiumUser;
extern int g_nPrivateStoreSellLimitForFreeUser;
extern int g_nPrivateStoreSellLimitForPremiumUser;
extern bool g_bCanPrivateStoreBuyForFreeUser;
extern bool g_bCanPrivateStoreBuyForPremiumUser;
extern bool g_bCanRegisterCommissionForFreeUser;
extern bool g_bCanRegisterCommissionForPremiumUser;
extern bool g_bCanCreatePledgeForFreeUser;
extern bool g_bCanCreatePledgeForPremiumUser;
extern bool g_bCanItemAttachedPostForFreeUser;
extern bool g_bCanItemAttachedPostForPremiumUser;
extern bool g_bDailyPostLimitForFreeUser;
extern bool g_bCanBeMentorForFreeUser;
extern bool g_bCanBeMentorForPremiumUser;
extern int g_nPostFeeBase;
extern int g_nPostFeePerSlot;
extern bool g_bRecipeGiveBack;
extern double g_dRecipeGiveBackPer;

// Current config.obj BSS at 02379570/02379608. NPC extended opcode 0x29
// republishes the shared penalty and inserts each received quest ID with the
// same truncating integer penalty value.
extern double g_dF2PQuestRewardPenaltyPer;
extern std::map<int, int> g_mapF2PRewardPenaltyQuests;

// Current config.obj xvector at 023795c8..023795e7. SetAdminIP@005c918c
// appends the nonempty comma-delimited Setting/AdminIP tokens without
// trimming. VersionPacket's ServerStopper authentication reads the list.
extern std::vector<std::u16string> g_AdminIP;

// Current config.obj map at 023795e8. CommCheckVersion publishes the
// world-number/name pairs consumed by CParseHTML::GetWorldName.
extern std::map<int, std::u16string> g_mapWorldName;

// Current config.obj L2COMM carriers. Their startup values are overwritten
// by CConfig::Load from the L2COMM section before the Comm worker is started.
extern ::in_addr commServerAddr;
extern std::uint16_t commServerPort;
extern std::int32_t commAutoReconnect;
extern std::int32_t commUseSvr;

// Current config.obj byte at 015cd04b. CConfig::Load publishes
// BBS.ShowSharedWritableBBS and CCommunity::GetBoardList consumes it.
extern bool g_bShowSharedWritableBBS;
extern int g_nBBSWriteLevel;

// Current config.obj storage 02379250/02379260. The filename is a target
// WCHAR[256], not a host-wchar array on non-Windows builds.
extern int g_nFontCheck;
extern char16_t g_sFontCheckName[256];

namespace F2PConfig {

bool IsAllCharacterSlotUsableAccount(CUserSocket* socket);
int GetUsableCharacterSlotCount(CUserSocket* socket);
int GetVitalityBonus(CUserSocket* socket);
int GetVitalityItemMaxCount(CUserSocket* socket);
int GetVitalityItemUsableCount(CUserSocket* socket, int usedCount);
bool IsContainPCCafeUserBonus(CUserSocket* socket);
bool CanTakePremiumItem(User* user);
int GetPKLimitCount(const User* user);
int GetPKRemainCount(const User* user);
bool CheckPKCount(const User* user);
int GetPrivateStoreSellLimit(User* user);
bool CanOpenPrivateStoreBuy(User* user);
bool CanRegisterCommission(User* user);
bool CanCreatePledge(User* user);
bool CanItemAttatchedPostUser(User* user);
bool IsDailyPostLimitUser(User* user);
bool CanBeMentor(User* user);
double GetExpBoostRate(User* user);
double GetSpBoostRate(User* user);
double GetSpoilBoostRate(User* user);
double GetNormalItemDropBoostRate(User* user);
double GetVitalItemDropBoostRate(User* user);
int GetPremiumUserBuffSkillId(User* user);

} // namespace F2PConfig


// Current CConfig::Load writes these exact config.obj globals from
// LogD/address, LogD/port and LogD/FileDuration. Shared/logcli.cpp consumes
// the address and port directly. CLog::Init receives the duration together
// with g_nWorldId.
extern ::in_addr logServerAddr;
extern short logServerPort;
extern int g_LogFileDuration;
extern int g_nWorldId;
// Current address 02fd4302. CConfig::Load normalizes
// World/UseCustomEquipment at reader callsite 005cbbd7 and publishes it at
// 005cbbef. The setting parser selects the matching initial-equipment block.
extern bool g_bUseCustomEquipment;
// Current address 02fd4303. CConfig::Load publishes the normalized
// World/UseLecture root read at 005cbc21 into this one-byte switch.
extern bool g_bUseLecture;

// Current CConfig::LoadSettingSection writes the two Setting values consumed
// by CUserSocket's dormant/newbie window checks. TotalChecker2015 config.obj
// supplies the original spellings. Current callsites 005cadfc/005cae1f fix
// the keys, defaults and int32 carrier.
extern int g_DormantPostponeTime;
extern int g_NewbiePostponeTime;

// Current CConfig::LoadSettingSection reads Setting/MentorCanclePenalty and
// Setting/MentorGraduatePenalty as int32 values at 005cafdd/005caffd. The
// matching-source config.obj supplies these original public spellings.
extern int g_nMentorCancelPanelty;
extern int g_nMentorGraduatePanelty;

// Current CConfig::Load AuthD roots at 005cb58e/005cb5c1/005cb5e4 write
// these three int32 globals. The current image fixes their initial values,
// normalization and every China-fatigue consumer. The matching-source donor
// supplies the original public system-type spelling.
extern int g_nChinaFatigueSystemType;
extern int g_ChinaHalfFatigueTime;
extern int g_ChinaFullFatigueTime;

// Current CConfig::LoadSettingSection reads Setting/playtimelimit at
// 005c9a72 and multiplies a positive hour value by 3600 with int32 wrapping.
// Current CConfig::Load reads FreeToPlay/FreeOpenServer at 005cc9c0 and
// normalizes only the string "true" to true. The matching-source donor
// supplies the original public spellings. Current consumers prove both
// contracts and process-global storage locations 02378fec/0237925e.
extern int g_nPlayTimeLimit;
// Current config.obj writes immediately following playtimelimit. The
// current image fixes the int32/bool stores and CL2Time consumers. The
// matching TotalChecker config.obj symbols supply the original spellings.
extern int g_nPlayTimeChanged;
extern bool g_bNewVote;
extern bool g_bFreeOpenServer;

// Current config.obj VersionPacket gates. LoadSettingSection publishes
// Setting/ServerStopperEnabled, ServerStopperKey and AcceptLowerProtocol at
// 005c970e/005c973e/005c9a3b. The key is the target unsigned-int carrier.
extern bool g_bServerStopperEnabled;
extern std::uint32_t g_nServerStopperKey;
extern bool g_bAcceptLowerProtocol;

// Current bytes 02fd4326/015d0db4 start as false/one. The setting loader
// publishes normalized Setting/newcharactercreate at reader/cmp/store
// 005c9ee4/005c9ef1/005c9ef8 and the unmodified Setting/CreateLevel int32
// at reader/store 005c9f17/005c9f1c. CharacterCreatePacket reads them at
// 00c12196/00c12342.
extern bool g_bNewCharacterCreate;
extern int g_nCharacterCreateLevel;

// Current config.obj storage 015cd0c0. LoadSettingSection publishes the
// unnormalized Setting/EscapeMode int32 at 005c9cb9. The escape basic action
// and BuilderCmd_escape_mode share this live carrier.
extern int g_nEscapeMode;

// Current config.obj bytes 0237925b..0237925d. LoadSettingSection publishes
// Setting/ProhibitCharCreation, ProhibitCharDeletion and
// ProhibitFirstCharCreation at 005ca17a/005ca1c0/005ca206. The create and
// delete packet gates consume these exact process-wide bytes.
extern bool g_bProhibitAllCharCreation;
extern bool g_bProhibitAllCharDeletion;
extern bool g_bProhibitFirstCharCreation;

// Current config.obj storage 015cd0c4. LoadSettingSection reads
// Setting/ProhibitCharCreationCount with default 7 and selects 7 for every
// signed value outside [0, 7].
extern int g_nProhibitCharCreationCount;
extern std::int32_t g_nCheckCastleId;

// Current CConfig::Load reads Setting/PacketRepeatLimit and
// PacketRepeatLimitTime at 005cc344/005cc3a6. The former is clamped to
// [0,10], the latter to a non-negative int32. DropItemPacket consumes both.
extern int g_nPacketRepeatLimit;
extern int g_nPacketRepeatLimitTime;

// Current config.obj byte 023794f0. CConfig::Load reads
// Setting/FreeTeleportNeedItem at 005cc403 and publishes the normalized
// boolean consumed by the user-bookmark save path.
extern bool g_bFreeTeleportNeedItem;

// Current config.obj storage 0237947c/015cd0c8. The exact setting loader
// publishes Setting.MaxSpawnNumPerOneTick and MaxDespawnNumPerOneTick with
// defaults 100/500. NpcSocket's enter/remove serializers consume them.
extern int g_nMaxSpawnNumPerOneTick;
extern int g_nMaxDespawnNumPerOneTick;

// Current UserSocket heartbeat/session globals. LoadSettingSection publishes
// these from autologoutsec, taitime, gameguard_builder_log, PingCountOff and
// PingOnRaid at 005c9870/005c9b72/005c9c86/005ca4dc/005ca522.
extern int g_nAutoLogoutTimeLimit;
extern bool g_b22to6DisableGame;
extern bool g_bBuilderLogAdd;
extern bool g_bPingCountOff;
extern bool g_bPingOnRaid;

// Current config.obj storage 015cd088. LoadSettingSection publishes the
// signed Setting/BotUnseleteTime int32 (default 500) at 005c9893. User's
// repeated-target gate compares the wrapped GetTickCount delta to it.
extern int g_dBotUnseleteTime;

// AuthSocket::AuthRequestAboutToPlay consumes these current config.obj
// values directly while admitting, marking, or queueing an account.
extern int g_nAgeCheckType;
extern int g_DormantUserDay;
extern bool g_bNewDormantBitset;
extern bool g_bNotDormantServer;
extern int g_DormantUserGift;
extern int g_nNewbieGiftId;
extern bool g_bSendGiftToNewbie;
extern bool g_bSendGiftToDormant;
extern bool g_bUseAuthTimeManageSystem;
// Current config.obj storage at 02379515. LoadSettingSection normalizes
// Setting/UseAuthUserData at 005ca610. CUserSocket::ProcessQuit selects the
// AuthD logout route from this byte.
extern bool g_bUseAuthUserData;
// Current config.obj storage 0x02379518. LoadSettingSection publishes the
// exact Setting/BirthdayItem int32 at 0x005ca643 (default 0x52b1).
extern int g_nBirthdayItemClassID;
extern bool g_DormantUserSystem;
extern int g_nPremiumLimit;
extern int g_nWaiterMaxCount;
// Current config.obj storage at 015cd19c/02379555/02379556. CConfig::Load
// publishes QueueTicket/PassVipLevel and maps WaiterLevel "normal"/"vip"
// to the exact normal-wait/vip-wait byte pair at 005ccdc0..005cce2c.
extern int g_nQueueTicketPassVipLevel;
extern bool g_bQueueTicketNormalWait;
extern bool g_bQueueTicketVipWait;
extern bool g_bQueueTicketBuilderOff;

// Current config.obj storage at 02378dac/0237921f. LoadSettingSection
// publishes Setting/reconnectAuth to the int32 value. The AuthD connection
// path owns the connected-state transitions. TotalChecker2015 supplies the
// original spellings and source owner.
extern int g_nUseReconnectAuth;
extern bool g_bAuthConnected;
extern bool g_ShutDownSystem;

// Current config.obj CacheD connection carriers at
// 02379104/0237912c and 015cd230/015cd192. CConfig::Load publishes the
// address, truncating port, version-check timeout and crash policy from the
// four CacheD keys at 005cb3f3..005cb498.
extern short cacheServerPort;
extern ::in_addr cacheServerAddr;
extern int g_CacheD_nCheckTimeout;
extern bool g_CacheD_bCheckCrash;

// Current config.obj storage 015cd238..015cd247. The AccountServer loader
// at 005c7b24 publishes the normalized connect flag, inet_addr result,
// truncating signed-short port and heartbeat. ListenThread_common reads the
// same owner before AccountServerStub::Init. CConnectThreadManager later
// consumes only bConnect for the process-lifetime reconnect loop. The
// matching-source PDB supplies the original owner and member spellings.
struct AccountServerSetting {
    bool bConnect;
    ::in_addr serverAddr;
    short serverPort;
    int heartBeat;
};


extern AccountServerSetting g_AccountServerSetting;

// Current config.obj storage 02378db0..02378fbb. The five PetitionD reads at
// 005cb9a6..005cbac8 publish this single physical owner. Current consumers
// close every field offset. The matching-source PDB supplies
// the original owner and member spellings.
struct PetitionStruct {
    unsigned heartBeat;
    ::in_addr serverAddr;
    short serverPort;
    char16_t wsWorldServerName[256];
    bool UseWebPetition;
};


extern PetitionStruct g_PetitionSetting;

// Current config.obj storage at 02379154/0237917c. CConfig::Load reads
// AuthD/port and AuthD/address at 005cb517/005cb4e2, truncates the port with
// the exact word store at 005cb51c and publishes inet_addr's four-byte
// result at 005cb530. The matching-source config.obj donor supplies the
// original public spellings and confirms the in_addr/short types.
extern short authServerPort;
extern ::in_addr authServerAddr;

// Current config.obj storage at 023794fc/02379554. CConfig::Load publishes
// World/AuthServerId at 005cbc59 and the normalized
// QueueTicket/QueueTicketEnabled value at 005ccd65. Current AuthD consumers
// read the low byte of the int32 server id. TotalChecker2015 supplies only
// the original symbol spellings and source owner.
extern int g_nAuthServerID;
extern bool g_bQueueTicketEnabled;
extern bool g_bUseNPLogin;

// Current config.obj byte at 0237955a. It is zero in the PE and has no
// CConfig::Load producer. WndProc command 0x804c toggles it at runtime.
// TotalChecker2015 config.obj supplies the original bool spelling/type.
extern bool g_bDebugNPLogin;

// Current LoadEventSection stores the normalized event/ChargeFree and
// event/ChargeDistinction values in these adjacent config.obj bytes at
// 02379507/02379508. Both current PE initial values are false.
extern bool g_bChargeFree;
extern bool g_bChargeDistinction;

// Current config.obj byte at 02379517. LoadSettingSection normalizes
// Setting/CheckSecondaryAuth at 005cab2d. AuthD and UserSocket consumers read
// the byte directly. TotalChecker2015 supplies the original spelling/type.
extern bool g_bCheckSecondaryAuth;

// Current config.obj storage at 015cd1a0/015cd220. CConfig::Load reads
// AuthD/NPAddress and AuthD/NPPort at 005cb746/005cb787. The address is
// published through UnicodeToAnsi at 005cb76a and the port is truncated by
// the exact word store at 005cb78c.
extern char g_szNPAddress[128];
extern short g_nNPPort;

// CConfig::LoadFreeToPlaySection publishes the two VitalBonus and two
// VitalItemCount tokens here. Current CVitality construction reads all four
// globals directly. The matching-source donor supplies their spellings.
extern int g_nVitalBonusForFreeUser;
extern int g_nVitalBonusForPremiumUser;
extern int g_nMaxRestoreVitalItemCountForFreeUser;
extern int g_nMaxRestoreVitalItemCountForPremiumUser;
// Current config.obj storage 015cd150. LoadFreeToPlaySection publishes the
// unmodified FreeToPlayUserSetting/ActivateCharacterAmount int32 at
// 005c71df. Free accounts use it as their character-slot limit.
extern int g_nActivateCharacterAmountInF2P;
// Current config.obj byte 02fd432a. LoadFreeToPlaySection publishes the
// normalized FreeToPlayUserSetting/PCCafe token here.
extern bool g_bPremiumUserIsPCCafe;

// Current config.obj storage at 015cd140/015cd148 and 02379533/02379534.
// CConfig::Load reads the four FreeToPlay values at 005cc8a9..005cc97a.
// CVitality::GetConsumePoint consumes them directly when calculating the
// current pre/post-renewal mob and boss vitality deltas.
extern double g_dVitalityConsumeByMobPer;
extern double g_dVitalityConsumeByBossPer;
extern bool g_bVitalityByMobBeforeRenewal;
extern bool g_bVitalityByBossBeforeRenewal;

// Current CConfig::LoadSettingSection writes all four values below. The
// current CSocketLimiter and CUserSocket packet-stat paths consume them
// directly. TotalChecker2015 config.obj supplies the original spellings.
extern int g_nMaxUserSocketPerIP;
extern int g_nMaxUserSocketPerMAC;
extern bool g_bUserToServerPacketStatMonitor;
extern bool g_bServerToUserPacketStatMonitor;
extern bool g_bTimeAnalysisMonitor;
extern bool g_bNpcDieStatMonitor;
extern bool g_bNPCBroadcastMonitor;
extern bool g_bCachePingFlag;
extern bool g_bNpcPingFlag;
extern int g_nCachePingTime;
extern int g_nNpcPingTime;

// Current config.obj storage at 023794f4. CConfig::LoadSettingSection reads
// Setting/IPBlock as an int32 at 005ca398. IPBlockManager consumes the value
// as an enabled/disabled gate without normalizing the stored carrier.
extern int g_nIPBlock;

// Current config.obj byte at 0237950f. CConfig::LoadSettingSection reads
// Setting/UseResourceLoader with default false at 005ca40e, compares against
// "true" at 005ca41b and publishes the normalized byte at 005ca422.
extern bool g_bUseResourceLoader;

// Current config.obj byte at 173dd1a9. CConfig::LoadSettingSection reads
// Setting/UserPathFind with default true at 005c93eb and publishes the
// case-insensitive "true" result. CMoveAction::SetMoving consumes it.
extern bool g_bUserPathFind;

// Current config.obj byte at 0x02379513. CConfig::LoadSettingSection reads
// Setting/LoadEventDataFromDB with default false at 0x005ca568 and publishes
// the case-insensitive "true" result. CEventManager uses it to select the
// DB-refreshed eventdata path during reload.
extern bool g_bLoadEventDataFromDB;

// Current config.obj byte at 0x02379561. CConfig::LoadSettingSection reads
// Setting/TeleportToNPCInternal as int32 at 0x005c9620 and publishes the
// nonzero result. CNPC::EnterWorld consumes this exact process-wide gate.
extern bool g_bEnableTeleportToNPCInternal;

// Current config.obj storage at 0x02379468. LoadSettingSection reads the
// signed Setting/BotItem value with default 10213 at 0x005ca2fb. User::GetItem
// consumes this exact class ID for the bot-check pickup path.
extern int g_nBotCheckItemClassID;

// Current config.obj storage at 0x0237946c. LoadSettingSection reads the
// signed Setting/BotItemCount value with default 1 at 0x005ca31e. The
// builder bot-check-item path consumes the published count unchanged.
extern int g_nBotCheckItemCount;

// Current config.obj storage at 0x02379470. LoadSettingSection reads the
// signed Setting/FakeItem value with default 57 at 0x005ca341. The world
// pickup and builder fake-item paths consume this process-wide class ID.
extern int g_nFakeItemClassID;

// Current config.obj byte at 0237950e. CConfig::LoadSettingSection reads
// Setting/UseAutoSoulShotUI with default false at 005c9800 and publishes the
// normalized bool used by the shot and auto-shot paths.
extern bool g_bUseAutoSoulShotUI;

// Current config.obj byte at 0237950d. LoadSettingSection normalizes
// Setting/UseOneDayReward at 005c97ce. DailyMission, DB and packet routes
// consume this exact process-wide feature gate.
extern bool g_bUseOneDayReward;

// Current config.obj storage at 015cd0d0. CConfig::LoadSettingSection reads
// Setting/PPTP with default 1 at 005ca364, stores it at 005ca369 and replaces
// values outside the signed range 1..3 with 1 at 005ca379.
extern int g_nPPTPOption;

// Current address 02379460. CConfig::LoadSettingSection normalizes
// Setting/UsePacketLog to this byte at 005ca2c8. CUserSocket::ReportPacketLog
// reads it before the ten-minute report gate. TotalChecker2015 supplies only
// the original global spelling.
extern bool g_bWritePacketLog;

// Current address 0237950c. CConfig::LoadSettingSection normalizes
// Setting/EnablePacketShuffle with a case-insensitive comparison to "true".
// CUserSocket::GetShuffleSeedAndPublicKey reads the resulting byte directly.
extern bool g_bEnablePacketShuffle;

// Current address 0237955b. CConfig::LoadSettingSection normalizes
// Setting/ClientINI at 005cb0ff and the current CUserSocket ClientINI roots
// consume the resulting byte. TotalChecker2015 supplies only the original
// global spelling.
extern bool g_bProcessClientINI;

// Current addresses 02379562/0237956c. CConfig::LoadSettingSection
// publishes Setting/UseAttendanceSystem and Setting/AttendanceDelayTime.
// CUserSocket::CheckAttendanceTime consumes both values directly.
extern bool g_bUseAttendanceSystem;
extern bool g_bUsePCCafeAttendance;
extern bool g_bUseVipAttendance;
extern int g_nAttendanceMinimumLev;
extern int g_nAttendanceDelayTime;

// Current config.obj storage at 0237957c. LoadSettingSection reads the
// signed Setting/UserStatusLogTime value with default 180 at 005cb238.
// User::UserStatusLog consumes the published timer interval.
extern int g_nUserStatusLogTime;

// Current server.cpp reads this config.obj flag before emitting accepted
// connection diagnostics. Its process image initial value is false.
extern bool bWriteConnLog;

// Current address 02379258. CConfig::LoadSettingSection publishes the
// normalized Setting/isnonpk value here. User's guilty-state overrides read
// this byte directly. TotalChecker2015 config.obj keeps the original name.
extern bool g_bIsNonPKServer;

// Current address 02379259. CConfig::LoadSettingSection publishes the
// normalized Setting/cannotmonrace value (default true). CMonRaceDB's
// heartbeat reads the byte directly.
extern bool g_bCannotMonRace;

// Current config.obj byte 02379531. LoadSettingSection normalizes
// Setting/AllUserPccafe at 005cabc0. User::SetAtConstructor consumes this
// process-wide override before accepting the packet-provided PCCafe flag.
extern bool g_bAllUserPcCafe;

// Current config.obj byte 02379558. LoadSettingSection normalizes
// Setting/UserGarbageCollect at 005cb033. User::ReleaseSocket consumes this
// process-wide switch without widening the stored carrier.
extern bool g_bUserGarbageCollector;

// Current address 0237955c. CConfig::Load reads
// Localization/PartyMatchCreateLevelLimit at 005ccc7a and User consumes the
// resulting int32 value at 00b07b74. TotalChecker2015 config.obj keeps the
// original symbol spelling.
extern int g_nCreatePartyMatchLevelLimit;

// Current config.obj storage at 015cd194. CConfig::Load reads
// Localization/ClanAdventSkill at 005ccc5a (default 19009). Infected-skill
// persistence excludes the configured skill by this mutable process value.
extern int g_nClanAdventSkill;

// Current address 02378558. The current CConfig root writes 666 before the
// gameplay timer paths consume this signed 32-bit heartbeat divisor.
// TotalChecker2015 config.obj keeps the original symbol spelling.
extern int nHeartBeat;

struct GoodsInventoryStruct {
    bool enable; // +0x00
    unsigned heartBeat; // +0x04
    ::in_addr serverAddr; // +0x08
    short serverPort; // +0x0c
};


// Current address 02378fc0. GoodsInventory.cpp consumes this exact aggregate
// for its connection thread, protocol-version request and heartbeat.
extern GoodsInventoryStruct g_GoodsInventorySetting;

struct PremiumStruct {
    bool enable; // +0x00
    unsigned heartBeat; // +0x04
    ::in_addr serverAddr; // +0x08
    short serverPort; // +0x0c
    bool pause; // +0x0e
    bool cashshopPause; // +0x0f
    int purchaseHistoryCount; // +0x10
    int giftLevelLimit; // +0x14
    bool g_bStandAloneShopTest; // +0x18
};

// Current address 02378fd0. CConfig::Load publishes all nine Premium lanes
// in current read/store order. User::ShowPremiumItemList consumes pause at
// +0x0e from this same process-lifetime owner.
extern PremiumStruct g_PremiumSetting;

// Current config.obj byte 0x02fd4329. Premium/CashshopPriceIsTotal selects
// whether the billing request carries the unit price or amount * unit price.
extern bool g_bCashshopPriceIsTotal;

// Current config.obj byte 0x02fd432b. PremiumUser/IsMentorBuffed makes the
// number of online mentees select the mentor-buff level.
extern bool g_bIsMentorBuffed;

struct CaptchaSettingyStruct {
    bool enable; // +0x00
    unsigned heartBeat; // +0x04
    ::in_addr serverAddr; // +0x08
    unsigned short serverPort; // +0x0c
    int reportCntSendCaptcha; // +0x10
    int confirmExpiredSeconds; // +0x14
    int reportDelaySeconds; // +0x18
    int reportReuseDelaySeconds; // +0x1c
    bool canBotReportPrivateStore; // +0x20
    bool canBotReportCraft; // +0x21
};

// Current address 02378578. CConfig::Load creates this complete
// process-lifetime aggregate from the ten Captcha-section reads.
extern CaptchaSettingyStruct g_CaptchaSetting;

struct GIPStruct {
    bool enabled; // +0x00
    unsigned heartBeat; // +0x04
    ::in_addr serverAddr; // +0x08
    short serverPort; // +0x0c
};

// Current config.obj aggregate at 0x02378548. CConfig::Load publishes the
// four GIP lanes and the GIP client consumes the same process-lifetime owner.
extern GIPStruct g_GIPSetting;

struct WorldChat {
    enum Account : int {
        NONE = 0,
        PREMIUM = 1,
        ALL = 2,
    };

    Account eAccount; // +0x00
    int nMinLevForFree; // +0x04
    int nMinLevForPremium; // +0x08
    int nComsumePointForFree; // +0x0c
    int nComsumePointForPremium; // +0x10
    int nMaxChatCnt; // +0x14
    int nIntervalForFree; // +0x18
    int nIntervalForPremium; // +0x1c
    std::int32_t tNextResetTime; // +0x20, target long
    int nQuestID1; // +0x24
};

static_assert(WorldChat::NONE == 0);
static_assert(WorldChat::PREMIUM == 1);
static_assert(WorldChat::ALL == 2);

// Current address 02378520. CConfig::LoadWorldChatSection writes the eight
// configurable lanes and the fixed quest ID while leaving tNextResetTime
// under its runtime owner.
extern WorldChat g_WorldChat;

// Current config.obj owner at 02378500. Current LoadChannelChattingSection
// fixes every store width and offset. The matching-source PDB
// supplies the original aggregate/member spellings.
struct ChannelChatting {
    ::in_addr m_stChannelChattingAddress; // +0x00
    short m_hChannelChattingPort; // +0x04
    ::in_addr m_stChannelChattingClientAddress; // +0x08
    short m_hChannelChattingPortForClient; // +0x0c
    std::uint32_t ip; // +0x10, target ulong
    std::uint32_t clientip; // +0x14, target ulong
    int m_nHeartBeat; // +0x18
    int m_nGameServerID; // +0x1c
};


extern ChannelChatting g_stChannelChattingInfo;

// Current RTTI .?AVCConfig@@, global object config@023795c3, and the complete
// matching-source type record all close a fieldless, non-polymorphic owner.
// Current entry points close the public method surface below.
class CConfig final {
public:
    CConfig();
    ~CConfig();

    void Load();
    void MakeAdminIPList(SharedUtilsWideChar* list);
    void MakeUIEventIDList(SharedUtilsWideChar* list);
    void LoadSettingSection();
    void LoadEventSection();
    void SetEventStartDate(SharedUtilsWideChar* value);
    void LoadWorldChatSection();
    void LoadChannelChattingSection();
    void LoadAccountServerSection();
    void LoadFreeToPlaySection();
};


extern CConfig config;
