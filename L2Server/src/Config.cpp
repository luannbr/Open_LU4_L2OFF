#include "CConfig.h"

#include "BotChecker.h"
#include "Default.h"
#include "Item.h"
#include "MakerEvent.h"
#include "NPC.h"
#include "NPConst.h"
#include "NewActionMove.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "Relay.h"
#include "SecureINIFileReader.h"
#include "ServerType.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/Utils.h"
#include "UIEventCountManager.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

bool g_bSnoop = false;
bool g_bGotDamageLog = false;
bool g_bDisable18Age = false;
bool g_bUserPacketMonitor = false;
bool g_bRunTestCase = true;
bool g_bShowTestCaseProgress = false;
bool g_bEnableDirectCommand = false;
bool g_bProcessUIEventCount = false;

int g_nUserPort = 0;
int g_nNPCPort = 0;
int g_nRaidPort = 0;
int g_nOlympiadPort = 0;
bool g_bUseVehicle = true;
bool g_bUseServerStartMail = false;
bool g_bTestEventServer = false;

SharedUtilsWideChar g_newinspectorversion[50]{};
bool g_bQuietWorld = false;
bool g_bLogMsnMsg = true;
int g_nChatLimitTarget = 2;
int g_nChatLimitLevelAll = 0;
int g_nChatLimitLevelNormal = 0;
int g_nChatLimitLevelShout = 0;
int g_nChatLimitLevelMarket = 0;
int g_nChatLimitLevelWhisper = 0;
bool g_bChatLimitWhisperReplyOnly = false;
bool g_bChatLimitByOverChat = true;
int g_nBotInspectorTestCount = 0;
int g_nBOTInspectorBlock = 0;

bool g_bSendSummonHPMPInfo = true;
bool g_bCanRollDice = true;
int g_nDistanceBroadcastingHPMP = 600;
bool g_bNpcExpMatchingByUserExpTable = true;
bool g_bPKKarmaPenalty = true;
int g_nPKPenaltyCount = 4;
bool g_bInterludeKarma = false;
int g_nNotDropItemOnUserDie = 0;
int g_nNotDecExp = 0;
int g_nMaxDropItemLimit = 5;
bool g_bDiePenalty = true;
bool g_bMultiLang = false;
bool g_bMergedServer = false;
bool g_bChangeWrongCharName = false;
bool g_bTestServerPenalty = false;
bool g_bHtmlLinkCheck = false;
bool g_bLogOnInvalidLink = false;
bool g_bKickOnInvalidLink = false;
double g_dPledgeNameValuePer = 1.0;
double g_dSpoilItemDropRatePer = 1.0;
double g_dAdenaAmountPer = 1.0;
double g_dItemDropRatePer = 1.0;
double g_dVitalItemDropPenalty = 1.0;
bool g_bDropAdenaToInventory = false;
double g_dAiAdenaRate = 0.0;
double g_dEventAdjustGoalRate = 1.0;
unsigned int g_DQuestGoalAdjRate = 100U;
double g_dExpSPGetRate = 1.0;
double g_dPartyExpSPGetRate = 1.0;
double g_dAIGiveEXPRate = 1.0;
int g_MAX_LEVELDIFF_PARTY_GIVEN_EXP = 10;
int g_nPartyLevelDiffExpPenaltyIndex = 0;
int g_MAX_LEVELDIFF_MPCC_GIVEN_EXP = 10;
bool g_bSetMPCCRoutingOfContribute = false;
bool g_bUsePledgeBonus = false;
bool g_bDoNotUseInClassic_ExpPenalty = false;
double g_PartyBonus[8] = {
    1.0,
    1.0,
    1.3,
    1.4,
    1.5,
    1.6,
    1.7,
    2.0,
};
double LEVELDIFF_EXP_RATE_origin[6][10] = {
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.3, 0.3, 0.3, 0.3},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.3, 0.3, 0.3, 0.3},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.3, 0.3, 0.3, 0.3},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.3, 0.3, 0.3, 0.3},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.3, 0.3, 0.3, 0.3},
};
bool g_bPKPenaltyItemDrop = false;
double g_dDropPenaltyRate = 1.0;
int g_nPremiumUserLevel = -1;
int g_CurrentAllianceMemberLimit = 3;
int g_EasyPK = 0;
int g_nPledgeLevelForPledgeGameAdmission = 3;
PremiumUserBoost g_PremiumUserBoost{};
int g_PremiumUserBuff = 0;
int g_nPremiumItem[10]{};
bool g_PCCafeBonusBuff = false;
int g_nPKCountForFreeUser = -1;
int g_nPKCountForPremiumUser = -1;
// Current config.obj storage 015cd17c/015cd180. LoadFreeToPlaySection
// publishes the parsed free/premium pair selected by
// F2PConfig::GetPrivateStoreSellLimit@005c8834.
int g_nPrivateStoreSellLimitForFreeUser = -1;
int g_nPrivateStoreSellLimitForPremiumUser = -1;
bool g_bCanPrivateStoreBuyForFreeUser = true;
bool g_bCanPrivateStoreBuyForPremiumUser = true;
// Current config.obj storage 015cd184..015cd185, selected by
// F2PConfig::CanRegisterCommission@005c89b4.
bool g_bCanRegisterCommissionForFreeUser = true;
bool g_bCanRegisterCommissionForPremiumUser = true;
// Current config.obj storage 015cd186..015cd187, selected by
// F2PConfig::CanCreatePledge@005c8a74.
bool g_bCanCreatePledgeForFreeUser = true;
bool g_bCanCreatePledgeForPremiumUser = true;
// Current config.obj storage 015cd188..015cd18a. The first two bytes are
// selected by CanItemAttatchedPostUser@005c8b34. The third is consumed by
// the post DB request/reply paths together with IsDailyPostLimitUser.
bool g_bCanItemAttachedPostForFreeUser = true;
bool g_bCanItemAttachedPostForPremiumUser = true;
bool g_bDailyPostLimitForFreeUser = true;
// Current config.obj storage 015d0dc8..015d0dc9, selected by
// F2PConfig::CanBeMentor@005c8e30.
bool g_bCanBeMentorForFreeUser = true;
bool g_bCanBeMentorForPremiumUser = true;
// Current config.obj storage 015cd120/015cd124, populated by
// LoadSettingSection callsites 005cab67/005cab8a.
int g_nPostFeeBase = 100;
int g_nPostFeePerSlot = 1000;
// Current config.obj storage 02379541/02379548, populated by
// Localization/RecipeGiveBack and RecipeGiveBackPer.
bool g_bRecipeGiveBack = false;
double g_dRecipeGiveBackPer = 0.0;
double g_dF2PQuestRewardPenaltyPer = 0.0;
CConfig config;
std::vector<std::u16string> g_AdminIP{};
::in_addr commServerAddr{};
std::map<int, std::u16string> g_mapWorldName{};
std::map<int, int> g_mapF2PRewardPenaltyQuests{};
std::uint16_t commServerPort = 2010;
std::int32_t commAutoReconnect = 1;
std::int32_t commUseSvr = 1;
bool g_bShowSharedWritableBBS = true;
int g_nBBSWriteLevel = 10;
int g_nFontCheck = 0;
char16_t g_sFontCheckName[256]{};
RelayStruct g_RelaySetting{};
::in_addr logServerAddr{};
short logServerPort = 0;
::in_addr cacheServerAddr{};
short cacheServerPort = 0;
int g_CacheD_nCheckTimeout = 60;
bool g_CacheD_bCheckCrash = true;
AccountServerSetting g_AccountServerSetting{};
ChannelChatting g_stChannelChattingInfo{};
PetitionStruct g_PetitionSetting{};
int g_LogFileDuration = 30;
// Current address 023794f8. Current CConfig::Load writes the World.WorldId
// value at 005cbb20. TotalChecker2015 config.obj keeps this exact spelling.
int g_nWorldId = 0;
// Current address 02fd4302, populated from World/UseCustomEquipment at
// reader callsite 005cbbd7 / store 005cbbef.
bool g_bUseCustomEquipment = false;
// Current address 02fd4303, populated from World/UseLecture at 005cbc21.
bool g_bUseLecture = false;
int g_DormantPostponeTime = 24;
int g_NewbiePostponeTime = 168;
// Current config.obj storage 015cd170..015cd179. LoadSettingSection reads the
// exact four Setting keys at 005caecc..005caf67.
int g_DormantUserGift = 37313;
int g_nNewbieGiftId = 37313;
bool g_bSendGiftToNewbie = true;
bool g_bSendGiftToDormant = true;
int g_nMentorCancelPanelty = 0;
int g_nMentorGraduatePanelty = 0;
int g_nChinaFatigueSystemType = 0;
int g_ChinaHalfFatigueTime = 3;
int g_ChinaFullFatigueTime = 5;
int g_nPlayTimeLimit = 0;
int g_nPlayTimeChanged = 0;
bool g_bNewVote = false;
int g_nEscapeMode = 1;
int g_nAutoLogoutTimeLimit = 600;
bool g_b22to6DisableGame = false;
bool g_bBuilderLogAdd = true;
bool g_bPingCountOff = false;
bool g_bPingOnRaid = false;
int g_nAgeCheckType = 0;
int g_DormantUserDay = 60;
bool g_bNewDormantBitset = true;
bool g_bNotDormantServer = false;
bool g_bUseAuthTimeManageSystem = false;
bool g_bUseAuthUserData = false;
int g_nBirthdayItemClassID = 0;
bool g_DormantUserSystem = false;
int g_nPremiumLimit = 5500;
int g_nWaiterMaxCount = 5000;
int g_nQueueTicketPassVipLevel = 1;
bool g_bQueueTicketNormalWait = false;
bool g_bQueueTicketVipWait = false;
bool g_bQueueTicketBuilderOff = false;
int g_nUseReconnectAuth = 0;
bool g_bAuthConnected = false;
bool g_ShutDownSystem = false;
short authServerPort = 0;
::in_addr authServerAddr{};
int g_nAuthServerID = 0;
bool g_bQueueTicketEnabled = false;
bool g_bUseNPLogin = false;
bool g_bDebugNPLogin = false;
bool g_bChargeFree = false;
bool g_bChargeDistinction = false;
bool g_bCheckSecondaryAuth = false;
char g_szNPAddress[128] = "dev-np-01";
short g_nNPPort = 0;
bool g_bFreeOpenServer = false;
bool g_bServerStopperEnabled = false;
std::uint32_t g_nServerStopperKey = 0;
bool g_bAcceptLowerProtocol = false;
bool g_bNewCharacterCreate = false;
int g_nCharacterCreateLevel = 1;
bool g_bProhibitAllCharCreation = false;
bool g_bProhibitAllCharDeletion = false;
bool g_bProhibitFirstCharCreation = false;
int g_nProhibitCharCreationCount = 7;
std::int32_t g_nCheckCastleId = 1;
int g_nPacketRepeatLimit = 0;
int g_nPacketRepeatLimitTime = 1000;
bool g_bFreeTeleportNeedItem = false;
int g_nMaxSpawnNumPerOneTick = 100;
int g_nMaxDespawnNumPerOneTick = 500;
int g_nVitalBonusForFreeUser = 200;
int g_nVitalBonusForPremiumUser = 200;
int g_nMaxRestoreVitalItemCountForFreeUser = 5;
int g_nMaxRestoreVitalItemCountForPremiumUser = 10;
int g_nActivateCharacterAmountInF2P = 7;
bool g_bPremiumUserIsPCCafe = false;
double g_dVitalityConsumeByMobPer = 1.0;
double g_dVitalityConsumeByBossPer = 1.0;
bool g_bVitalityByMobBeforeRenewal = false;
bool g_bVitalityByBossBeforeRenewal = false;
int g_nMaxUserSocketPerIP = 0;
int g_nMaxUserSocketPerMAC = 0;
int g_nIPBlock = 0;
bool g_bUseResourceLoader = false;
bool g_bUserPathFind = false;
bool g_bLoadEventDataFromDB = false;
bool g_bEnableTeleportToNPCInternal = false;
int g_nBotCheckItemClassID = 0;
int g_nBotCheckItemCount = 0;
int g_nFakeItemClassID = 0x39;
int g_nClanAdventSkill = 19009;
bool g_bUseAutoSoulShotUI = false;
bool g_bUseOneDayReward = false;
int g_nPPTPOption = 1;
bool g_bUserToServerPacketStatMonitor = false;
bool g_bServerToUserPacketStatMonitor = false;
bool g_bTimeAnalysisMonitor = false;
bool g_bNpcDieStatMonitor = false;
bool g_bNPCBroadcastMonitor = false;
bool g_bCachePingFlag = false;
bool g_bNpcPingFlag = false;
int g_nCachePingTime = 60000;
int g_nNpcPingTime = 60000;
bool g_bWritePacketLog = false;
bool g_bEnablePacketShuffle = false;
bool g_bProcessClientINI = false;
bool g_bUseAttendanceSystem = false;
bool g_bUsePCCafeAttendance = false;
bool g_bUseVipAttendance = false;
int g_nAttendanceMinimumLev = 0;
int g_nAttendanceDelayTime = 0;
int g_nUserStatusLogTime = 180;
bool bWriteConnLog = false;
bool g_bIsNonPKServer = false;
bool g_bCannotMonRace = false;
bool g_bAllUserPcCafe = false;
bool g_bUserGarbageCollector = false;
int g_nCreatePartyMatchLevelLimit = 0;
int nHeartBeat = 0;
GoodsInventoryStruct g_GoodsInventorySetting{};
PremiumStruct g_PremiumSetting{};
GIPStruct g_GIPSetting{};
bool g_bCashshopPriceIsTotal = false;
bool g_bIsMentorBuffed = false;
CaptchaSettingyStruct g_CaptchaSetting{};
WorldChat g_WorldChat{};
int g_nCountryCode = 0;
// Current config.obj storage 0x023785a0. LoadSettingSection publishes the
// bounded Setting/binary_update_version UTF-16 value directly into this
// 0x800-byte process-global object at 0x005c936e.
BinaryInfo g_BinaryInfo{};
L2ServerType::Value g_L2ServerType = L2ServerType::LIVE;
int g_dBotUnseleteTime = 500;

bool F2PConfig::IsAllCharacterSlotUsableAccount(CUserSocket* socket) {
    // Current L2Server.exe 0x005c8178 (185 bytes).
    L2_GUARD_FUNCTION();
    return socket != nullptr && !socket->CheckPremiumType(PremiumUserType::PUT_FREE_USER);
}

int F2PConfig::GetUsableCharacterSlotCount(CUserSocket* socket) {
    // Current L2Server.exe 0x005c8234 (188 bytes).
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return 0;
    }
    return IsAllCharacterSlotUsableAccount(socket) ? 7 : g_nActivateCharacterAmountInF2P;
}

int F2PConfig::GetVitalityBonus(CUserSocket* socket) {
    // Current L2Server.exe 0x005c82f0 (191 bytes).
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return 0;
    }
    return socket->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_nVitalBonusForFreeUser : g_nVitalBonusForPremiumUser;
}

int F2PConfig::GetVitalityItemMaxCount(CUserSocket* socket) {
    // Current L2Server.exe 0x005c83b0 (191 bytes).
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return 0;
    }
    return socket->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_nMaxRestoreVitalItemCountForFreeUser
                                                                    : g_nMaxRestoreVitalItemCountForPremiumUser;
}

int F2PConfig::GetVitalityItemUsableCount(CUserSocket* socket, const int usedCount) {
    // Current L2Server.exe 0x005c8470 (184 bytes).
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return 0;
    }
    return std::max(GetVitalityItemMaxCount(socket) - usedCount, 0);
}

bool F2PConfig::IsContainPCCafeUserBonus(CUserSocket* socket) {
    // Current L2Server.exe 0x005c8cac (189 bytes).
    L2_GUARD_FUNCTION();
    if (socket == nullptr || socket->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
        return false;
    }
    return g_bPremiumUserIsPCCafe;
}

bool F2PConfig::CanTakePremiumItem(User* const user) {
    // Current L2Server 0x005c8d6c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
        return false;
    }
    return g_nPremiumItem[0] != 0;
}

int F2PConfig::GetPKLimitCount(const User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_nPKCountForFreeUser : g_nPKCountForPremiumUser;
}

int F2PConfig::GetPKRemainCount(const User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0;
    }
    return std::max(GetPKLimitCount(user) - user->m_data->m_nPKCount, 0);
}

bool F2PConfig::CheckPKCount(const User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    const int limit = GetPKLimitCount(user);
    return limit == -1 || user->m_data->m_nPKCount < limit;
}

int F2PConfig::GetPrivateStoreSellLimit(User* user) {
    // Current L2Server 0x005c8834.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return -1;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_nPrivateStoreSellLimitForFreeUser
                                                                  : g_nPrivateStoreSellLimitForPremiumUser;
}

bool F2PConfig::CanOpenPrivateStoreBuy(User* user) {
    // Current L2Server 0x005c88f4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_bCanPrivateStoreBuyForFreeUser
                                                                  : g_bCanPrivateStoreBuyForPremiumUser;
}

bool F2PConfig::CanRegisterCommission(User* user) {
    // Current L2Server 0x005c89b4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_bCanRegisterCommissionForFreeUser
                                                                  : g_bCanRegisterCommissionForPremiumUser;
}

bool F2PConfig::CanCreatePledge(User* user) {
    // Current L2Server 0x005c8a74.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_bCanCreatePledgeForFreeUser
                                                                  : g_bCanCreatePledgeForPremiumUser;
}

bool F2PConfig::CanItemAttatchedPostUser(User* user) {
    // Current L2Server 0x005c8b34. Preserve the source spelling.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_bCanItemAttachedPostForFreeUser
                                                                  : g_bCanItemAttachedPostForPremiumUser;
}

bool F2PConfig::IsDailyPostLimitUser(User* user) {
    // Current L2Server 0x005c8bf4.
    L2_GUARD_FUNCTION();
    return user != nullptr && user->CheckPremiumType(PremiumUserType::PUT_FREE_USER);
}

bool F2PConfig::CanBeMentor(User* user) {
    // Current L2Server 0x005c8e30 (191 bytes).
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    return user->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? g_bCanBeMentorForFreeUser : g_bCanBeMentorForPremiumUser;
}

double F2PConfig::GetExpBoostRate(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
        return 0.0;
    }
    const double boost = g_PremiumUserBoost.expBoost;
    return user->m_data->m_nLevel < g_PremiumUserBoost.expSpBoostMinLevel ? 0.0 : boost;
}

double F2PConfig::GetSpBoostRate(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
        return 0.0;
    }
    const double boost = g_PremiumUserBoost.spBoost;
    return user->m_data->m_nLevel < g_PremiumUserBoost.expSpBoostMinLevel ? 0.0 : boost;
}

double F2PConfig::GetSpoilBoostRate(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0.0;
    }
    const bool freeUser = user->CheckPremiumType(PremiumUserType::PUT_FREE_USER);
    const double boost = g_PremiumUserBoost.spoilBoost;
    return freeUser ? 0.0 : boost;
}

double F2PConfig::GetNormalItemDropBoostRate(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0.0;
    }
    const bool freeUser = user->CheckPremiumType(PremiumUserType::PUT_FREE_USER);
    const double boost = g_PremiumUserBoost.normalItemDropBoost;
    return freeUser ? 0.0 : boost;
}

double F2PConfig::GetVitalItemDropBoostRate(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0.0;
    }
    const bool freeUser = user->CheckPremiumType(PremiumUserType::PUT_FREE_USER);
    const double boost = g_PremiumUserBoost.vitalItemDropBoost;
    return freeUser ? 0.0 : boost;
}

int F2PConfig::GetPremiumUserBuffSkillId(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
        return 0;
    }
    return g_PremiumUserBuff;
}

namespace l2server {
namespace {

struct LegacyIpv4Address final {
    // Numeric value stored by inet_addr on the little-endian current target.
    std::uint32_t windowsNetworkOrder = 0xffffffffU;
};

template <typename T>
struct F2PTypePair final {
    // Current producers/consumers fix the value-0 and fallback branches. The
    // matching-source donor is accepted for their free/premium spellings.
    T typeZero;
    T fallback;
};

struct CConfigSettingBirthdayDate final {
    bool exactlyThreeTokens = false;
    std::int32_t year = 0;
    std::int32_t month = 0;
    std::int32_t day = 0;

    // The current helper calls the local-time 32-bit Windows CRT boundary.
    // A value is exposed only where the current range rejection alone proves
    // it. Valid local dates remain host-dependent on non-Windows.
    bool windowsLocalTime32Known = false;
    std::int32_t windowsLocalTime32 = -1;
};

std::string LegacyIpv4Text(LegacyIpv4Address address) {
    // LegacyIpv4Address stores the little-endian numeric value returned by
    // current WinSock inet_addr. Formatting its four target-memory bytes is
    // the portable equivalent of passing that in_addr to sockaddr_in.
    const std::uint32_t value = address.windowsNetworkOrder;
    return std::to_string(value & 0xffU) + "." + std::to_string((value >> 8U) & 0xffU) + "." +
        std::to_string((value >> 16U) & 0xffU) + "." + std::to_string((value >> 24U) & 0xffU);
}


bool ParseLegacyNumber(std::u16string_view text, std::uint32_t& result) {
    if (text.empty()) {
        return false;
    }
    std::uint32_t base = 10;
    std::size_t offset = 0;
    if (text.size() >= 2 && text[0] == u'0' && (text[1] == u'x' || text[1] == u'X')) {
        base = 16;
        offset = 2;
        if (offset == text.size()) {
            return false;
        }
    } else if (text[0] == u'0') {
        base = 8;
        offset = 1;
        if (offset == text.size()) {
            result = 0;
            return true;
        }
    }

    std::uint64_t value = 0;
    for (; offset < text.size(); ++offset) {
        const char16_t character = text[offset];
        std::uint32_t digit = 0;
        if (character >= u'0' && character <= u'9') {
            digit = character - u'0';
        } else if (character >= u'a' && character <= u'f') {
            digit = 10U + character - u'a';
        } else if (character >= u'A' && character <= u'F') {
            digit = 10U + character - u'A';
        } else {
            return false;
        }
        if (digit >= base) {
            return false;
        }
        value = value * base + digit;
        if (value > 0xffffffffULL) {
            return false;
        }
    }
    result = static_cast<std::uint32_t>(value);
    return true;
}

std::uint32_t ByteSwap32(std::uint32_t value) {
    return ((value & 0x000000ffU) << 24U) | ((value & 0x0000ff00U) << 8U) | ((value & 0x00ff0000U) >> 8U) |
        ((value & 0xff000000U) >> 24U);
}

LegacyIpv4Address LegacyInetAddress(std::u16string_view text) {
    std::array<std::uint32_t, 4> parts{};
    std::size_t count = 0;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        if (count == parts.size()) {
            return {};
        }
        const std::size_t dot = text.find(u'.', begin);
        const std::size_t end = dot == std::u16string_view::npos ? text.size() : dot;
        if (end == begin || !ParseLegacyNumber(text.substr(begin, end - begin), parts[count])) {
            return {};
        }
        ++count;
        if (dot == std::u16string_view::npos) {
            break;
        }
        begin = dot + 1;
    }

    std::uint32_t hostValue = 0;
    if (count == 1) {
        hostValue = parts[0];
    } else if (count == 2 && parts[0] <= 0xffU && parts[1] <= 0xffffffU) {
        hostValue = (parts[0] << 24U) | parts[1];
    } else if (count == 3 && parts[0] <= 0xffU && parts[1] <= 0xffU && parts[2] <= 0xffffU) {
        hostValue = (parts[0] << 24U) | (parts[1] << 16U) | parts[2];
    } else if (count == 4 && parts[0] <= 0xffU && parts[1] <= 0xffU && parts[2] <= 0xffU && parts[3] <= 0xffU) {
        hostValue = (parts[0] << 24U) | (parts[1] << 16U) | (parts[2] << 8U) | parts[3];
    } else {
        return {};
    }
    return {ByteSwap32(hostValue)};
}


struct FirstCommaToken final {
    bool present = false;
    std::u16string_view first;
    std::u16string_view remainder;
};

FirstCommaToken TokenizeFirstComma(std::u16string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && value[begin] == u',') {
        ++begin;
    }
    if (begin == value.size()) {
        return {};
    }
    const std::size_t comma = value.find(u',', begin);
    if (comma == std::u16string_view::npos) {
        return {true, value.substr(begin), std::u16string_view{}};
    }
    return {
        true,
        value.substr(begin, comma - begin),
        value.substr(comma + 1),
    };
}

bool IsDecimalSpace(char16_t value) {
    return value == u' ' || value == u'\t' || value == u'\r' || value == u'\n' || value == u'\v' || value == u'\f';
}

std::int32_t Wcstol10(std::u16string_view value) {
    std::size_t offset = 0;
    while (offset < value.size() && IsDecimalSpace(value[offset])) {
        ++offset;
    }
    bool negative = false;
    if (offset < value.size() && (value[offset] == u'+' || value[offset] == u'-')) {
        negative = value[offset] == u'-';
        ++offset;
    }
    const std::size_t digitStart = offset;
    const std::uint64_t limit = negative ? static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1U
                                         : static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    std::uint64_t magnitude = 0;
    bool saturated = false;
    while (offset < value.size() && value[offset] >= u'0' && value[offset] <= u'9') {
        const std::uint64_t digit = value[offset] - u'0';
        if (!saturated) {
            if (magnitude > (limit - digit) / 10U) {
                magnitude = limit;
                saturated = true;
            } else {
                magnitude = magnitude * 10U + digit;
            }
        }
        ++offset;
    }
    if (offset == digitStart) {
        return 0;
    }
    if (negative && magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1U) {
        return std::numeric_limits<std::int32_t>::min();
    }
    const std::int32_t converted = static_cast<std::int32_t>(magnitude);
    return negative ? -converted : converted;
}

void ApplyDecimalPair(const std::u16string& value, F2PTypePair<std::int32_t>& destination) {
    const FirstCommaToken token = TokenizeFirstComma(value);
    if (!token.present) {
        return;
    }
    destination.typeZero = Wcstol10(token.first);
    destination.fallback = Wcstol10(token.remainder);
}

void ApplyBooleanPair(const std::u16string& value, F2PTypePair<bool>& destination) {
    const FirstCommaToken token = TokenizeFirstComma(value);
    if (!token.present) {
        return;
    }
    destination.typeZero = token.first == u"true";
    destination.fallback = token.remainder == u"true";
}


bool EqualsAsciiCaseInsensitive(std::u16string_view left, std::u16string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        char16_t lhs = left[index];
        char16_t rhs = right[index];
        if (lhs >= u'A' && lhs <= u'Z') {
            lhs += u'a' - u'A';
        }
        if (rhs >= u'A' && rhs <= u'Z') {
            rhs += u'a' - u'A';
        }
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}


std::vector<std::u16string_view> SplitNonempty(std::u16string_view value, char16_t delimiter) {
    std::vector<std::u16string_view> tokens;
    std::size_t offset = 0;
    while (offset < value.size()) {
        while (offset < value.size() && value[offset] == delimiter) {
            ++offset;
        }
        if (offset == value.size()) {
            break;
        }
        const std::size_t end = value.find(delimiter, offset);
        if (end == std::u16string_view::npos) {
            tokens.push_back(value.substr(offset));
            break;
        }
        tokens.push_back(value.substr(offset, end - offset));
        offset = end + 1;
    }
    return tokens;
}

std::int32_t LookupServerType(std::u16string_view value) {
    std::u16string mutableValue(value);
    return static_cast<std::int32_t>(Singleton<ServerTypeHelper>::inst().GetType(mutableValue.data()));
}

CConfigSettingBirthdayDate ParseBirthdayDate(std::u16string_view value) {
    CConfigSettingBirthdayDate result;
    const std::vector<std::u16string_view> tokens = SplitNonempty(value, u'/');
    result.exactlyThreeTokens = tokens.size() == 3U;
    if (result.exactlyThreeTokens) {
        result.year = Wcstol10(tokens[0]);
        result.month = Wcstol10(tokens[1]);
        result.day = Wcstol10(tokens[2]);
    }

    // FUN_00ceec90 first rejects tm_year values outside its unsigned
    // [69, 1101] gate. That fact is independent of host timezone and closes
    // the common 9999/01/01 default exactly. Values that pass the gate remain
    // explicit because the current next step is local Windows CRT time32 and
    // even consumes an otherwise uninitialized tm_isdst slot.
    const std::int32_t tmYear = runtime::SubtractInt32Wrapping(result.year, 1900U);
    const std::uint32_t range = static_cast<std::uint32_t>(tmYear) - 69U;
    if (range >= 0x409U) {
        result.windowsLocalTime32Known = true;
        result.windowsLocalTime32 = -1;
    }
    return result;
}


template <typename CharT, std::size_t Extent>
void PublishTargetString(CharT (&destination)[Extent], const std::u16string& source) {
    static_assert(std::is_same_v<CharT, char16_t>);
    const std::size_t length = std::min(source.size(), Extent - 1U);
    std::copy_n(source.data(), length, destination);
    destination[length] = u'\0';
}

void PublishBirthdayEventStart(const CConfigSettingBirthdayDate& value) {
    // Current 005c8f7c initializes only the six calendar/time lanes before
    // _mktime32. In particular tm_isdst is not initialized.
    std::tm localTime;
    localTime.tm_sec = 0;
    localTime.tm_min = 0;
    localTime.tm_hour = 0;
    localTime.tm_mday = value.day;
    localTime.tm_mon = value.month - 1;
    localTime.tm_year = value.year - 1900;
    g_bBirthdayEventStart = runtime::crt::Mktime32(&localTime);
}


void PublishLegacyAddressExact(::in_addr& destination, LegacyIpv4Address source) {
    std::memcpy(&destination, &source.windowsNetworkOrder, sizeof(destination));
}

void PublishAnsiExact(char* destination, int destinationCapacity, const std::u16string& source) {
    UnicodeToAnsi(const_cast<char16_t*>(source.c_str()), static_cast<int>(source.size()), destination, destinationCapacity);
}


} // namespace
} // namespace l2server

namespace {

constexpr std::u16string_view kL2ServerIni = u".\\l2server.ini";

std::u16string ReadConfigString(
    std::u16string_view section, std::u16string_view key, std::u16string_view defaultValue, std::size_t capacity
) {
    std::vector<char16_t> buffer(capacity, u'\0');
    l2server::g_SecureINIFileDB.GetString(
        section, key, defaultValue, buffer.data(), static_cast<std::uint32_t>(capacity), kL2ServerIni
    );
    return std::u16string(buffer.data());
}

std::u16string ReadProfileConfigString(
    std::u16string_view section, std::u16string_view key, std::u16string_view defaultValue, std::size_t capacity
) {
    std::vector<char16_t> buffer(capacity, u'\0');
    l2server::g_SecureINIFileDB.GetProfileString(
        section, key, defaultValue, buffer.data(), static_cast<std::uint32_t>(capacity), kL2ServerIni
    );
    return std::u16string(buffer.data());
}

[[noreturn]] void CrashOnMissingL2ServerIni() {
    // Current 005cb350..005cb3a3 publishes the crash location and reason,
    // then calls CrashImpl@007264c8 (xor eax,eax, then mov byte ptr [rax],1).
    ForceCrash(CrashInfo::LOAD_FAILURE_FOR_L2SERVER_INI);
    std::abort();
}

std::u16string_view ReadNormalizedConfigString(
    std::u16string_view section,
    std::u16string_view key,
    std::u16string_view defaultValue,
    std::array<char16_t, 1024>& buffer,
    std::uint32_t capacity = 0x3ffU
) {
    buffer.fill(u'\0');
    l2server::g_SecureINIFileDB.GetString(section, key, defaultValue, buffer.data(), capacity, kL2ServerIni);
    return std::u16string_view(buffer.data());
}

double ReadUnsignedConfigPercent(std::u16string_view section, std::u16string_view key, std::int32_t defaultValue) {
    const std::int32_t value = l2server::g_SecureINIFileDB.GetInt(section, key, defaultValue, kL2ServerIni);
    return static_cast<double>(static_cast<std::uint32_t>(value)) / 100.0;
}

bool IsNormalizedTrue(std::u16string_view value) { return value == u"true"; }

void ReadDecimalPair(std::u16string_view key, int& typeZero, int& fallback, std::array<char16_t, 1024>& buffer) {
    const std::u16string_view value = ReadNormalizedConfigString(u"FreeToPlayUserSetting", key, u"", buffer);
    l2server::F2PTypePair<std::int32_t> pair{typeZero, fallback};
    l2server::ApplyDecimalPair(std::u16string(value), pair);
    typeZero = pair.typeZero;
    fallback = pair.fallback;
}

void ReadBooleanPair(std::u16string_view key, bool& typeZero, bool& fallback, std::array<char16_t, 1024>& buffer) {
    const std::u16string_view value = ReadNormalizedConfigString(u"FreeToPlayUserSetting", key, u"", buffer);
    l2server::F2PTypePair<bool> pair{typeZero, fallback};
    l2server::ApplyBooleanPair(std::u16string(value), pair);
    typeZero = pair.typeZero;
    fallback = pair.fallback;
}

void PublishLegacyAddress(::in_addr& destination, std::u16string_view value) {
    const l2server::LegacyIpv4Address address = l2server::LegacyInetAddress(value);
    std::memcpy(&destination, &address.windowsNetworkOrder, sizeof(destination));
}

std::uint32_t LegacyAddressValue(std::u16string_view value) {
    return l2server::LegacyInetAddress(value).windowsNetworkOrder;
}

} // namespace

CConfig::CConfig() = default;

CConfig::~CConfig() { L2_GUARD_FUNCTION(); }

void CConfig::Load() {
    L2_GUARD_FUNCTION();

    // Current 005cb2f9 publishes this fixed value before touching the INI.
    nHeartBeat = 666;
    if (!l2server::g_SecureINIFileDB.Load(kL2ServerIni)) {
        L2_LOG_ERROR(Log, "[configuration] Cannot continue without l2server.ini; terminating the process.");
        CrashOnMissingL2ServerIni();
    }

    LoadSettingSection();
    LoadFreeToPlaySection();

    l2server::LegacyIpv4Address cacheAddress;
    l2server::LegacyIpv4Address authAddress;
    l2server::LegacyIpv4Address logAddress;
    int petitionDisableStart = 0;
    int petitionDisableEnd = 0;
    int worldId = 1;

    // [CacheD] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"CacheD", u"address", u"127.0.0.1", 1023));
        cacheAddress = value;
    }

    // [CacheD] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"CacheD", u"port", 12006, kL2ServerIni);
        cacheServerPort = static_cast<short>(value);
        l2server::PublishLegacyAddressExact(cacheServerAddr, cacheAddress);
    }

    // [CacheD] CheckTimeout
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"CacheD", u"CheckTimeout", 60, kL2ServerIni);
        g_CacheD_nCheckTimeout = value;
    }

    // [CacheD] CrashOnFailed
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"CacheD", u"CrashOnFailed", u"true", 1023), u"true");
        g_CacheD_bCheckCrash = value;
    }

    // [AuthD] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"AuthD", u"address", u"127.0.0.1", 1023));
        authAddress = value;
    }

    // [AuthD] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"port", 2044, kL2ServerIni);
        authServerPort = static_cast<short>(value);
        l2server::PublishLegacyAddressExact(authServerAddr, authAddress);
    }

    // [AuthD] UseAuthTimeManageSystem
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"AuthD", u"UseAuthTimeManageSystem", u"false", 1023), u"true"
        );
        g_bUseAuthTimeManageSystem = value;
    }

    // [AuthD] ChinaFatigueSystem
    {
        const auto input = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"ChinaFatigueSystem", 0, kL2ServerIni);
        const auto value = input < 0 || input > 3 ? 0 : input;
        g_nChinaFatigueSystemType = value;
    }

    // [AuthD] ChinaHalfFatigueTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"ChinaHalfFatigueTime", 3, kL2ServerIni);
        g_ChinaHalfFatigueTime = value;
    }

    // [AuthD] ChinaFullFatigueTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"ChinaFullFatigueTime", 5, kL2ServerIni);
        g_ChinaFullFatigueTime = value;
    }

    // [AuthD] ShutDownSystem
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"AuthD", u"ShutDownSystem", u"false", 1023), u"true");
        g_ShutDownSystem = value;
    }

    // [AuthD] DualBillingSystem
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"AuthD", u"DualBillingSystem", u"false", 1023), u"true");
        g_bDualBillingSystem = value;
    }

    // [AuthD] DualBillingSystemLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"DualBillingSystemLevel", 85, kL2ServerIni);
        g_nDualBillingSystemLevel = value;
    }

    // [AuthD] DormantUserSystem
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"AuthD", u"DormantUserSystem", u"false", 1023), u"true");
        g_DormantUserSystem = value;
    }

    // [AuthD] UseNPLogin
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"AuthD", u"UseNPLogin", u"false", 1023), u"true");
        g_bUseNPLogin = value;
    }

    // [AuthD] NPAddress
    {
        const auto value = ReadConfigString(u"AuthD", u"NPAddress", u"dev-np-01", 1023);
        l2server::PublishAnsiExact(g_szNPAddress, 0x7f, value);
    }

    // [AuthD] NPPort
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"NPPort", 6601, kL2ServerIni);
        g_nNPPort = static_cast<short>(value);
    }

    // [AuthD] NPProgramID
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"AuthD", u"NPProgramID", 1102, kL2ServerIni);
        NPSystem::g_nProgramId = value;
    }

    // [AuthD] NPAppGroupCode
    {
        const auto value = ReadConfigString(u"AuthD", u"NPAppGroupCode", u"lin2", 1023);
        l2server::PublishAnsiExact(NPSystem::g_pAppGroupCode, 0x3f, value);
    }

    // [AuthD] NPServerAppKey
    {
        const auto value = ReadConfigString(u"AuthD", u"NPServerAppKey", u"9B1C477D-C61C-C4F3-3C24-F3E302F7D1B7", 1023);
        l2server::PublishAnsiExact(NPSystem::g_pServerAppKey, 0x3f, value);
    }

    // [AuthD] NPClientAppKey
    {
        const auto value = ReadConfigString(u"AuthD", u"NPClientAppKey", u"9DD5DD41-71AF-E48B-68AA-30570B7B8B97", 1023);
        l2server::PublishAnsiExact(NPSystem::g_pClientAppKey, 0x3f, value);
    }

    // [LogD] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"LogD", u"address", u"127.0.0.1", 1023));
        logAddress = value;
        l2server::PublishLegacyAddressExact(logServerAddr, logAddress);
    }

    // [LogD] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"LogD", u"port", 3999, kL2ServerIni);
        logServerPort = static_cast<short>(value);
    }

    // [LogD] FileDuration
    {
        const auto input = l2server::g_SecureINIFileDB.GetInt(u"LogD", u"FileDuration", 30, kL2ServerIni);
        // Preserve the original divide-error path for zero before any publication.
        if (input == 0) {
            runtime::RaiseWin64IntegerDivideError();
        }
        const auto value = input > 0 && 60 % input == 0 ? input : 30;
        g_LogFileDuration = value;
    }

    // [PetitionD] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"PetitionD", u"address", u"127.0.0.1", 1023));
        l2server::PublishLegacyAddressExact(g_PetitionSetting.serverAddr, value);
    }

    // [PetitionD] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"PetitionD", u"port", 3999, kL2ServerIni);
        g_PetitionSetting.serverPort = static_cast<short>(value);
    }

    // [PetitionD] heartbeat
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"PetitionD", u"heartbeat", 300, kL2ServerIni), 1000U
        );
        g_PetitionSetting.heartBeat = static_cast<unsigned>(value);
    }

    // [PetitionD] worldName
    {
        const auto value = ReadConfigString(u"PetitionD", u"worldName", u"L2server ", 255);
        l2server::PublishTargetString(g_PetitionSetting.wsWorldServerName, value);
    }

    // [PetitionD] DisableStart
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"PetitionD", u"DisableStart", 0, kL2ServerIni) % 24;
        petitionDisableStart = value;
    }

    // [PetitionD] DisableEnd
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"PetitionD", u"DisableEnd", 0, kL2ServerIni) % 24;
        petitionDisableEnd = value;
    }

    // [PetitionD] UseWebPetition
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"PetitionD", u"UseWebPetition", u"false", 1023), u"true");
        g_PetitionSetting.UseWebPetition = value;
        petitionCli.SetDisableTime(petitionDisableStart, petitionDisableEnd);
    }

    // [World] WorldId
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"World", u"WorldId", 1, kL2ServerIni);
        worldId = value;
        g_nServerId = value;
        g_nWorldId = value;
        Log.Init(g_LogFileDuration, g_nWorldId);
        runtime::log::ConfigureLog(
            Log,
            runtime::log::LogOptions{l2server::LegacyIpv4Text(logAddress),
                static_cast<int>(static_cast<unsigned short>(logServerPort)), g_nWorldId, "log", g_LogFileDuration}
        );
    }

    // [World] UserLimit
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"World", u"UserLimit", 3000, kL2ServerIni);
        g_nServerUserCountLimit = value;
    }

    // [World] LetBuilder
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"World", u"LetBuilder", 1, kL2ServerIni);
        g_nLetBuilder = value;
    }

    // [World] PremiumLimit
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"World", u"PremiumLimit", 5500, kL2ServerIni);
        g_nPremiumLimit = value;
    }

    // [World] UseCustomEquipment
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"World", u"UseCustomEquipment", u"false", 1023), u"true");
        g_bUseCustomEquipment = value;
    }

    // [World] UseLecture
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"World", u"UseLecture", u"false", 1023), u"true");
        g_bUseLecture = value;
    }

    // [World] AuthServerId
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"World", u"AuthServerId", worldId, kL2ServerIni);
        g_nAuthServerID = value;
    }

    // [BBS] WriteLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"BBS", u"WriteLevel", 10, kL2ServerIni);
        g_nBBSWriteLevel = value;
    }

    // [BBS] ShowSharedWritableBBS
    {
        const auto value = !l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"BBS", u"ShowSharedWritableBBS", u"true", 1023), u"false"
        );
        g_bShowSharedWritableBBS = value;
    }

    // [L2COMM] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"L2COMM", u"address", u"127.0.0.1", 1023));
        commServerAddr.s_addr = value.windowsNetworkOrder;
    }

    // [L2COMM] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"L2COMM", u"port", 2010, kL2ServerIni);
        commServerPort = static_cast<std::uint16_t>(value);
    }

    // [L2COMM] autoconnect
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"L2COMM", u"autoconnect", 1, kL2ServerIni);
        commAutoReconnect = value;
    }

    // [L2COMM] usesvr
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"L2COMM", u"usesvr", 1, kL2ServerIni);
        commUseSvr = value;
    }

    LoadEventSection();

    // [FONTCHECK] FONTCHECK
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"FONTCHECK", u"FONTCHECK", 0, kL2ServerIni);
        g_nFontCheck = value;
    }

    // [FONTCHECK] filename
    {
        const auto value = ReadConfigString(u"FONTCHECK", u"filename", u"smallfont.gly", 255);
        l2server::PublishTargetString(g_sFontCheckName, value);
    }

    // [BOT] block
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"BOT", u"block", 0, kL2ServerIni) != 0;
        g_nBOTInspectorBlock = value ? 1 : 0;
    }

    // [BOT] TestCount
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"BOT", u"TestCount", 3, kL2ServerIni);
        g_nBotInspectorTestCount = value;
        if (g_nBOTInspectorBlock < 1) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"Bot Inspector block is running report mode.")
            // L2_LOG_INFO(Log,
            //     L"Bot Inspector block is running report mode.")
            L2_LOG_INFO(Log, "[configuration] Bot Inspector is configured for reporting only.");
        } else {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"Bot Inspector block is enabled. test count = %d",
            //     g_nBotInspectorTestCount)
            // L2_LOG_INFO(Log,
            //     L"Bot Inspector block is enabled. test count = %d",
            //     g_nBotInspectorTestCount)
            L2_LOG_INFO(Log, "[configuration] Bot Inspector blocking is enabled (test count %d).", g_nBotInspectorTestCount);
        }
    }

    // [HtmlLinkCheck] HtmlLinkCheck
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"HtmlLinkCheck", u"HtmlLinkCheck", u"false", 1023), u"true"
        );
        g_bHtmlLinkCheck = value;
    }

    // [HtmlLinkCheck] LogOnInvalidLink
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"HtmlLinkCheck", u"LogOnInvalidLink", u"false", 1023), u"true"
        );
        g_bLogOnInvalidLink = value;
    }

    // [HtmlLinkCheck] KickOnInvalidLink
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"HtmlLinkCheck", u"KickOnInvalidLink", u"false", 1023), u"true"
        );
        g_bKickOnInvalidLink = value;
    }

    // [Ping] CachePingFlag
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Ping", u"CachePingFlag", u"false", 1023), u"true");
        g_bCachePingFlag = value;
    }

    // [Ping] CachePingTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Ping", u"CachePingTime", 60000, kL2ServerIni);
        g_nCachePingTime = value;
    }

    // [Ping] NpcPingFlag
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Ping", u"NpcPingFlag", u"false", 1023), u"true");
        g_bNpcPingFlag = value;
    }

    // [Ping] NpcPingTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Ping", u"NpcPingTime", 60000, kL2ServerIni);
        g_nNpcPingTime = value;
    }

    // [GIP] enabled
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"GIP", u"enabled", u"false", 1023), u"true");
        g_GIPSetting.enabled = value;
    }

    // [GIP] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"GIP", u"address", u"127.0.0.1", 1023));
        l2server::PublishLegacyAddressExact(g_GIPSetting.serverAddr, value);
    }

    // [GIP] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"GIP", u"port", 3999, kL2ServerIni);
        g_GIPSetting.serverPort = static_cast<short>(value);
    }

    // [GIP] heartbeat
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"GIP", u"heartbeat", 10, kL2ServerIni), 1000U
        );
        g_GIPSetting.heartBeat = static_cast<unsigned>(value);
    }

    // [Premium] enable
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Premium", u"enable", u"false", 1023), u"true");
        g_PremiumSetting.enable = value;
    }

    // [Premium] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"Premium", u"address", u"127.0.0.1", 1023));
        l2server::PublishLegacyAddressExact(g_PremiumSetting.serverAddr, value);
    }

    // [Premium] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Premium", u"port", 3999, kL2ServerIni);
        g_PremiumSetting.serverPort = static_cast<short>(value);
    }

    // [Premium] heartbeat
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Premium", u"heartbeat", 1, kL2ServerIni), 1000U
        );
        g_PremiumSetting.heartBeat = static_cast<unsigned>(value);
    }

    // [Premium] pause
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Premium", u"pause", u"false", 1023), u"true");
        g_PremiumSetting.pause = value;
    }

    // [Premium] pauseCashshop
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Premium", u"pauseCashshop", u"false", 1023), u"true");
        g_PremiumSetting.cashshopPause = value;
    }

    // [Premium] GiftLevelLimit
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Premium", u"GiftLevelLimit", 1, kL2ServerIni);
        g_PremiumSetting.giftLevelLimit = value;
    }

    // [Premium] StandAloneShopTest
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Premium", u"StandAloneShopTest", u"false", 1023), u"true"
        );
        g_PremiumSetting.g_bStandAloneShopTest = value;
    }

    // [Setting] PacketRepeatLimit
    {
        const auto value =
            std::clamp(l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PacketRepeatLimit", 0, kL2ServerIni), 0, 10);
        g_nPacketRepeatLimit = value;
    }

    // [Setting] PacketRepeatLimitTime
    {
        const auto value =
            std::max(l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PacketRepeatLimitTime", 1000, kL2ServerIni), 0);
        g_nPacketRepeatLimitTime = value;
    }

    // [Setting] FreeTeleportNeedItem
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"FreeTeleportNeedItem", u"false", 1023), u"true"
        );
        g_bFreeTeleportNeedItem = value;
    }

    // [Premium] CashshopPriceIsTotal
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Premium", u"CashshopPriceIsTotal", u"false", 1023), u"true"
        );
        g_bCashshopPriceIsTotal = value;
    }

    // [Premium] PurchaseHistoryCount
    {
        const auto value =
            std::min(l2server::g_SecureINIFileDB.GetInt(u"Premium", u"PurchaseHistoryCount", 5, kL2ServerIni), 30);
        g_PremiumSetting.purchaseHistoryCount = value;
    }

    // [PremiumUser] IsMentorBuffed
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"PremiumUser", u"IsMentorBuffed", u"false", 1023), u"true"
        );
        g_bIsMentorBuffed = value;
    }

    // [PremiumUser] account
    {
        const auto value = ReadConfigString(u"PremiumUser", u"account", u"", 15);
        l2server::PublishTargetString(g_szPremiumAccount, value);
    }

    // [GoodsInventory] enable
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"GoodsInventory", u"enable", u"false", 1023), u"true");
        g_GoodsInventorySetting.enable = value;
    }

    // [GoodsInventory] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"GoodsInventory", u"address", u"127.0.0.1", 1023));
        l2server::PublishLegacyAddressExact(g_GoodsInventorySetting.serverAddr, value);
    }

    // [GoodsInventory] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"GoodsInventory", u"port", 3999, kL2ServerIni);
        g_GoodsInventorySetting.serverPort = static_cast<short>(value);
    }

    // [GoodsInventory] heartbeat
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"GoodsInventory", u"heartbeat", 1, kL2ServerIni);
        g_GoodsInventorySetting.heartBeat = static_cast<unsigned>(value);
    }

    // [Captcha] enable
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Captcha", u"enable", u"false", 1023), u"true");
        g_CaptchaSetting.enable = value;
    }

    // [Captcha] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"Captcha", u"address", u"127.0.0.1", 1023));
        l2server::PublishLegacyAddressExact(g_CaptchaSetting.serverAddr, value);
    }

    // [Captcha] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Captcha", u"port", 43330, kL2ServerIni);
        g_CaptchaSetting.serverPort = static_cast<unsigned short>(value);
    }

    // [Captcha] heartbeat
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Captcha", u"heartbeat", 1, kL2ServerIni);
        g_CaptchaSetting.heartBeat = static_cast<unsigned>(value);
    }

    // [Captcha] reportCntSendCaptcha
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Captcha", u"reportCntSendCaptcha", 3, kL2ServerIni);
        g_CaptchaSetting.reportCntSendCaptcha = value;
    }

    // [Captcha] confirmExpiredSeconds
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Captcha", u"confirmExpiredSeconds", 1200, kL2ServerIni);
        g_CaptchaSetting.confirmExpiredSeconds = value;
    }

    // [Captcha] reportDelaySeconds
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Captcha", u"reportDelaySeconds", 1200, kL2ServerIni);
        g_CaptchaSetting.reportDelaySeconds = value;
    }

    // [Captcha] reportReuseDelaySeconds
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Captcha", u"reportReuseDelaySeconds", 1800, kL2ServerIni);
        g_CaptchaSetting.reportReuseDelaySeconds = value;
    }

    // [Captcha] canBotReportPrivateStore
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Captcha", u"canBotReportPrivateStore", u"true", 1023), u"true"
        );
        g_CaptchaSetting.canBotReportPrivateStore = value;
    }

    // [Captcha] canBotReportCraft
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Captcha", u"canBotReportCraft", u"true", 1023), u"true");
        g_CaptchaSetting.canBotReportCraft = value;
    }

    // [DynamicQuest] AdjustedGoalRate
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"DynamicQuest", u"AdjustedGoalRate", 100, kL2ServerIni);
        g_DQuestGoalAdjRate = static_cast<unsigned int>(value);
    }

    // [FreeToPlay] VitalItemDropPenaltyPercent
    {
        const auto value =
            static_cast<double>(static_cast<std::uint32_t>(
                l2server::g_SecureINIFileDB.GetInt(u"FreeToPlay", u"VitalItemDropPenaltyPercent", 0, kL2ServerIni)
            )) / 100.0 +
            1.0;
        g_dVitalItemDropPenalty = value;
    }

    // [FreeToPlay] VitalityConsumeByMob
    {
        const auto value = static_cast<double>(static_cast<std::uint32_t>(
                               l2server::g_SecureINIFileDB.GetInt(u"FreeToPlay", u"VitalityConsumeByMob", 100, kL2ServerIni)
                           )) /
            100.0;
        g_dVitalityConsumeByMobPer = value;
    }

    // [FreeToPlay] VitalityConsumeByBoss
    {
        const auto value = static_cast<double>(static_cast<std::uint32_t>(
                               l2server::g_SecureINIFileDB.GetInt(u"FreeToPlay", u"VitalityConsumeByBoss", 100, kL2ServerIni)
                           )) /
            100.0;
        g_dVitalityConsumeByBossPer = value;
    }

    // [FreeToPlay] VitalityByMobBeforeRenewal
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"FreeToPlay", u"VitalityByMobBeforeRenewal", u"false", 1023), u"true"
        );
        g_bVitalityByMobBeforeRenewal = value;
    }

    // [FreeToPlay] VitalityByBossBeforeRenewal
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"FreeToPlay", u"VitalityByBossBeforeRenewal", u"false", 1023), u"true"
        );
        g_bVitalityByBossBeforeRenewal = value;
    }

    // [FreeToPlay] FreeOpenServer
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"FreeToPlay", u"FreeOpenServer", u"false", 1023), u"true"
        );
        g_bFreeOpenServer = value;
    }

    // [FreeToPlay] ItemDropLevelPenaltyWholeServer
    {
        const bool enabled = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"FreeToPlay", u"ItemDropLevelPenaltyWholeServer", u"false", 1023), u"true"
        );
        g_nAdenaDropLevel1 = enabled ? 2 : 8;
        g_nAdenaDropLevel2 = enabled ? 6 : 15;
        g_nItemDropLevel1 = enabled ? 2 : 5;
        g_nItemDropLevel2 = enabled ? 6 : 10;
        CNPC::m_dAdenaDropValue1 = 90.0 / static_cast<double>(g_nAdenaDropLevel2 - g_nAdenaDropLevel1);
        CNPC::m_dAdenaDropValue2 = static_cast<double>(g_nAdenaDropLevel2) * CNPC::m_dAdenaDropValue1 + 10.0;
        CNPC::m_dItemDropValue1 = 90.0 / static_cast<double>(g_nItemDropLevel2 - g_nItemDropLevel1);
        CNPC::m_dItemDropValue2 = static_cast<double>(g_nItemDropLevel2) * CNPC::m_dItemDropValue1 + 10.0;
    }

    // [FreeToPlay] PremiumUserLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"FreeToPlay", u"PremiumUserLevel", -1, kL2ServerIni);
        g_nPremiumUserLevel = value;
    }

    // [Localization] CurrentAllianceMemberLimit
    {
        const auto value = std::min(
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"CurrentAllianceMemberLimit", 3, kL2ServerIni), 12
        );
        g_CurrentAllianceMemberLimit = value;
    }

    // [Localization] PledgeWarWaitingTime
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PledgeWarWaitingTime", 72, kL2ServerIni), 3600U
        );
        BP::_PLEDGE_WAR_WAITING_TIME = value;
    }

    // [Localization] PledgeWarCancelTime
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PledgeWarCancelTime", 168, kL2ServerIni), 3600U
        );
        BP::_PLEDGE_WAR_CANCEL_TIME = value;
    }

    // [Localization] PledgeWarProcessingTime
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PledgeWarProcessingTime", 504, kL2ServerIni), 3600U
        );
        BP::_PLEDGE_WAR_PROCESSING_TIME = value;
    }

    // [Localization] PledgeWarSuspenddingTime
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PledgeWarSuspenddingTime", 504, kL2ServerIni), 3600U
        );
        BP::_PLEDGE_WAR_SUSPENDDING_TIME = value;
    }

    // [Localization] PledgeWarStanbyTime
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PledgeWarStanbyTime", 168, kL2ServerIni), 3600U
        );
        BP::_PLEDGE_WAR_STANDBY_TIME = value;
    }

    // [Localization] EasyPK
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Localization", u"EasyPK", 0, kL2ServerIni);
        g_EasyPK = value;
    }

    // [Localization] RecipeGiveBack
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Localization", u"RecipeGiveBack", u"false", 1023), u"true"
        );
        g_bRecipeGiveBack = value;
    }

    // [Localization] RecipeGiveBackPer
    {
        const auto value = static_cast<double>(static_cast<std::uint32_t>(
                               l2server::g_SecureINIFileDB.GetInt(u"Localization", u"RecipeGiveBackPer", 0, kL2ServerIni)
                           )) /
            100.0;
        g_dRecipeGiveBackPer = value;
    }

    // [Localization] ClanAdventSkill
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Localization", u"ClanAdventSkill", 19009, kL2ServerIni);
        g_nClanAdventSkill = value;
    }

    // [Localization] PartyMatchCreateLevelLimit
    {
        const auto value =
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PartyMatchCreateLevelLimit", 0, kL2ServerIni);
        g_nCreatePartyMatchLevelLimit = value;
    }

    // [Localization] MPCCRoutingOfContribute
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Localization", u"MPCCRoutingOfContribute", u"false", 1023), u"true"
        );
        g_bSetMPCCRoutingOfContribute = value;
    }

    // [Localization] PledgeLevelForPledgeGameAdmission
    {
        const auto value =
            l2server::g_SecureINIFileDB.GetInt(u"Localization", u"PledgeLevelForPledgeGameAdmission", 3, kL2ServerIni);
        g_nPledgeLevelForPledgeGameAdmission = value;
    }

    // [PacketMinimize] SendSummonHPMPInfo
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"PacketMinimize", u"SendSummonHPMPInfo", u"true", 1023), u"true"
        );
        g_bSendSummonHPMPInfo = value;
    }

    // [QueueTicket] QueueTicketEnabled
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"QueueTicket", u"QueueTicketEnabled", u"false", 1023), u"true"
        );
        g_bQueueTicketEnabled = value;
    }

    // [QueueTicket] WaiterMaxCount
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"QueueTicket", u"WaiterMaxCount", 5000, kL2ServerIni);
        g_nWaiterMaxCount = value;
    }

    // [QueueTicket] PassVipLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"QueueTicket", u"PassVipLevel", 1, kL2ServerIni);
        g_nQueueTicketPassVipLevel = value;
    }

    // [QueueTicket] WaiterLevel
    {
        const auto input = ReadConfigString(u"QueueTicket", u"WaiterLevel", u"none", 1023);
        const bool vip = l2server::EqualsAsciiCaseInsensitive(input, u"vip");
        g_bQueueTicketNormalWait = vip || l2server::EqualsAsciiCaseInsensitive(input, u"normal");
        g_bQueueTicketVipWait = vip;
    }

    // [QueueTicket] BuilderOff
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"QueueTicket", u"BuilderOff", u"false", 1023), u"true");
        g_bQueueTicketBuilderOff = value;
    }

    // [Relay] address
    {
        const auto value = l2server::LegacyInetAddress(ReadConfigString(u"Relay", u"address", u"127.0.0.1", 1023));
        l2server::PublishLegacyAddressExact(g_RelaySetting.serverAddr, value);
    }

    // [Relay] port
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Relay", u"port", 3699, kL2ServerIni);
        g_RelaySetting.serverPort = static_cast<short>(value);
    }

    // [Relay] connect
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Relay", u"connect", u"false", 1023), u"true");
        g_bConnectRelayServer = value;
    }

    // [Setting] UsePledgeBonus
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UsePledgeBonus", u"false", 1023), u"true");
        g_bUsePledgeBonus = value;
    }

    // [Setting] DropAdenaToInventory
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"DropAdenaToInventory", u"false", 1023), u"true"
        );
        g_bDropAdenaToInventory = value;
    }

    LoadWorldChatSection();
    LoadChannelChattingSection();
    LoadAccountServerSection();
}

void CConfig::MakeAdminIPList(SharedUtilsWideChar* list) {
    for (const std::u16string_view token : l2server::SplitNonempty(std::u16string_view(list), u',')) {
        g_AdminIP.emplace_back(token);
    }
}

void CConfig::MakeUIEventIDList(SharedUtilsWideChar* list) {
    for (const std::u16string_view token : l2server::SplitNonempty(std::u16string_view(list), u':')) {
        const int eventId = l2server::Wcstol10(token);
        if (eventId != 0) {
            UIEventCountManager::Inst()->PushReservedID(eventId);
        }
    }
}

void CConfig::SetEventStartDate(SharedUtilsWideChar* value) {
    l2server::PublishBirthdayEventStart(l2server::ParseBirthdayDate(std::u16string_view(value)));
}

void CConfig::LoadSettingSection() {
    L2_GUARD_FUNCTION();

    // Preserve allocation and immediate publication in the original read order.
    mailServer = static_cast<char*>(std::malloc(1000U));

    // [Setting] server_type
    {
        const auto value = l2server::LookupServerType(ReadConfigString(u"Setting", u"server_type", u"live", 1024));
        g_L2ServerType = static_cast<L2ServerType::Value>(value);
    }

    // [Setting] binary_update_version
    {
        const auto value = ReadConfigString(
            u"Setting", u"binary_update_version", u"Please insert binary_update_version in setting section!", 1023
        );
        l2server::PublishTargetString(g_BinaryInfo.updateVersion, value);
    }

    // [Setting] MailServer
    {
        const auto value = ReadConfigString(u"Setting", u"MailServer", u"mail.ncsoft.co.kr", 999);
        UnicodeToAnsi(const_cast<char16_t*>(value.c_str()), 1000, mailServer, 1000);
    }

    // [Setting] UserPathFind
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UserPathFind", u"true", 1023), u"true");
        g_bUserPathFind = value;
    }

    // [Setting] DiePenalty
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"DiePenalty", u"true", 1023), u"true");
        g_bDiePenalty = value;
    }

    // [Setting] WorldCollision
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"WorldCollision", u"true", 1023), u"true");
        g_bWorldCollision = value;
    }

    // [Setting] OptionalErrorLogEnabled
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"OptionalErrorLogEnabled", u"false", 1023), u"true"
        );
        g_OptionalErrorLogEnabled = value;
    }

    // [Setting] UserPort
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"UserPort", 7777, kL2ServerIni);
        g_nUserPort = value;
    }

    // [Setting] NPCPort
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"NPCPort", 2002, kL2ServerIni);
        g_nNPCPort = value;
    }

    // [Setting] RaidPort
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"RaidPort", 8888, kL2ServerIni);
        g_nRaidPort = value;
    }

    // [Setting] OlympiadPort
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"OlympiadPort", 9999, kL2ServerIni);
        g_nOlympiadPort = value;
    }

    // [Setting] Country
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"Country", 0, kL2ServerIni);
        ::g_nCountryCode = value;
    }

    // [Setting] Snoop
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"Snoop", 0, kL2ServerIni) != 0;
        g_bSnoop = value;
    }

    // [Setting] UseVehicle
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"UseVehicle", 1, kL2ServerIni) != 0;
        g_bUseVehicle = value;
    }

    // [Setting] IsMergedServer
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"IsMergedServer", 0, kL2ServerIni) != 0;
        g_bMergedServer = value;
    }

    // [Setting] ChangeWrongCharName
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"ChangeWrongCharName", 0, kL2ServerIni) != 0;
        g_bChangeWrongCharName = value;
    }

    // [Setting] TeleportToNPCInternal
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"TeleportToNPCInternal", 0, kL2ServerIni) != 0;
        g_bEnableTeleportToNPCInternal = value;
        if (g_bMergedServer) {
            // Log.Add(LOG_NORMAL, L"Merged Server")
            // L2_LOG_INFO(Log, L"Merged Server")
            L2_LOG_INFO(Log, "[configuration] Merged-server mode is enabled.");
        }
    }

    if (g_L2ServerType == L2ServerType::DEV) {
        // [Setting] MailTo
        {
            const auto value = ReadConfigString(u"Setting", u"MailTo", u"", 1023);
            {
                std::array<char, 1024> receivers{};
                UnicodeToAnsi(const_cast<char16_t*>(value.c_str()), 0x400, receivers.data(), 0x400);
                SystemMail::SetReceivers(receivers.data());
            }
        }
    }

    // [Setting] ServerStopperEnabled
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ServerStopperEnabled", u"false", 1023), u"true"
        );
        g_bServerStopperEnabled = value;
    }

    // [Setting] ServerStopperKey
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"ServerStopperKey", 0, kL2ServerIni);
        g_nServerStopperKey = static_cast<std::uint32_t>(value);
    }

    // [Setting] EnablePacketShuffle
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"EnablePacketShuffle", u"false", 1023), u"true"
        );
        g_bEnablePacketShuffle = value;
    }

    // [Setting] UseOneDayReward
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UseOneDayReward", u"false", 1023), u"true");
        g_bUseOneDayReward = value;
    }

    // [Setting] UseAutoSoulShotUI
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UseAutoSoulShotUI", u"false", 1023), u"true"
        );
        g_bUseAutoSoulShotUI = value;
    }

    // [Setting] AdminIP
    {
        auto value = ReadConfigString(u"Setting", u"AdminIP", u"", 1024);
        MakeAdminIPList(value.data());
    }

    // [Setting] autologoutsec
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"autologoutsec", 600, kL2ServerIni);
        g_nAutoLogoutTimeLimit = value;
    }

    // [Setting] BotUnseleteTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BotUnseleteTime", 500, kL2ServerIni);
        g_dBotUnseleteTime = value;
    }

    // [Setting] GotDamageLog
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"GotDamageLog", u"false", 1024), u"true");
        g_bGotDamageLog = value;
    }

    // [Setting] BOTMessage
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"BOTMessage", u"false", 1024), u"true");
        g_bBOTMessage = value;
    }

    // [Setting] BOTLog
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"BOTLog", u"false", 1024), u"true");
        g_bBOTLog = value;
    }

    // [Setting] BOTCheckTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BOTCheckTime", 120000, kL2ServerIni);
        g_nBOTCheckTime = value;
    }

    // [Setting] BOTCheckWaitTimeLimit
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BOTCheckWaitTimeLimit", 110000, kL2ServerIni);
        g_nBOTCheckWaitTimeLimit = value;
    }

    // [Setting] BOTBlockSec
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BOTBlockSec", 30, kL2ServerIni), 1000U
        );
        CBotBlockManager::inst().m_BanPeriodMSec = static_cast<std::uint32_t>(value);
    }

    // [Setting] AcceptEscapeCommand: the original read is discarded.
    (void)ReadConfigString(u"Setting", u"AcceptEscapeCommand", u"false", 1024);

    // [Setting] AcceptLowerProtocol
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"AcceptLowerProtocol", u"false", 1024), u"true"
        );
        g_bAcceptLowerProtocol = value;
    }

    // [Setting] playtimelimit
    {
        const auto input = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"playtimelimit", 0, kL2ServerIni);
        const auto value = input > 0 ? runtime::MultiplyInt32Wrapping(input, 3600U) : input;
        g_nPlayTimeLimit = value;
        g_nPlayTimeChanged = 0;
        g_bNewVote = false;
    }

    // [Setting] reconnectAuth
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"reconnectAuth", 0, kL2ServerIni);
        g_nUseReconnectAuth = value;
    }

    // [Setting] AgeCheckType
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"AgeCheckType", 0, kL2ServerIni);
        g_nAgeCheckType = value;
    }

    // [Setting] MaxSpawnNumPerOneTick
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"MaxSpawnNumPerOneTick", 100, kL2ServerIni);
        g_nMaxSpawnNumPerOneTick = value;
    }

    // [Setting] MaxDespawnNumPerOneTick
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"MaxDespawnNumPerOneTick", 500, kL2ServerIni);
        g_nMaxDespawnNumPerOneTick = value;
    }

    // [Setting] canrolldice
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"canrolldice", 1, kL2ServerIni) != 0;
        g_bCanRollDice = value;
    }

    // [Setting] taitime
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"taitime", u"false", 1024), u"true");
        g_b22to6DisableGame = value;
    }

    // [Setting] ItemLeakDetailCheck
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ItemLeakDetailCheck", u"false", 1024), u"true"
        );
        g_bItemOrtType = value;
    }

    // [Setting] gameguard
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"gameguard", u"false", 1024), u"true");
        g_bCheckGameGuardAuth = value;
    }

    // [Setting] gameguard_checkonly
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"gameguard_checkonly", u"false", 1024), u"true"
        );
        g_bCheckGameGuardAuthCheckOnly = value;
    }

    // [Setting] gameguard_builder_log
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"gameguard_builder_log", u"true", 1024), u"true"
        );
        g_bBuilderLogAdd = value;
    }

    // [Setting] EscapeMode
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"EscapeMode", 1, kL2ServerIni);
        g_nEscapeMode = value;
    }

    // [Setting] direct_command
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"direct_command", u"false", 1024), u"true");
        g_bEnableDirectCommand = value;
    }

    // [Setting] minidump
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"minidump", u"true", 1024), u"true");
        g_bExceptionMakeMiniDump = value;
    }

    // [Setting] minidump_type
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"minidump_type", 1, kL2ServerIni);
        g_nExceptionMiniDumpType = value;
    }

    // [Setting] newinspector
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"newinspector", u"false", 1024), u"true");
        g_bCheckNewInspectorAuth = value;
    }

    // [Setting] ncguard
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"ncguard", u"false", 1024), u"true");
        g_bInitedNCGuard = value;
    }

    // [Setting] newinspector_checkonly
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"newinspector_checkonly", u"false", 1024), u"true"
        );
        g_bCheckNewInspectorAuthCheckOnly = value;
    }

    // [Setting] newinspectorversion
    {
        const auto value = ReadConfigString(u"Setting", u"newinspectorversion", u"serverdata.dat", 49);
        l2server::PublishTargetString(g_newinspectorversion, value);
    }

    // [Setting] debug_hide_off
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"debug_hide_off", u"false", 1024), u"true");
        g_bDefaultDebugHideOff = value;
    }

    // [Setting] newcharactercreate
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"newcharactercreate", u"false", 1024), u"true"
        );
        g_bNewCharacterCreate = value;
    }

    // [Setting] CreateLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"CreateLevel", 1, kL2ServerIni);
        g_nCharacterCreateLevel = value;
    }

    // [Setting] birthday_event_start
    {
        auto value = ReadConfigString(u"Setting", u"birthday_event_start", u"9999/01/01", 1024);
        SetEventStartDate(value.data());
    }

    // [Setting] MultiLang
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"MultiLang", u"false", 1023), u"true");
        g_bMultiLang = value;
    }

    // [Setting] isnonpk
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"isnonpk", u"false", 1023), u"true");
        g_bIsNonPKServer = value;
        if (g_bIsNonPKServer) {
            DisableNpcPosEvent(u"18age");
        } else {
            EnableNpcPosEvent(u"18age");
        }
    }

    // [Setting] cannotmonrace
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"cannotmonrace", u"true", 1023), u"true");
        g_bCannotMonRace = value;
    }

    // [Setting] disable18age
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"disable18age", u"false", 1023), u"true");
        g_bDisable18Age = value;
        if (g_bDisable18Age) {
            DisableNpcPosEvent(u"18age");
        } else {
            EnableNpcPosEvent(u"18age");
        }
    }

    // [Setting] TestServerPenalty
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"TestServerPenalty", u"false", 1023), u"true"
        );
        g_bTestServerPenalty = value;
    }

    // [Setting] LogMsn
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"LogMsn", u"true", 1023), u"true");
        g_bLogMsnMsg = value;
        // Log.Add(
        //     LOG_NORMAL,
        //     g_bLogMsnMsg
        //         ? L"MSN Logging is ON"
        //         : L"MSN Logging is OFF")
        // L2_LOG_INFO(Log,
        //     g_bLogMsnMsg
        //         ? L"MSN Logging is ON"
        //         : L"MSN Logging is OFF")
        L2_LOG_INFO(Log, "[configuration] MSN message logging is %s.", g_bLogMsnMsg ? "enabled" : "disabled");
    }

    // [Setting] ProhibitCharCreation
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ProhibitCharCreation", u"false", 1023), u"true"
        );
        g_bProhibitAllCharCreation = value;
    }

    // [Setting] ProhibitCharDeletion
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ProhibitCharDeletion", u"false", 1023), u"true"
        );
        g_bProhibitAllCharDeletion = value;
    }

    // [Setting] ProhibitFirstCharCreation
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ProhibitFirstCharCreation", u"false", 1023), u"true"
        );
        g_bProhibitFirstCharCreation = value;
    }

    // [Setting] ProhibitCharCreationCount
    {
        const auto input = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"ProhibitCharCreationCount", 7, kL2ServerIni);
        const auto value = input < 0 || input > 7 ? 7 : input;
        g_nProhibitCharCreationCount = value;
    }

    // [Setting] UserPacketMonitor
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UserPacketMonitor", u"false", 1023), u"true"
        );
        g_bUserPacketMonitor = value;
    }

    // [Setting] UsePacketLog
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UsePacketLog", u"false", 1023), u"true");
        g_bWritePacketLog = value;
    }

    // [Setting] BotItem
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BotItem", 10213, kL2ServerIni);
        g_nBotCheckItemClassID = value;
    }

    // [Setting] BotItemCount
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BotItemCount", 1, kL2ServerIni);
        g_nBotCheckItemCount = value;
    }

    // [Setting] FakeItem
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"FakeItem", 57, kL2ServerIni);
        g_nFakeItemClassID = value;
    }

    // [Setting] PPTP
    {
        const auto input = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PPTP", 1, kL2ServerIni);
        const auto value = input < 1 || input > 3 ? 1 : input;
        g_nPPTPOption = value;
    }

    // [Setting] IPBlock
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"IPBlock", 0, kL2ServerIni);
        g_nIPBlock = value;
    }

    // [Setting] MaxUserSocketPerIP
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"MaxUserSocketPerIP", 0, kL2ServerIni);
        g_nMaxUserSocketPerIP = value;
    }

    // [Setting] MaxUserSocketPerMAC
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"MaxUserSocketPerMAC", 0, kL2ServerIni);
        g_nMaxUserSocketPerMAC = value;
    }

    // [Setting] UseResourceLoader
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UseResourceLoader", u"false", 1023), u"true"
        );
        g_bUseResourceLoader = value;
    }

    // [Setting] RunTestCase
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"RunTestCase", u"true", 1023), u"true");
        g_bRunTestCase = value;
    }

    // [Setting] ShowTestCaseProgress
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ShowTestCaseProgress", u"false", 1023), u"true"
        );
        g_bShowTestCaseProgress = value;
    }

    // [Setting] PingCountOff
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"PingCountOff", u"false", 1023), u"true");
        g_bPingCountOff = value;
    }

    // [Setting] PingOnRaid
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"PingOnRaid", u"false", 1023), u"true");
        g_bPingOnRaid = value;
    }

    // [Setting] LoadEventDataFromDB
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"LoadEventDataFromDB", u"false", 1023), u"true"
        );
        g_bLoadEventDataFromDB = value;
    }

    // [Setting] UseServerStartMail
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadProfileConfigString(u"Setting", u"UseServerStartMail", u"false", 1024), u"true"
        );
        g_bUseServerStartMail = value;
    }

    // [Setting] DistanceBroadcastingHPMP
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"DistanceBroadcastingHPMP", 600, kL2ServerIni);
        g_nDistanceBroadcastingHPMP = value;
    }

    // [Setting] UseAuthUserData
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UseAuthUserData", u"false", 1023), u"true");
        g_bUseAuthUserData = value;
    }

    // [Setting] BirthdayItem
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"BirthdayItem", 21169, kL2ServerIni);
        g_nBirthdayItemClassID = value;
    }

    // [Setting] PKPenaltyItemDrop
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"PKPenaltyItemDrop", u"false", 1023), u"true"
        );
        g_bPKPenaltyItemDrop = value;
    }

    // [Setting] PKPenaltyCount
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PKPenaltyCount", 4, kL2ServerIni);
        g_nPKPenaltyCount = value;
    }

    // [Setting] PKPenaltyDropPer
    {
        const auto value = static_cast<double>(static_cast<std::uint32_t>(
                               l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PKPenaltyDropPer", 100, kL2ServerIni)
                           )) /
            100.0;
        g_dDropPenaltyRate = value;
    }

    // [Setting] PKPenaltyDropItemLimit
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PKPenaltyDropItemLimit", 5, kL2ServerIni);
        g_nMaxDropItemLimit = value;
    }

    // [Setting] PKKarmaPenalty
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"PKKarmaPenalty", u"true", 1023), u"true");
        g_bPKKarmaPenalty = value;
    }

    // [Setting] InterludeKarma
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"InterludeKarma", u"false", 1023), u"true");
        g_bInterludeKarma = value;
    }

    // [ChatLimit] TargetUser
    {
        const auto input = ReadConfigString(u"ChatLimit", u"TargetUser", u"none", 1023);
        int value = 2;
        if (l2server::EqualsAsciiCaseInsensitive(input, u"non_premium")) {
            value = 1;
        } else if (l2server::EqualsAsciiCaseInsensitive(input, u"all")) {
            value = 0;
        }
        g_nChatLimitTarget = value;
    }

    // [ChatLimit] AllLimitLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"ChatLimit", u"AllLimitLevel", 0, kL2ServerIni);
        g_nChatLimitLevelAll = value;
        if (g_nChatLimitLevelAll > 0) {
            g_nChatLimitLevelNormal = g_nChatLimitLevelAll;
            g_nChatLimitLevelShout = g_nChatLimitLevelAll;
            g_nChatLimitLevelMarket = g_nChatLimitLevelAll;
            g_nChatLimitLevelWhisper = g_nChatLimitLevelAll;
        }
    }

    // [ChatLimit] NormalLimitLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"ChatLimit", u"NormalLimitLevel", 0, kL2ServerIni);
        g_nChatLimitLevelNormal = value;
    }

    // [ChatLimit] ShoutLimitLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"ChatLimit", u"ShoutLimitLevel", 0, kL2ServerIni);
        g_nChatLimitLevelShout = value;
    }

    // [ChatLimit] MarketLimitLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"ChatLimit", u"MarketLimitLevel", 0, kL2ServerIni);
        g_nChatLimitLevelMarket = value;
    }

    // [ChatLimit] WhisperLimitLevel
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"ChatLimit", u"WhisperLimitLevel", 0, kL2ServerIni);
        g_nChatLimitLevelWhisper = value;
    }

    // [ChatLimit] ReplyWhisperOnly
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"ChatLimit", u"ReplyWhisperOnly", u"false", 1023), u"true"
        );
        g_bChatLimitWhisperReplyOnly = value;
    }

    // [ChatLimit] ChatLimitByOverChat
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"ChatLimit", u"ChatLimitByOverChat", u"false", 1023), u"true"
        );
        g_bChatLimitByOverChat = value;
    }

    // [ServerAutoStarter] Enabled
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadProfileConfigString(u"ServerAutoStarter", u"Enabled", u"false", 1024), u"true"
        );
        ServerAutoStarter::config.m_enabled = value;
    }

    // [ServerAutoStarter] WorkingFolder
    {
        const auto value = ReadProfileConfigString(u"ServerAutoStarter", u"WorkingFolder", u"", 1024);
        ServerAutoStarter::config.m_workingFolder = std::u16string(value);
    }

    // [ServerAutoStarter] FilePath
    {
        const auto value = ReadProfileConfigString(u"ServerAutoStarter", u"FilePath", u"", 1024);
        ServerAutoStarter::config.m_filePath = std::u16string(value);
    }

    // [BatchAutoStarter] Enabled
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadProfileConfigString(u"BatchAutoStarter", u"Enabled", u"false", 1024), u"true"
        );
        BatchAutoStarter::config.m_enabled = value;
    }

    // [BatchAutoStarter] WorkingFolder
    {
        const auto value = ReadProfileConfigString(u"BatchAutoStarter", u"WorkingFolder", u"", 1024);
        BatchAutoStarter::config.m_workingFolder = std::u16string(value);
    }

    // [BatchAutoStarter] FilePath
    {
        const auto value = ReadProfileConfigString(u"BatchAutoStarter", u"FilePath", u"", 1024);
        BatchAutoStarter::config.m_filePath = std::u16string(value);
    }

    // [Setting] CheckSecondaryAuth
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"CheckSecondaryAuth", u"false", 1023), u"true"
        );
        g_bCheckSecondaryAuth = value;
    }

    // [Setting] PostFeeBase
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PostFeeBase", 100, kL2ServerIni);
        g_nPostFeeBase = value;
    }

    // [Setting] PostFeePerSlot
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PostFeePerSlot", 1000, kL2ServerIni);
        g_nPostFeePerSlot = value;
    }

    // [Setting] AllUserPccafe
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"AllUserPccafe", u"false", 1023), u"true");
        g_bAllUserPcCafe = value;
    }

    // [Setting] UserToServerPacketStatMonitor
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UserToServerPacketStatMonitor", u"false", 1023), u"true"
        );
        g_bUserToServerPacketStatMonitor = value;
    }

    // [Setting] ServerToUserPacketStatMonitor
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"ServerToUserPacketStatMonitor", u"false", 1023), u"true"
        );
        g_bServerToUserPacketStatMonitor = value;
    }

    // [Setting] NpcDieStatMonitor
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"NpcDieStatMonitor", u"false", 1023), u"true"
        );
        g_bNpcDieStatMonitor = value;
    }

    // [Setting] NpcBroadcastStatMonitor
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"NpcBroadcastStatMonitor", u"false", 1023), u"true"
        );
        g_bNPCBroadcastMonitor = value;
    }

    // [Setting] TimeAnalysisMonitor
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"TimeAnalysisMonitor", u"false", 1023), u"true"
        );
        g_bTimeAnalysisMonitor = value;
    }

    // [Setting] PCCafeBonusBuff
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"PCCafeBonusBuff", u"false", 1023), u"true");
        g_PCCafeBonusBuff = value;
    }

    // [Setting] NpcExpReset
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"NpcExpReset", u"true", 1023), u"true");
        g_bNpcExpMatchingByUserExpTable = value;
    }

    // [Setting] DormantUserDay
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"DormantUserDay", 60, kL2ServerIni);
        g_DormantUserDay = value;
    }

    // [Setting] DormantPostponeTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"DormantPostponeTime", 24, kL2ServerIni);
        g_DormantPostponeTime = value;
    }

    // [Setting] NewbiePostponeTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"NewbiePostponeTime", 168, kL2ServerIni);
        g_NewbiePostponeTime = value;
    }

    // [Setting] NewDormantInfo
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"NewDormantInfo", u"true", 1023), u"true");
        g_bNewDormantBitset = value;
    }

    // [Setting] NotDormantServer
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"NotDormantServer", u"false", 1023), u"true");
        g_bNotDormantServer = value;
    }

    // [Setting] DormantUserGift
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"DormantUserGift", 37313, kL2ServerIni);
        g_DormantUserGift = value;
    }

    // [Setting] NewbieGiftId
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"NewbieGiftId", 37313, kL2ServerIni);
        g_nNewbieGiftId = value;
    }

    // [Setting] SendGiftToNewbie
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"SendGiftToNewbie", u"true", 1023), u"true");
        g_bSendGiftToNewbie = value;
    }

    // [Setting] SendGiftToDormant
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"SendGiftToDormant", u"true", 1023), u"true");
        g_bSendGiftToDormant = value;
    }

    // [Setting] testeventserver
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"testeventserver", u"false", 1024), u"true");
        g_bTestEventServer = value;
    }

    // [Setting] MentorCanclePenalty
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"MentorCanclePenalty", 0, kL2ServerIni);
        g_nMentorCancelPanelty = value;
    }

    // [Setting] MentorGraduatePenalty
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"MentorGraduatePenalty", 0, kL2ServerIni);
        g_nMentorGraduatePanelty = value;
    }

    // [Setting] UserGarbageCollect
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UserGarbageCollect", u"false", 1023), u"true"
        );
        g_bUserGarbageCollector = value;
    }

    // [Setting] UIEventCountUse
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UIEventCountUse", u"false", 1023), u"true");
        g_bProcessUIEventCount = value;
    }

    // [Setting] UIEventCountIDs
    {
        auto value = ReadConfigString(u"Setting", u"UIEventCountIDs", u"", 1024);
        MakeUIEventIDList(value.data());
    }

    // [Setting] ClientINI
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"ClientINI", u"false", 1023), u"true");
        g_bProcessClientINI = value;
    }

    // [Setting] UseAttendanceSystem
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UseAttendanceSystem", u"false", 1023), u"true"
        );
        g_bUseAttendanceSystem = value;
    }

    // [Setting] UsePCCafeAttendance
    {
        const auto value = l2server::EqualsAsciiCaseInsensitive(
            ReadConfigString(u"Setting", u"UsePCCafeAttendance", u"false", 1023), u"true"
        );
        g_bUsePCCafeAttendance = value;
    }

    // [Setting] UseVipAttendance
    {
        const auto value =
            l2server::EqualsAsciiCaseInsensitive(ReadConfigString(u"Setting", u"UseVipAttendance", u"false", 1023), u"true");
        g_bUseVipAttendance = value;
    }

    // [Setting] AttendanceMinimumLev
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"AttendanceMinimumLev", 0, kL2ServerIni);
        g_nAttendanceMinimumLev = value;
    }

    // [Setting] AttendanceDelayTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"AttendanceDelayTime", 0, kL2ServerIni);
        g_nAttendanceDelayTime = value;
    }

    // [Setting] UserStatusLogTime
    {
        const auto value = l2server::g_SecureINIFileDB.GetInt(u"Setting", u"UserStatusLogTime", 180, kL2ServerIni);
        g_nUserStatusLogTime = value;
    }

    // [Setting] PledgePenaltyReduction
    {
        const auto value = runtime::MultiplyInt32Wrapping(
            l2server::g_SecureINIFileDB.GetInt(u"Setting", u"PledgePenaltyReduction", 1440, kL2ServerIni), 60U
        );
        BP::_PLEDGE_OUST_PENALTY_TIMEOUT = value;
        BP::_PLEDGE_WITHDRAW_PENALTY_TIMEOUT = value;
        BP::_PLEDGE_OUSTED_PENALTY_TIMEOUT = value;
    }
}

void CConfig::LoadEventSection() {
    L2_GUARD_FUNCTION();

    g_dAdenaAmountPer = ReadUnsignedConfigPercent(u"event", u"adenaamount", 100);
    g_dItemDropRatePer = ReadUnsignedConfigPercent(u"event", u"itemrate", 100);
    g_dSpoilItemDropRatePer = ReadUnsignedConfigPercent(u"event", u"spoilrate", 100);
    g_dExpSPGetRate = ReadUnsignedConfigPercent(u"event", u"exprate", 100);
    g_dPartyExpSPGetRate = ReadUnsignedConfigPercent(u"event", u"partyexprate", 100);
    g_nNotDropItemOnUserDie = l2server::g_SecureINIFileDB.GetInt(u"event", u"dontdropitem", 0, kL2ServerIni) != 0;
    g_nNotDecExp = l2server::g_SecureINIFileDB.GetInt(u"event", u"dontdecexp", 0, kL2ServerIni) != 0;
    g_dAIGiveEXPRate = ReadUnsignedConfigPercent(u"event", u"aiexprate", 100);
    g_dAiAdenaRate = ReadUnsignedConfigPercent(u"event", u"aiadenarate", 0);
    g_dPledgeNameValuePer = ReadUnsignedConfigPercent(u"event", u"pledgenamevaluerate", 100);
    g_dEventAdjustGoalRate = ReadUnsignedConfigPercent(u"event", u"AdjustedGoalRate", 100);

    std::array<char16_t, 1024> buffer{};
    g_bChargeFree = IsNormalizedTrue(ReadNormalizedConfigString(u"event", u"ChargeFree", u"false", buffer));
    g_bChargeDistinction = IsNormalizedTrue(ReadNormalizedConfigString(u"event", u"ChargeDistinction", u"false", buffer));
    g_bDoNotUseInClassic_ExpPenalty =
        IsNormalizedTrue(ReadNormalizedConfigString(u"event", u"DoNotUseInClassic_ExpPenalty", u"false", buffer));
    g_MAX_LEVELDIFF_PARTY_GIVEN_EXP =
        IsNormalizedTrue(
            ReadNormalizedConfigString(u"event", u"DoNotUseInClassic_PartyMaxLevelDiffPenalty", u"false", buffer)
        )
        ? 121
        : 10;
    g_nPartyLevelDiffExpPenaltyIndex =
        l2server::g_SecureINIFileDB.GetInt(u"event", u"PartyLevelDiffExpPenaltyIndex", 0, kL2ServerIni);
    if (g_nPartyLevelDiffExpPenaltyIndex > 6) {
        g_nPartyLevelDiffExpPenaltyIndex = 0;
    }
    g_MAX_LEVELDIFF_MPCC_GIVEN_EXP =
        IsNormalizedTrue(
            ReadNormalizedConfigString(u"event", u"DoNotUseInClassic_MPCCMaxLevelDiffPenalty", u"false", buffer)
        )
        ? 121
        : 10;
}

void CConfig::LoadFreeToPlaySection() {
    L2_GUARD_FUNCTION();

    constexpr std::u16string_view section = u"FreeToPlayUserSetting";
    g_PremiumUserBoost.expSpBoostMinLevel =
        l2server::g_SecureINIFileDB.GetInt(section, u"ExpSpBoostMinLevel", 0, kL2ServerIni);
    g_PremiumUserBoost.expBoost = ReadUnsignedConfigPercent(section, u"ExpBoostPercent", 0);
    g_PremiumUserBoost.spBoost = ReadUnsignedConfigPercent(section, u"SpBoostPercent", 0);
    g_PremiumUserBoost.spoilBoost = ReadUnsignedConfigPercent(section, u"SpoilBoostPercent", 0);
    g_PremiumUserBoost.normalItemDropBoost = ReadUnsignedConfigPercent(section, u"DropBoostPercent", 0);
    g_PremiumUserBoost.vitalItemDropBoost = ReadUnsignedConfigPercent(section, u"VitalItemDropBoostPercent", 0);
    g_nActivateCharacterAmountInF2P =
        l2server::g_SecureINIFileDB.GetInt(section, u"ActivateCharacterAmount", 7, kL2ServerIni);

    std::array<char16_t, 1024> buffer{};
    ReadDecimalPair(u"VitalBonus", g_nVitalBonusForFreeUser, g_nVitalBonusForPremiumUser, buffer);
    ReadDecimalPair(
        u"VitalItemCount", g_nMaxRestoreVitalItemCountForFreeUser, g_nMaxRestoreVitalItemCountForPremiumUser, buffer
    );
    ReadDecimalPair(u"PKLimitCount", g_nPKCountForFreeUser, g_nPKCountForPremiumUser, buffer);
    ReadBooleanPair(u"CanCreatePledge", g_bCanCreatePledgeForFreeUser, g_bCanCreatePledgeForPremiumUser, buffer);
    ReadDecimalPair(
        u"PrivateStoreSellLimit", g_nPrivateStoreSellLimitForFreeUser, g_nPrivateStoreSellLimitForPremiumUser, buffer
    );
    ReadBooleanPair(
        u"CanOpenPrivateStoreBuy", g_bCanPrivateStoreBuyForFreeUser, g_bCanPrivateStoreBuyForPremiumUser, buffer
    );
    ReadBooleanPair(
        u"CanRegisterCommission", g_bCanRegisterCommissionForFreeUser, g_bCanRegisterCommissionForPremiumUser, buffer
    );
    ReadBooleanPair(u"CanItemAttachedPost", g_bCanItemAttachedPostForFreeUser, g_bCanItemAttachedPostForPremiumUser, buffer);
    g_bDailyPostLimitForFreeUser = IsNormalizedTrue(ReadNormalizedConfigString(section, u"DailyPostlimit", u"true", buffer));
    ReadBooleanPair(u"CanBeMentor", g_bCanBeMentorForFreeUser, g_bCanBeMentorForPremiumUser, buffer);

    constexpr std::u16string_view itemKeys[] = {
        u"Item0",
        u"Item1",
        u"Item2",
        u"Item3",
        u"Item4",
        u"Item5",
        u"Item6",
        u"Item7",
        u"Item8",
        u"Item9",
    };
    for (std::size_t index = 0; index < std::size(itemKeys); ++index) {
        g_nPremiumItem[index] = l2server::g_SecureINIFileDB.GetInt(section, itemKeys[index], 0, kL2ServerIni);
    }
    g_bPremiumUserIsPCCafe = IsNormalizedTrue(ReadNormalizedConfigString(section, u"PCCafe", u"false", buffer, 0x400U));
    g_PremiumUserBuff = l2server::g_SecureINIFileDB.GetInt(section, u"PremiumSpecialBuff", 0, kL2ServerIni);
}

void CConfig::LoadWorldChatSection() {
    L2_GUARD_FUNCTION();

    g_WorldChat.eAccount =
        static_cast<WorldChat::Account>(l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"EnableAccount", 0, kL2ServerIni));
    g_WorldChat.nMaxChatCnt = l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"MaxCount", 0, kL2ServerIni);
    g_WorldChat.nMinLevForFree = l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"LevelFree", 85, kL2ServerIni);
    g_WorldChat.nMinLevForPremium = l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"LevelPremium", 85, kL2ServerIni);
    g_WorldChat.nComsumePointForFree = l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"PointForFree", 2, kL2ServerIni);
    g_WorldChat.nComsumePointForPremium =
        l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"PointForPremium", 1, kL2ServerIni);
    g_WorldChat.nIntervalForFree =
        l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"TimeIntervalForFree", 60, kL2ServerIni);
    g_WorldChat.nIntervalForPremium =
        l2server::g_SecureINIFileDB.GetInt(u"WorldChat", u"TimeIntervalForPremium", 30, kL2ServerIni);
    g_WorldChat.nQuestID1 = 0x2a3f;
}

void CConfig::LoadChannelChattingSection() {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 1024> buffer{};
    const std::uint32_t address =
        LegacyAddressValue(ReadNormalizedConfigString(u"ChannelChatting", u"address", u"127.0.0.1", buffer));
    std::memcpy(&g_stChannelChattingInfo.m_stChannelChattingAddress, &address, sizeof(address));
    g_stChannelChattingInfo.ip = address;
    g_stChannelChattingInfo.m_hChannelChattingPort =
        static_cast<short>(l2server::g_SecureINIFileDB.GetInt(u"ChannelChatting", u"port", 2044, kL2ServerIni));

    const std::uint32_t clientAddress =
        LegacyAddressValue(ReadNormalizedConfigString(u"ChannelChatting", u"clientaddress", u"127.0.0.1", buffer));
    std::memcpy(&g_stChannelChattingInfo.m_stChannelChattingClientAddress, &clientAddress, sizeof(clientAddress));
    g_stChannelChattingInfo.clientip = clientAddress;
    g_stChannelChattingInfo.m_nGameServerID =
        l2server::g_SecureINIFileDB.GetInt(u"ChannelChatting", u"GameServerID", 1, kL2ServerIni);
    g_stChannelChattingInfo.m_hChannelChattingPortForClient =
        static_cast<short>(l2server::g_SecureINIFileDB.GetInt(u"ChannelChatting", u"clientport", 2044, kL2ServerIni));
}

void CConfig::LoadAccountServerSection() {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 1024> buffer{};
    g_AccountServerSetting.bConnect =
        IsNormalizedTrue(ReadNormalizedConfigString(u"AccountServer", u"enable", u"false", buffer));
    PublishLegacyAddress(
        g_AccountServerSetting.serverAddr, ReadNormalizedConfigString(u"AccountServer", u"address", u"127.0.0.1", buffer)
    );
    g_AccountServerSetting.serverPort =
        static_cast<short>(l2server::g_SecureINIFileDB.GetInt(u"AccountServer", u"port", 12121, kL2ServerIni));
    g_AccountServerSetting.heartBeat = l2server::g_SecureINIFileDB.GetInt(u"AccountServer", u"heartbeat", 100, kL2ServerIni);
}
