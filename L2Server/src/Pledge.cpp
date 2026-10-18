#include "Pledge.h"
#include "AccountJobFinishSignal.h"
#include "Agit.h"
#include "AirShip.h"
#include "Alliance.h"
#include "BattleCamp.h"
#include "BeautyManager.h"
#include "CConfig.h"
#include "Castle.h"
#include "CommCli.h"
#include "CommissionTrade.h"
#include "CompeteAgit.h"
#include "DB.h"
#include "DeferredJob.h"
#include "Dominion.h"
#include "Fortress.h"
#include "HtmlGenerator.h"
#include "InstantAgit.h"
#include "NpcServer.h"
#include "Packet.h"
#include "Pet.h"
#include "PledgeFunction.h"
#include "PledgeItem.h"
#include "PledgeJoinSystem.h"
#include "PledgeLevel.h"
#include "PledgeMastery.h"
#include "PledgeMemberList.h"
#include "PledgeMemberService.h"
#include "PledgeRaid.h"
#include "PledgeSkill.h"
#include "PledgeStuff.h"
#include "Portable/ProcessRuntime.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Residence.h"
#include "SerializedMessage.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeRanking.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/UserConst.h"
#include "Skill.h"
#include "SocialClass.h"
#include "TeamBattleAgit.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <new>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace BP {

int _PLEDGE_WAR_TIMEOUT = 0x15180;
int _PLEDGE_OUST_PENALTY_TIMEOUT = 0x15180;
int _PLEDGE_WITHDRAW_PENALTY_TIMEOUT = 0x15180;
int _PLEDGE_OUSTED_PENALTY_TIMEOUT = 0x15180;
int _PLEDGE_RECHALLENGE_PENALTY_TIMEOUT = 0x69780;
int _PLEDGE_RECHALLENGE_DESPITE_REJECTION_PENALTY_TIMEOUT = 0x69780;
int _PLEDGE_WAR_CANCEL_TIME = 0x93a80;
int _PLEDGE_WAR_WAITING_TIME = 0x3f480;
int _PLEDGE_WAR_PROCESSING_TIME = 0x1baf80;
int _PLEDGE_WAR_SUSPENDDING_TIME = 0x1baf80;
int _PLEDGE_WAR_STANDBY_TIME = 0x93a80;
int _PLEDGE_HEARTBEAT_FOR_EXPIRATION = 0xea60;
int _PLEDGE_DISMISS_TIMEOUT = 0x93a80;
int _PLEDGE_DISMISS_PENALTY_TIMEOUT = 0xd2f00;
int _PLEDGE_DISMISS_PROPOSE_PENALTY_TIMEOUT = 0x93a80;
int _ALLIANCE_OUST_PENALTY_TIMEOUT = 0x15180;
int _ALLIANCE_WITHDRAW_PENALTY_TIMEOUT = 0x15180;
int _ALLIANCE_OUSTED_PENALTY_TIMEOUT = 0x15180;
int _ALLIANCE_DISMISS_PENALTY_TIMEOUT = 0x15180;
int _ALLIANCE_RECHALLENGE_PENALTY_TIMEOUT = 0x69780;
int _ALLIANCE_RECHALLENGE_DESPITE_REJECTION_PENALTY_TIMEOUT = 0x69780;
int _CASTLE_ANNOUNCE_TIME = 0x15180;
int _CASTLE_STANDBY_TIME = 0x15180;
int _CASTLE_IN_WAR_TIME = 0x1c20;
int _CASTLE_CHANGE_TIME = 0xe10;
int _DOMINION_STANDBY_TIME = 0;
int _DOMINION_SIEGE_TIME = 0x1c20;


} // namespace BP

void CPledge::SetPledgePenalty() {
    // Current L2Server 0x0096a26c. The PDB names this static Pledge.cpp
    // owner. Current .data fixes the normal table and this method applies
    // only the exact TestServerPenalty overrides.
    if (!g_bTestServerPenalty) {
        return;
    }
    BP::_PLEDGE_WAR_TIMEOUT = 60;
    BP::_PLEDGE_OUST_PENALTY_TIMEOUT = 60;
    BP::_PLEDGE_DISMISS_TIMEOUT = 420;
    BP::_PLEDGE_DISMISS_PROPOSE_PENALTY_TIMEOUT = 420;
    BP::_PLEDGE_WITHDRAW_PENALTY_TIMEOUT = 60;
    BP::_PLEDGE_OUSTED_PENALTY_TIMEOUT = 60;
    BP::_PLEDGE_RECHALLENGE_PENALTY_TIMEOUT = 300;
    BP::_CASTLE_ANNOUNCE_TIME = 0x15180;
    BP::_CASTLE_STANDBY_TIME = 0x15180;
    BP::_PLEDGE_RECHALLENGE_DESPITE_REJECTION_PENALTY_TIMEOUT = 300;
    BP::_PLEDGE_DISMISS_PENALTY_TIMEOUT = 600;
    BP::_ALLIANCE_OUST_PENALTY_TIMEOUT = 60;
    BP::_ALLIANCE_WITHDRAW_PENALTY_TIMEOUT = 60;
    BP::_ALLIANCE_OUSTED_PENALTY_TIMEOUT = 60;
    BP::_ALLIANCE_DISMISS_PENALTY_TIMEOUT = 600;
    BP::_ALLIANCE_RECHALLENGE_PENALTY_TIMEOUT = 300;
    BP::_ALLIANCE_RECHALLENGE_DESPITE_REJECTION_PENALTY_TIMEOUT = 300;
    BP::_CASTLE_IN_WAR_TIME = 0x1c20;
}

int CPledge::GetPledgeCountToBeLoaded() {
    // Current L2Server 0x0096f158.
    L2_READ_GUARD(s_lockSet);
    return static_cast<int>(s_setPledgeToBeLoaded.size());
}

void CPledge::RequestReservedLoaded() {
    // Current L2Server 0x00974ef8.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lockSet);
    for (const auto& key : s_setPledgeToBeLoaded) {
        db.RequestLoadPledge(key.m_PrimaryKey, key.m_SecondaryKey, true);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] ::RequestReservedLoaded LoadPledge PledgeDBID[%d]",
        //     key.m_SecondaryKey)
        // Noise: Per-clan database load requests repeat on startup retries. The phase and failure diagnostics provide the
        // useful context. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] ::RequestReservedLoaded LoadPledge PledgeDBID[%d]",
        //     key.m_SecondaryKey)
    }
}

void CPledge::InitPledge() {
    // Current L2Server 0x00981df4.
    L2_GUARD_FUNCTION();

    db.RequestLoadDismissReservedPledge();
    static_cast<void>(runtime::GetTickCount());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][InitPledge] RequestLoadDismissReservedPledge Start")
    // Noise: Routine request-start trace. The following branch reports the dissolution-load outcome.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][InitPledge] RequestLoadDismissReservedPledge Start")
    if (g_JobFinishSignal.m_nMustLoadDismissReserved < 1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Dissmiss Reserved Pledge], Count: 0")
        // L2_LOG_INFO(Log,
        //     L"Load Success[Dissmiss Reserved Pledge], Count: 0")
        L2_LOG_INFO(Log, "No reserved clan dissolutions are required; marking this startup dependency ready.");
        g_JobFinishSignal.NotifyFinish(JFT_LOAD_DISMISS_RESERVED);
        SetFlagReservePledge(PRLT_DISMISS);
    } else if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_DISMISS_RESERVED, 30)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Problem occurred in Loading Dismiss Reserved Pledge")
        // L2_LOG_ERROR(Log,
        //     L"Problem occurred in Loading Dismiss Reserved Pledge")
        L2_LOG_ERROR(
            Log, "Reserved clan-dissolution loading did not signal completion within 30 seconds; startup continues."
        );
    } else {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Dissmiss Reserved Pledge]")
        // L2_LOG_INFO(Log,
        //     L"Load Success[Dissmiss Reserved Pledge]")
        L2_LOG_INFO(Log, "Reserved clan-dissolution load completion signal received.");
    }

    const std::uint32_t warBegin = runtime::GetTickCount();
    db.RequestLoadAllPledgeWar();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][InitPledge] RequestLoadAllPledgeWar Start")
    // Noise: Routine request-start trace. The following branch and timing summary describe clan-war loading.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][InitPledge] RequestLoadAllPledgeWar Start")
    if (g_JobFinishSignal.m_nMustLoadWar < 1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Declared War], count: 0")
        // L2_LOG_INFO(Log,
        //     L"Load Success[Declared War], count: 0")
        L2_LOG_INFO(Log, "No declared clan wars are required; marking this startup dependency ready.");
        g_JobFinishSignal.NotifyFinish(JFT_LOAD_WAR);
        SetFlagReservePledge(PRLT_PLEDGEWAR);
    } else if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_WAR, 120)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Problem occurred in Loading Declared War")
        // L2_LOG_ERROR(Log,
        //     L"Problem occurred in Loading Declared War")
        L2_LOG_ERROR(Log, "Declared clan-war loading did not signal completion within 120 seconds; startup continues.");
    } else {
        // Log.Add(LOG_NORMAL, L"Load Success[Declared War]")
        // Noise: WaitFinish already reports the received completion signal. The next message summarizes elapsed clan-war
        // load time. L2_LOG_INFO(Log, L"Load Success[Declared War]")
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][InitPledge] RequestLoadAllPledgeWar End %d",
    //     runtime::GetTickCount() - warBegin)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][InitPledge] RequestLoadAllPledgeWar End %d",
    //     runtime::GetTickCount() - warBegin)
    L2_LOG_INFO(
        Log, "Declared clan-war load phase finished after %u ms; continuing startup.",
        static_cast<unsigned>(runtime::GetTickCount() - warBegin)
    );

    const std::uint32_t reservedBegin = runtime::GetTickCount();
    SetFlagReservePledge(PRLT_SIEGEAGIT);
    SetFlagReservePledge(PRLT_TEAMBATTLEAGIT);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][InitPledge] RequestReservedLoaded Start")
    // Noise: Routine reserved-clan phase entry is covered by its outcome and elapsed-time diagnostics.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][InitPledge] RequestReservedLoaded Start")
    if (GetPledgeCountToBeLoaded() < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][InitPledge] GetPledgeCountToBeLoaded() <= 0")
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][InitPledge] GetPledgeCountToBeLoaded() <= 0")
        L2_LOG_INFO(Log, "No reserved clans are pending; skipping the reserved-clan load wait.");
    } else {
        const unsigned int reserveTimeout = g_L2ServerType == L2ServerType::WORLDSERVER ? 600U : 300U;
        const std::uint32_t waitBegin = runtime::GetTickCount();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][InitPledge] JFT_WAIT_PLEDGE_RESERVE Start")
        // Noise: WaitFinish reports its requested timeout and outcome. A second generic wait-start trace is unnecessary.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][InitPledge] JFT_WAIT_PLEDGE_RESERVE Start")
        if (!g_JobFinishSignal.WaitFinish(JFT_WAIT_PLEDGE_RESERVE, reserveTimeout)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Problem occurred in wait reserve pledge")
            // L2_LOG_ERROR(Log,
            //     L"Problem occurred in wait reserve pledge")
            L2_LOG_ERROR(
                Log,
                "Clan load prerequisites did not become ready within %u seconds; reserved-clan requests are skipped and "
                "startup continues.",
                reserveTimeout
            );
            {
                L2_WRITE_GUARD(s_lockSet);
                // Noise: Replace eight per-bit failure records with one named readiness summary.
                // for (int index = 0. Index < 8. ++index) {
                //     // Log.Add(
                //     //     LOG_ERROR,
                //     //     L"ReserveBitset[%d]:%d",
                //     //     index,
                //     //     (s_ReserveBitset >> index) & 1U)
                //     L2_LOG_ERROR(Log,
                //         L"ReserveBitset[%d]:%d",
                //         index,
                //         (s_ReserveBitset >> index) & 1U)
                // }
                L2_LOG_ERROR(
                    Log,
                    "Clan load prerequisites: dissolution %s, halls %s, wars %s, alliances %s, team-battle halls %s, siege "
                    "halls %s, fortresses %s, castles %s.",
                    (s_ReserveBitset & (1U << 0)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 1)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 2)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 3)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 4)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 5)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 6)) != 0 ? "ready" : "pending",
                    (s_ReserveBitset & (1U << 7)) != 0 ? "ready" : "pending"
                );
            }
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][InitPledge] JFT_WAIT_PLEDGE_RESERVE End %d",
            //     runtime::GetTickCount() - waitBegin)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][InitPledge] JFT_WAIT_PLEDGE_RESERVE End %d",
            //     runtime::GetTickCount() - waitBegin)
            L2_LOG_ERROR(
                Log, "Clan prerequisite wait ended without a completion signal after %u ms.",
                static_cast<unsigned>(runtime::GetTickCount() - waitBegin)
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][InitPledge] JFT_WAIT_PLEDGE_RESERVE End %d",
            //     runtime::GetTickCount() - waitBegin)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][InitPledge] JFT_WAIT_PLEDGE_RESERVE End %d",
            //     runtime::GetTickCount() - waitBegin)
            L2_LOG_INFO(
                Log, "Clan load prerequisites are ready after %u ms; requesting reserved clans.",
                static_cast<unsigned>(runtime::GetTickCount() - waitBegin)
            );

            bool loaded = false;
            for (int attempt = 0; attempt < 5; ++attempt) {
                RequestReservedLoaded();
                if (g_JobFinishSignal.WaitFinish(JFT_LOAD_PLEDGE, 120)) {
                    // Log.Add(LOG_NORMAL, L"Load Success[Pledge]")
                    // L2_LOG_INFO(Log, L"Load Success[Pledge]")
                    L2_LOG_INFO(Log, "Reserved-clan load completion signal received on attempt %d of 5.", attempt + 1);
                    loaded = true;
                    break;
                }
            }
            if (!loaded) {
                L2_READ_GUARD(s_lockSet);
                for (const auto& key : s_setPledgeToBeLoaded) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Problem occurred in Loading Pledge dbid [%d]",
                    //     key.m_SecondaryKey)
                    // L2_LOG_ERROR(Log,
                    //     L"Problem occurred in Loading Pledge dbid [%d]",
                    //     key.m_SecondaryKey)
                    L2_LOG_ERROR(
                        Log,
                        "Clan %u in world %d is still pending after five load attempts; temporary clan objects will be "
                        "cleared.",
                        static_cast<unsigned>(key.m_SecondaryKey), key.m_PrimaryKey
                    );
                }
                s_pledgeDBMapTemp.DeleteAllObjects(const_cast<char*>("Pledge.cpp"), 0x929);
                if (g_L2ServerType == L2ServerType::WORLDSERVER) {
                    runtime::process::ReportNamedCallStack(L"LoadPledge", LOG_ERROR, false);
                }
            }
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][InitPledge] RequestReservedLoaded End %d",
    //     runtime::GetTickCount() - reservedBegin)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][InitPledge] RequestReservedLoaded End %d",
    //     runtime::GetTickCount() - reservedBegin)
    L2_LOG_INFO(
        Log, "Reserved-clan startup phase finished after %u ms; continuing with war and siege setup.",
        static_cast<unsigned>(runtime::GetTickCount() - reservedBegin)
    );

    SetAllDeclaredWarToPledges();
    CCastle::SetAllCastleSiegeToPledges();
    CCastle::AdjustSiegeTime();
    CCastle::StartAllTimer();
    CFortress::StartAllTimer();
    CCommissionTradeManager::Inst().Init();
    CBeautyManager::Inst()->Init();
    g_PledgeTimer.AddTimer(1000, 0);
}

namespace {

void DeleteAbnormalStatusFrom(CCreature* creature, int skillId) {
    // Current L2Server 0x0096eb10.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    L2_GUARD(creature->m_InfectedSkillsSect);
    for (std::size_t index = 0; index < creature->m_InfectedSkills.size(); ++index) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(creature->m_InfectedSkills, index);
        if (infected.pSkillInfo != nullptr && infected.pSkillInfo->m_nSkillId == skillId) {
            creature->_DeleteAbnormalStatus(static_cast<int>(index), creature, infected.pSkillInfo, true, SPET_DISPEL, true);
            return;
        }
    }
}

void DeleteAbnormalStatusFromSummonsOf(User* user, int skillId) {
    // Current L2Server 0x0097d820.
    L2_GUARD_FUNCTION();
    std::vector<CSummon*> summons;
    user->GetAllSummonAndPet(summons);
    for (CSummon* const summon : summons) {
        DeleteAbnormalStatusFrom(summon, skillId);
    }
}


std::u16string HostWideToTargetUtf16(const wchar_t* const value) {
    std::u16string result;
    if (value == nullptr) {
        return result;
    }
    for (const wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
        const std::uint32_t codePoint = static_cast<std::uint32_t>(*cursor);
        if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
            result.push_back(static_cast<char16_t>(codePoint));
        } else if (codePoint <= 0xffffU) {
            result.push_back(static_cast<char16_t>(codePoint));
        } else if (codePoint <= 0x10ffffU) {
            const std::uint32_t supplementary = codePoint - 0x10000U;
            result.push_back(static_cast<char16_t>(0xd800U + (supplementary >> 10U)));
            result.push_back(static_cast<char16_t>(0xdc00U + (supplementary & 0x3ffU)));
        } else {
            result.push_back(static_cast<char16_t>(codePoint));
        }
    }
    return result;
}

void AssignTargetSubPledgeName(::std::u16string& destination, wchar_t* value) {
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        destination.assign(reinterpret_cast<const char16_t*>(value));
        return;
    }

    const std::u16string converted = ::WideToUtf16(std::wstring_view(value));
    destination.assign(std::u16string_view(converted));
}

void CopyTargetSubPledgeMasterName(char16_t (&destination)[25], const wchar_t* const source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }

    const std::u16string converted = ::WideToUtf16(std::wstring_view(source));
    if (converted.size() >= std::size(destination)) {
        destination[0] = u'\0';
        return;
    }
    std::copy(converted.begin(), converted.end(), destination);
    destination[converted.size()] = u'\0';
}

template <std::size_t Capacity>
void CopyTargetPledgeName(char16_t (&destination)[Capacity], const wchar_t* const source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }
    const std::u16string converted = ::WideToUtf16(std::wstring_view(source));
    if (converted.size() >= Capacity) {
        destination[0] = u'\0';
        return;
    }
    std::copy(converted.begin(), converted.end(), destination);
    destination[converted.size()] = u'\0';
}

std::u16string LowerPledgeNameKey(const char16_t* const name) {
    static_assert(sizeof(char16_t) == sizeof(SharedUtilsWideChar));
    std::array<SharedUtilsWideChar, 32> lowered{};
    ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(name), lowered.data());
    const std::size_t length = std::char_traits<SharedUtilsWideChar>::length(lowered.data());
    return std::u16string(reinterpret_cast<const char16_t*>(lowered.data()), length);
}

#if !defined(_WIN32)
// Compatibility getter storage: valid until the next subpledge-name lookup
// on this thread. Do not replace it with a temporary string.
wchar_t* TargetSubPledgeNameToHostWide(const ::std::u16string& name) {
    thread_local std::wstring hostName;
    hostName = ::Utf16ToWide(runtime::StringView(name));
    return hostName.data();
}
#endif

bool IsPlayingGame(const int characterDatabaseId) {
    // Current anonymous Pledge.cpp owner at 0x0097df7c.
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentWorldId(), characterDatabaseId));
    return user && (user->IsOlympiadPlayer() || user->m_PledgeGameUser.IsPlayer());
}

} // namespace

CPledge::PledgeSmartIdManager CPledge::s_pledgeSmartIdManager;

void CPledge::GarbageCollect() {
    // Current L2Server.exe 0x009739a0, physical owner Pledge.obj.
    s_pledgeSmartIdManager.GarbageCollect();
}

CPledge::PledgeDbIdMap CPledge::s_pledgeDBMap;
CPledge::PledgeDbIdMapTemp CPledge::s_pledgeDBMapTemp;
CPledge::PledgeNameMap CPledge::s_pledgeNameMap;
std::set<std::CDualKey<int, std::uint32_t>> CPledge::s_setPledgeToBeLoaded;
std::uint32_t CPledge::s_ReserveBitset = 0;
RWLock CPledge::s_lockSet;
std::vector<std::uint32_t> CPledge::s_lstChallengerDbId;
std::vector<std::uint32_t> CPledge::s_lstChallengeeDbId;
std::vector<std::uint32_t> CPledge::s_lstDeclareTime;
std::vector<std::uint32_t> CPledge::s_lstPoint;
std::vector<std::uint32_t> CPledge::s_lstPointPrev;
std::vector<std::uint32_t> CPledge::s_lstDeathCount;
RWLock CPledge::s_lockList;
bool CPledge::s_ignoreMemberCountCondition = false;
CPledgeTimer g_PledgeTimer;

L2_DEFINE_OBJECT_CLASS_ID(CPledge)

L2_DEFINE_OBJECT_CLASS_NAME(CPledge, m_pClassName_CPledge)

// Current L2Server 0x006a2c00. Class-id carrier 0x20122c38 is initialized
// to number 220 by 0x00da8944/0x00da8953 (the next number is 221).
L2_DEFINE_OBJECT_ALLOCATOR(CPledge, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPledge)

CPledge::CPledge(
    const int pledgeDatabaseId,
    const int masterDatabaseId,
    wchar_t* const pledgeName,
    wchar_t* const masterName,
    const int worldId
) :
    m_data(nullptr),
    m_lock(nullptr),
    m_nAllianceSid(0),
    m_nWarCount(0),
    m_Dismissed(0),
    m_nAllianceDismissTime(0),
    m_nAllianceOustedTime(0),
    m_nAllianceWithdrawTime(0),
    m_nSiegeKillCount(0),
    m_nSiegeDeathCount(0),
    m_pCastle(nullptr),
    m_pWorldAgit(nullptr),
    m_pFortress(nullptr),
    m_pCompeteAgit(nullptr),
    m_pCastleUnderSiege{},
    m_pFortressUnderDefenceSiege(nullptr),
    m_pFortressUnderOffenceSiege(nullptr),
    m_pDominion(nullptr),
    m_tLastPledgeWorkTimeOut(0),
    m_nDeclareCount(0),
    m_LastWarDeclaredTime(0),
    m_tlDB{0, 0x11},
    m_remainedSubPledgeTypeBits(0),
    m_bSkillLoading(false),
    m_bMergedProcessing(false),
    m_bWaitingTransferMaster(false),
    m_nWorldId(worldId),
    m_bShowEmblem(false),
    m_nPrevPointTick(0),
    m_nRecruitType(-1),
    m_recruitPledgeType(static_cast<PledgeType>(0)),
    m_PledgeBonus{},
    m_pMastery(nullptr),
    m_pLevel(nullptr),
    m_pFunction(nullptr),
    m_pSkill(nullptr),
    m_pItem(nullptr) {
    // Current L2Server 0x0098460c. m_bUnderRaid and m_nStatTick are
    // not initialized here: the exact CPledge allocator above
    // pre-zeroes the complete 0x290-byte object before construction.
    m_PledgeBonus.m_PledgeBonus[0].m_nLastSaveTime = runtime::crt::Time32Now();
    m_PledgeBonus.m_PledgeBonus[1].m_nLastSaveTime = runtime::crt::Time32Now();

    CPledgeFactory& factory = CPledgeFactory::Inst();
    m_data = factory.AddById(pledgeDatabaseId);
    m_lock = factory.GetLock(m_data->index);
    m_memberList.SetObject(new CPledgeMemberList, ObjectRefType::FunctionChain);

    ResetCastleSiege();
    m_nId = s_pledgeSmartIdManager.RegisterObject(
        // 12,
        SCLS_PLEDGE, static_cast<std::uint32_t>(m_data->index), this
    );
    factory.PublishIndexId(m_data->index, m_nId);
    InitPledgeData(pledgeDatabaseId, masterDatabaseId, pledgeName, masterName);

    m_pLevel = new PledgeLevel;
    m_pMastery = new PledgeMastery(m_nId, worldId, pledgeDatabaseId);
    m_pFunction = new PledgeFunction;
    m_pSkill = new PledgeSkill;
    m_pItem = new PledgeItem;
}

CPledge::~CPledge() {
    // Current L2Server 0x00983498.
    L2_GUARD_FUNCTION();
    if (m_pItem != nullptr) {
        L2_RELEASE(m_pItem, ObjectRefType::CreateDelete);
        m_pItem = nullptr;
    }
    if (m_pSkill != nullptr) {
        L2_RELEASE(m_pSkill, ObjectRefType::CreateDelete);
        m_pSkill = nullptr;
    }
    if (m_pFunction != nullptr) {
        L2_RELEASE(m_pFunction, ObjectRefType::CreateDelete);
        m_pFunction = nullptr;
    }
    if (m_pMastery != nullptr) {
        L2_RELEASE(m_pMastery, ObjectRefType::CreateDelete);
        m_pMastery = nullptr;
    }
    if (m_pLevel != nullptr) {
        L2_RELEASE(m_pLevel, ObjectRefType::CreateDelete);
        m_pLevel = nullptr;
    }

    CPledgeFactory::Inst().RemoveByIndex(m_data->index);
    for (auto& entry : m_subPledgeMap) {
        delete entry.second;
    }
    m_subPledgeMap.clear();
    if (m_nWorldId != g_nWorldId) {
        RaidServerManager::inst().RemoveRaidPledge(*this);
    }
}

// Current L2Server 0x00983868.
L2_DEFINE_OBJECT_RELEASE(CPledge)

void CPledge::InitPledgeData(
    const int pledgeDatabaseId, const int masterDatabaseId, wchar_t* const pledgeName, wchar_t* const masterName
) {
    // Current L2Server 0x0097f1fc.
    L2_GUARD_FUNCTION();
    m_data->m_nId = static_cast<std::int32_t>(m_nId);
    m_data->m_nDbId = static_cast<std::uint32_t>(pledgeDatabaseId);
    m_data->m_nMasterDbId = static_cast<std::uint32_t>(masterDatabaseId);
    CopyTargetPledgeName(m_data->m_sPledgeName, pledgeName);
    CopyTargetPledgeName(m_data->m_sMasterName, masterName);
    m_data->m_OnlineMaster.index = 0xffffffffU;
    m_mapDeclare.clear();
    SetSubPledge(MASTER_PLEDGE, pledgeName, static_cast<std::uint32_t>(masterDatabaseId), false);
    m_memberList->SetPledge(m_nId);
    m_memberList->SetWorldId(m_nWorldId);
    m_bMergedProcessing = false;
}

const unsigned char* CPledge::AddAcademyMemberByDisassemble(int memberCount, const unsigned char* packet) {
    // Current L2Server 0x0096ee64.
    L2_GUARD_FUNCTION();
    while (memberCount > 0) {
        std::int32_t characterDatabaseId = 0;
        std::array<char16_t, 32> characterName{};
        std::array<char16_t, 32> nickname{};
        std::int32_t level = 0;
        std::int32_t classId = 0;
        std::int32_t gender = 0;
        std::int32_t race = 0;
        std::int32_t gradeId = 0;
        std::int32_t missionCheckTime = 0;
        std::int32_t joinLevel = 0;
        std::int32_t academyMasterDatabaseId = 0;
        packet = ::Disassemble(
            packet, "dSSdddddddd", &characterDatabaseId, 0x32, characterName.data(), 0x32, nickname.data(), &level, &classId,
            &gender, &race, &gradeId, &missionCheckTime, &joinLevel, &academyMasterDatabaseId
        );
        AddDbMember(
            static_cast<std::uint32_t>(characterDatabaseId), characterName.data(), nickname.data(), level, classId, gender,
            race, 0, ACADEMY, gradeId, missionCheckTime
        );
        SetAcademyMember(
            static_cast<std::uint32_t>(characterDatabaseId), joinLevel, static_cast<std::uint32_t>(academyMasterDatabaseId)
        );
        --memberCount;
    }
    return packet;
}

const unsigned char* CPledge::SetProperty(const unsigned char* const packet) {
    // Current L2Server 0x00983d78. The three unused lanes and the return of
    // the original cursor (instead of the advanced cursor) are intentional.
    L2_GUARD_FUNCTION();

    std::int32_t ignoredFirst;
    std::int32_t nameValue;
    std::int32_t ignoredThird;
    std::int32_t oustTime;
    std::int32_t allianceDatabaseId;
    std::int32_t ignoredSixth;
    std::int32_t castleDatabaseId;
    std::int32_t agitDatabaseId;
    std::int32_t fortressDatabaseId;
    std::int32_t rank;
    std::int32_t status = 0;
    std::int32_t privateFlag;
    std::int32_t crestId;
    std::int32_t guilty;
    std::int32_t dismissReservedTime;
    std::int32_t pledgeLevel;
    std::int32_t allianceOustedTime;
    std::int32_t allianceWithdrawTime;
    std::int32_t allianceDismissTime;
    std::int32_t emblemId = 0;
    std::int32_t castleSiegeDefenceCount = 0;
    std::int32_t masteryResetTime = 0;
    std::int32_t powerGradeCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddddddddddddddddddddddd", &ignoredFirst, &nameValue, &ignoredThird, &oustTime, &allianceDatabaseId,
        &ignoredSixth, &castleDatabaseId, &agitDatabaseId, &fortressDatabaseId, &rank, &status, &privateFlag, &crestId,
        &guilty, &dismissReservedTime, &pledgeLevel, &allianceOustedTime, &allianceWithdrawTime, &allianceDismissTime,
        &emblemId, &castleSiegeDefenceCount, &masteryResetTime, &powerGradeCount
    );

    SetNameValue(nameValue);
    m_data->m_nOustTime = static_cast<std::uint32_t>(oustTime);
    m_data->m_nAllianceDbId = allianceDatabaseId;
    SetAllianceByDbId(static_cast<unsigned int>(allianceDatabaseId));
    SetCastleByDbId(static_cast<unsigned int>(castleDatabaseId));
    SetAgitByDbId(static_cast<unsigned int>(agitDatabaseId));
    SetFortressByDbId(static_cast<unsigned int>(fortressDatabaseId));
    m_data->m_nRank = rank;
    m_data->m_nStatus = status;
    m_data->m_nPrivateFlag = static_cast<std::uint32_t>(privateFlag);
    m_data->m_nCrestId = static_cast<std::uint32_t>(crestId);
    m_data->m_nEmblemId = static_cast<std::uint32_t>(emblemId);
    m_data->m_CastleSiegeDefenceCount = static_cast<std::uint32_t>(castleSiegeDefenceCount);
    m_data->m_bIsGuilty = guilty != 0;
    m_data->m_nDismissReservedTime = static_cast<std::uint32_t>(dismissReservedTime);
    SetPledgeLevel(static_cast<unsigned int>(pledgeLevel), false);
    m_nAllianceOustedTime = static_cast<std::uint32_t>(allianceOustedTime);
    m_nAllianceWithdrawTime = static_cast<std::uint32_t>(allianceWithdrawTime);
    m_nAllianceDismissTime = static_cast<std::uint32_t>(allianceDismissTime);
    m_data->m_nMasteryResetTime = static_cast<std::uint32_t>(masteryResetTime);

    for (std::int32_t index = 0; index < powerGradeCount; ++index) {
        std::int32_t gradeId;
        std::int32_t power;
        cursor = ::Disassemble(cursor, "dd", &gradeId, &power);
        SetPowerGrade(gradeId, static_cast<unsigned long>(static_cast<std::uint32_t>(power)));
    }

    std::int32_t previousServer;
    std::array<char16_t, 32> originPledgeName{};
    cursor = ::Disassemble(cursor, "dS", &previousServer, 0x32, originPledgeName.data());
    if (g_bMergedServer) {
        m_data->m_nPrevServer = static_cast<std::uint32_t>(previousServer);
    }
    std::wstring hostOriginName = Utf16ToWide(originPledgeName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    CopyTargetPledgeName(m_data->m_sOriginPledgeName, hostOriginName.c_str());

    std::int32_t currentAccessPoint = 0;
    std::int32_t maximumAccessPoint = 0;
    std::int32_t accessLastSaveTime = 0;
    std::int32_t accessRewardLevel = 0;
    std::int32_t currentHuntPoint = 0;
    std::int32_t maximumHuntPoint = 0;
    std::int32_t huntLastSaveTime = 0;
    std::int32_t huntRewardLevel = 0;
    ::Disassemble(
        cursor, "dddddddd", &currentAccessPoint, &maximumAccessPoint, &accessLastSaveTime, &accessRewardLevel,
        &currentHuntPoint, &maximumHuntPoint, &huntLastSaveTime, &huntRewardLevel
    );

    PledgeBonusInfo& access = m_PledgeBonus.m_PledgeBonus[0];
    access.m_nCurrentBonusPoint = currentAccessPoint;
    access.m_nMaxBonusPoint =
        maximumAccessPoint < 1 ? CPledgeBonusSetting::Inst().GetGoalRate(0, pledgeLevel) : maximumAccessPoint;
    access.m_nLastSaveTime = accessLastSaveTime;
    access.m_nTodayRewardLev = accessRewardLevel;

    PledgeBonusInfo& hunt = m_PledgeBonus.m_PledgeBonus[1];
    hunt.m_nCurrentBonusPoint = currentHuntPoint;
    hunt.m_nMaxBonusPoint =
        maximumHuntPoint < 1 ? CPledgeBonusSetting::Inst().GetGoalRate(1, pledgeLevel) : maximumHuntPoint;
    hunt.m_nLastSaveTime = huntLastSaveTime;
    hunt.m_nTodayRewardLev = huntRewardLevel;

    return packet;
}

void CPledge::SetRemainedSubPledgeTypeToLoad(std::uint32_t remainedBits) {
    // Current L2Server 0x00975350 keeps only the low eight source bits.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_remainedSubPledgeTypeBitsLock);
    std::uint32_t selected = 0;
    for (std::uint32_t index = 0; remainedBits != 0 && index < 8; ++index) {
        if ((remainedBits & 1U) != 0) {
            selected |= std::uint32_t{1} << index;
        }
        remainedBits >>= 1U;
    }
    m_remainedSubPledgeTypeBits |= selected;
}

void CPledge::SetPrevWarPointByLoading(const int enemyPledgeSid, const int previousWarPoint) {
    // Current L2Server 0x00979e74.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    const auto found = m_mapDeclare.find(enemyPledgeSid);
    if (found != m_mapDeclare.end()) {
        found->second.m_nWarPointPrev = previousWarPoint;
    }
}

void CPledge::DeclareWar(CPledge* const enemyPledge, const int declaredTime) {
    // Current L2Server 0x009808ec.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    const int enemyPledgeSid = static_cast<int>(enemyPledge->Id());
    if (declaredTime > 0) {
        m_LastWarDeclaredTime = declaredTime;
    }
    const bool bothDeclared = m_setTargeted.find(enemyPledgeSid) != m_setTargeted.end();
    const int warStartTime = bothDeclared ? declaredTime : 0;
    ++m_nDeclareCount;

    const auto found = m_mapDeclare.find(enemyPledgeSid);
    if (found == m_mapDeclare.end()) {
        PledgeWarInfo war{};
        war.m_nEnemyPledgeSId = static_cast<std::uint32_t>(enemyPledgeSid);
        war.m_bBothDeclared = bothDeclared;
        war.m_nWarStartTime = warStartTime;
        war.m_nDeclaredTime = declaredTime;
        war.m_nWarPoint = 0;
        war.m_nWarPointPrev = 0;
        const auto inserted = m_mapDeclare.emplace(enemyPledgeSid, war);
        if (!inserted.second) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d],[PledgeWar], CPledge::DeclareWar, "
            //     L"insert failed, %u, %s",
            //     L"Pledge.cpp",
            //     0x128d,
            //     enemyPledgeSid,
            //     enemyPledge->GetPledgeName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d],[PledgeWar], CPledge::DeclareWar, "
            //     L"insert failed, %u, %s",
            //     L"Pledge.cpp",
            //     0x128d,
            //     enemyPledgeSid,
            //     enemyPledge->GetPledgeName())
            L2_LOG_ERROR(
                Log,
                "Could not insert war declaration against clan '%s' (object %u); the declaration counter was already "
                "incremented.",
                Utf16ToUtf8(enemyPledge->GetPledgeName()).c_str(), static_cast<unsigned>(enemyPledgeSid)
            );
        }
    } else {
        PledgeWarInfo& war = found->second;
        war.m_bBothDeclared = bothDeclared;
        war.m_nDeclaredTime = declaredTime;
        war.m_nWarStartTime = warStartTime;
        war.m_nWarPoint = 0;
        war.m_nWarPointPrev = 0;
    }

    if (bothDeclared) {
        enemyPledge->_SetPledgeWarStartTime(static_cast<int>(Id()), declaredTime);
    }
}

void CPledge::CheckDeclareWarBoth(CPledge* const enemyPledge) {
    // Current L2Server 0x0097d008.
    L2_GUARD_FUNCTION();
    if (enemyPledge == nullptr) {
        return;
    }

    L2_GUARD(*m_lock);
    const int enemyPledgeSid = static_cast<int>(enemyPledge->Id());
    const auto targeted = m_setTargeted.find(enemyPledgeSid);
    if (targeted == m_setTargeted.end()) {
        return;
    }

    const int targetedCount = ++targeted->second;
    if (targetedCount < 5) {
        ::SystemMessage_S_d<NoCheck, User> service(
            SystemMessageId::CLAN_KILL_COUNT_BEFORE_AUTOMATIC_WAR_DECLARATION, EST_STRING,
            std::u16string(enemyPledge->GetPledgeName()).c_str(), 5 - targetedCount
        );
        _ServeForAllMember(service);
        db.RequestUpdatePledgeWarKillCount(*this, static_cast<int>(enemyPledge->m_data->m_nDbId), targetedCount);
        return;
    }

    if (CanDeclareWarBoth(CSPointer<CPledge>(enemyPledge, ObjectRefType::FunctionChain))) {
        int transaction = 2;
        if (BeginTransaction(&transaction)) {
            DxDeclareWar packet(
                static_cast<int>(Id()), static_cast<int>(m_data->m_nDbId), enemyPledgeSid,
                static_cast<int>(enemyPledge->m_data->m_nDbId), 0, runtime::crt::Time32Now()
            );
            db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x1f), static_cast<int>(sizeof(packet)), packet);
        }
    } else if (static_cast<int>(Id()) != enemyPledgeSid) {
        static_cast<void>(DidDeclareWar(enemyPledgeSid));
        static_cast<void>(enemyPledge->DidDeclareWar(static_cast<int>(Id())));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]ttp 64107 My[%s][%d] Target[%s][%d]",
        //     L"Pledge.cpp",
        //     0x1108,
        //     GetPledgeName(),
        //     declaredTarget,
        //     enemyPledge->GetPledgeName(),
        //     targetDeclaredMe)
        // Noise: The automatic mutual-war transition is ineligible when a declaration already exists or the opposing
        // declaration is absent, the original counter rollback remains. L2_LOG_ERROR(Log,
        //     L"[%s][%d]ttp 64107 My[%s][%d] Target[%s][%d]",
        //     L"Pledge.cpp",
        //     0x1108,
        //     GetPledgeName(),
        //     declaredTarget,
        //     enemyPledge->GetPledgeName(),
        //     targetDeclaredMe)
    }
    --targeted->second;
}

bool CPledge::CancelWar(CPledge* const enemyPledge, const int cancelTime) {
    // Current L2Server 0x009823e8.
    L2_GUARD(*m_lock);
    --m_nDeclareCount;
    if (cancelTime > 0) {
        m_LastWarDeclaredTime = cancelTime;
    }

    const int enemyPledgeSid = static_cast<int>(enemyPledge->Id());
    const auto targeted = m_setTargeted.find(enemyPledgeSid);
    if (targeted != m_setTargeted.end()) {
        targeted->second = 0;
    }
    m_mapDeclare.erase(enemyPledgeSid);
    return true;
}

void CPledge::_MakeDetailList(CPledge* const enemyPledge, const PledgeWarInfo war, WarDetailForView& detail) {
    // Current L2Server 0x00979b80.
    L2_GUARD_FUNCTION();
    if (enemyPledge == nullptr) {
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t referenceTime = war.m_bBothDeclared ? war.m_nWarStartTime : war.m_nDeclaredTime;
    const std::int32_t elapsedTime =
        runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(referenceTime, now), BP::_PLEDGE_WAR_CANCEL_TIME);

    detail.sPledgeName = enemyPledge->GetPledgeName();
    detail.nWarType = war.m_bBothDeclared ? PLEDGE_BOTH_DECLARE : PLEDGE_DECLARE;
    detail.nElapsedTime = elapsedTime;
    detail.nWarPoint = war.m_nWarPoint;
    detail.nWarPointDiff = runtime::SubtractInt32Wrapping(war.m_nWarPoint, war.m_nWarPointPrev);
    detail.nLeftKillCount = 0;

    L2_GUARD(*enemyPledge->m_lock);
    const auto targeted = enemyPledge->m_setTargeted.find(static_cast<int>(Id()));
    if (targeted != enemyPledge->m_setTargeted.end()) {
        detail.nLeftKillCount = targeted->second;
    }
}

bool CPledge::_MakeDetailTargetList(CPledge* const targetPledge, WarDetailForView& detail, int leftKillCount) {
    // Current L2Server 0x00979d18.
    L2_GUARD_FUNCTION();
    if (leftKillCount < 0) {
        leftKillCount = 0;
    }

    const auto found = m_mapDeclare.find(static_cast<int>(targetPledge->Id()));
    if (found == m_mapDeclare.end()) {
        return false;
    }
    const PledgeWarInfo war = found->second;
    if (war.m_bBothDeclared) {
        return false;
    }

    detail.sPledgeName = GetPledgeName();
    detail.nWarType = PLEDGE_TARGETED;
    detail.nElapsedTime = runtime::AddInt32Wrapping(
        runtime::SubtractInt32Wrapping(war.m_nDeclaredTime, runtime::crt::Time32Now()), BP::_PLEDGE_WAR_CANCEL_TIME
    );
    detail.nWarPoint = war.m_nWarPoint;
    detail.nWarPointDiff = runtime::SubtractInt32Wrapping(war.m_nWarPoint, war.m_nWarPointPrev);
    detail.nLeftKillCount = leftKillCount;
    return true;
}

void CPledge::GetDetailWarList(WarListType& pledges) {
    // Current L2Server 0x009862f8.
    pledges.clear();
    L2_GUARD(*m_lock);
    for (const auto& declaration : m_mapDeclare) {
        const PledgeWarInfo war = declaration.second;
        CPledge* const enemyPledge = GetObjectW(war.m_nEnemyPledgeSId);
        if (enemyPledge == nullptr) {
            continue;
        }

        WarDetailForView detail;
        _MakeDetailList(enemyPledge, war, detail);
        pledges.push_back(std::make_pair(static_cast<int>(enemyPledge->Id()), detail));
    }
}

void CPledge::GetDetailTargetedList(WarListType& pledges) {
    // Current L2Server 0x00986488 appends to the declaration list.
    L2_GUARD(*m_lock);
    for (const auto& targeted : m_setTargeted) {
        CPledge* const enemyPledge = GetObjectW(static_cast<std::uint32_t>(targeted.first));
        if (enemyPledge == nullptr) {
            continue;
        }

        WarDetailForView detail;
        if (enemyPledge->_MakeDetailTargetList(this, detail, targeted.second)) {
            pledges.push_back(std::make_pair(static_cast<int>(enemyPledge->Id()), detail));
        }
    }
}

void CPledge::SendViewPledgeWarList(CUserSocket* const socket, unsigned int page, const int listCondition) {
    // Current L2Server 0x009865c0.
    L2_GUARD_FUNCTION();
    static_cast<void>(listCondition);
    if (socket == nullptr) {
        return;
    }

    WarListType warList;
    GetDetailWarList(warList);
    GetDetailTargetedList(warList);

    const std::uint32_t warCount = static_cast<std::uint32_t>(warList.size());
    std::uint32_t pageCount = warCount / 500U;
    if (warCount != 0 && pageCount * 500U < warCount) {
        ++pageCount;
    }
    if (pageCount < page) {
        page = 0;
    }

    CPacketHelperEx<20480> packet;
    int emittedCount = 0;
    std::uint32_t index = page * 500U;
    const std::uint32_t endIndex = index + 500U;
    if (index < endIndex) {
        do {
            if (warCount <= index) {
                break;
            }
            const WarDetailForView& detail = warList[static_cast<std::size_t>(index)].second;
            const std::u16string pledgeName = std::u16string(detail.sPledgeName);
            packet.Assemble(
                "Sddddd", pledgeName.c_str(), detail.nWarType, detail.nElapsedTime, detail.nWarPoint, detail.nWarPointDiff,
                detail.nLeftKillCount
            );
            ++emittedCount;
            ++index;
        } while (index < endIndex);
    }

    socket->Send("chddb", 0xfe, 0x40, page, emittedCount, packet.GetLength(), packet.GetPacket());
}

void CPledge::SendWarReport(CUserSocket* const socket) {
    // Current L2Server 0x00989248.
    L2_GUARD_FUNCTION();
    CHtmlGenerator generator(u"war_report.htm");

    std::u16string killCount = FormatString(u"%d", m_nSiegeKillCount);
    generator.AddValue(u"kill_count", killCount);

    std::u16string deathCount = FormatString(u"%d", m_nSiegeDeathCount);
    generator.AddValue(u"death_count", deathCount);

    WarReport report(&generator);
    ServeForAllMember(report, true);

    CNpcHtmlSender sender(socket);
    std::u16string html = generator.Generate();
    sender.SendHtml(html);
}

void CPledge::GetBothDeclareList(std::vector<CPledge*>& pledges) {
    // Current L2Server 0x00984e5c.
    pledges.clear();
    L2_GUARD(*m_lock);
    for (const auto& declaration : m_mapDeclare) {
        if (!declaration.second.m_bBothDeclared) {
            continue;
        }
        CPledge* const pledge = GetObjectW(declaration.second.m_nEnemyPledgeSId);
        if (pledge != nullptr) {
            pledges.push_back(pledge);
        }
    }
}

void CPledge::GetTargetedList(std::vector<CPledge*>& pledges) {
    // Current L2Server 0x00984f7c.
    pledges.clear();
    L2_GUARD(*m_lock);
    for (const auto& targeted : m_setTargeted) {
        CPledge* const pledge = GetObjectW(static_cast<std::uint32_t>(targeted.first));
        if (pledge != nullptr) {
            pledges.push_back(pledge);
        }
    }
}

void CPledge::CancelWarWhoTargetedMe() {
    // Current L2Server 0x009861bc.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> targeted;
    std::vector<CPledge*> bothDeclared;
    GetTargetedList(targeted);
    GetBothDeclareList(bothDeclared);
    for (CPledge* const pledge : targeted) {
        pledge->CancelWar(this, 0);
        TargetedBy(pledge, 0, false);
    }
}

void CPledge::TargetedBy(CPledge* const enemyPledge, const int targetedCount, const bool bothDeclared) {
    // Current L2Server 0x0097d2dc.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    const int enemyPledgeSid = static_cast<int>(enemyPledge->Id());
    if (bothDeclared) {
        m_setTargeted.emplace(enemyPledgeSid, targetedCount);
    } else {
        const auto targeted = m_setTargeted.find(enemyPledgeSid);
        if (targeted != m_setTargeted.end()) {
            m_setTargeted.erase(targeted);
        }
    }

    const auto found = m_mapDeclare.find(enemyPledgeSid);
    if (found == m_mapDeclare.end()) {
        return;
    }

    PledgeWarInfo& war = found->second;
    war.m_bBothDeclared = bothDeclared;
    if (!bothDeclared) {
        const std::int32_t resetTime = runtime::crt::Time32Now();
        war.m_nDeclaredTime = resetTime;
        war.m_nWarStartTime = 0;
        war.m_nWarPoint = 0;
        war.m_nWarPointPrev = 0;
        db.ResetWarTargeted(*this, static_cast<int>(enemyPledge->m_data->m_nDbId), resetTime);
    }
}

int CPledge::_GetPledgeWarPoint(const int enemyPledgeSid) {
    // Current L2Server 0x0097924c. The caller owns synchronization.
    L2_GUARD_FUNCTION();
    const auto found = m_mapDeclare.find(enemyPledgeSid);
    if (found == m_mapDeclare.end() || !found->second.m_bBothDeclared) {
        return 0;
    }
    return found->second.m_nWarPoint;
}

int CPledge::GetPledgeWarPoint(const int enemyPledgeSid) {
    // Current L2Server 0x0097c2e4.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    return _GetPledgeWarPoint(enemyPledgeSid);
}

void CPledge::UpdatePledgeWarPoint(
    const int enemyPledgeSid, const int enemyPledgeDatabaseId, const int warPointDelta, const bool save
) {
    // Current L2Server 0x00978fac.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    int warPoint = 0;
    const auto found = m_mapDeclare.find(enemyPledgeSid);
    if (found != m_mapDeclare.end()) {
        found->second.m_nWarPoint = runtime::AddInt32Wrapping(found->second.m_nWarPoint, warPointDelta);
        warPoint = found->second.m_nWarPoint;
    }
    db.SavePledgeWarPoint(*this, enemyPledgeDatabaseId, warPoint);

    if (save) {
        return;
    }
    CPledge* const enemyPledge = GetObjectW(static_cast<std::uint32_t>(enemyPledgeSid));
    if (enemyPledge == nullptr) {
        return;
    }

    const std::u16string pledgeName = GetPledgeName();
    const std::u16string enemyPledgeName = enemyPledge->GetPledgeName();
    CLogFormatter formatter;
    formatter.WriteLog(
        0x7f2, m_data->m_nDbId, 0, enemyPledgeDatabaseId, 0, 0, 0, 0, pledgeName.c_str(), enemyPledgeName.c_str(), nullptr,
        m_data->m_nSkillLevel, GetDbMemberCount(), m_nDeclareCount, static_cast<std::int64_t>(m_setTargeted.size()),
        warPointDelta, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
    );
}

void CPledge::_SetPledgeWarStartTime(const int enemyPledgeSid, const int startTime) {
    // Current L2Server 0x00979f8c. The caller owns synchronization.
    L2_GUARD_FUNCTION();
    const auto found = m_mapDeclare.find(enemyPledgeSid);
    if (found != m_mapDeclare.end()) {
        found->second.m_nWarStartTime = startTime;
    }
}

void CPledge::SetPrevWarPoint() {
    // Current L2Server 0x00975030.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    for (auto& entry : m_mapDeclare) {
        PledgeWarInfo& war = entry.second;
        war.m_nWarPointPrev = war.m_nWarPoint;
        CPledge* const enemy = GetObjectW(war.m_nEnemyPledgeSId);
        if (enemy != nullptr) {
            db.SavePledgeWarPointPrev(*this, static_cast<int>(enemy->GetDbId()), war.m_nWarPoint);
        }
    }
}

void CPledge::CheckDeclareTime() {
    // Current L2Server 0x00975168.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    for (const auto& entry : m_mapDeclare) {
        const PledgeWarInfo war = entry.second;
        if (war.m_bBothDeclared || war.m_nDeclaredTime + BP::_PLEDGE_WAR_CANCEL_TIME > runtime::crt::Time32Now()) {
            continue;
        }

        const std::int32_t cancelTime = runtime::crt::Time32Now();
        CPledge* const enemy = GetObjectW(war.m_nEnemyPledgeSId);
        if (enemy == nullptr) {
            continue;
        }
        int transaction = 3;
        if (!BeginTransaction(&transaction)) {
            continue;
        }

        DxCancelWar packet(
            static_cast<int>(m_nId), static_cast<int>(m_data->m_nDbId), static_cast<int>(war.m_nEnemyPledgeSId),
            static_cast<int>(enemy->m_data->m_nDbId), 0, cancelTime, false, 0
        );
        db.RequestTPacketProcess(m_nWorldId, 0x20, static_cast<int>(sizeof(packet)), packet);
    }
}

void CPledge::TimerExpired(const int id) {
    // Current L2Server 0x0097e69c.
    if (id == 1) {
        GuardAddTimer rearm(this, BP::_PLEDGE_HEARTBEAT_FOR_EXPIRATION, 1);
        std::vector<int> expiredSkillIds;
        const std::int32_t now = runtime::crt::Time32Now();
        m_pSkill->SelectExpiredSkillIDs(now, expiredSkillIds);
        for (const int skillId : expiredSkillIds) {
            if (RemovePledgeSkill(skillId, true)) {
                db.RequestDelPledgeSkill(*this, skillId);
                PledgeStuff::Write_LOG_PLEDGE_SKILL_REMOVE(this, skillId);
            }
        }

        std::vector<int> expiredItemIds;
        m_pItem->SelectExpiredItemIDs(runtime::crt::Time32Now(), expiredItemIds);
        for (const int itemClassId : expiredItemIds) {
            if (m_pItem->RemoveItem(itemClassId)) {
                PledgeItemStuff::RequestRemovePledgeItem(this, itemClassId);
                PledgeStuff::Write_LOG_PLEDGE_ITEM_REMOVE(this, itemClassId);
            }
        }
        return;
    }

    if (m_nStatTick < 10) {
        ++m_nStatTick;
    } else {
        m_nStatTick = 0;
        CheckDeclareTime();
    }
    const std::uint32_t tick = runtime::GetTickCount();
    if (3600000U < tick - static_cast<std::uint32_t>(m_nPrevPointTick)) {
        m_nPrevPointTick = static_cast<std::int32_t>(runtime::GetTickCount());
        SetPrevWarPoint();
    }
    AddTimer(100000, 0);
}

void CPledge::ReserveLoaded(const int worldId, const int pledgeDatabaseId, const PledgeRequestLoadType) {
    // Current L2Server 0x0097c944. The presence check uses
    // the incoming CacheD request world, while the reserved key keeps the
    // explicit world carried by the caller.
    if (s_pledgeDBMap.ExistKey(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), pledgeDatabaseId))) {
        return;
    }

    L2_WRITE_GUARD(s_lockSet);
    s_setPledgeToBeLoaded.insert(std::CDualKey<int, std::uint32_t>(worldId, static_cast<std::uint32_t>(pledgeDatabaseId)));
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] ReserveLoaded nWorldId[%d] PledgeDBID[%d][type:%d]",
    //     worldId,
    //     pledgeDatabaseId,
    //     static_cast<int>(type))
    // Noise: Recording each clan in the startup reservation set is routine tracing. Readiness and unresolved-clan
    // diagnostics remain. L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] ReserveLoaded nWorldId[%d] PledgeDBID[%d][type:%d]",
    //     worldId,
    //     pledgeDatabaseId,
    //     static_cast<int>(type))
}

void CPledge::ReserveWarDeclare(
    const std::uint32_t pledgeSid,
    const std::uint32_t enemyPledgeSid,
    const std::uint32_t declaredTime,
    const std::uint32_t warPoint,
    const std::uint32_t previousWarPoint,
    const std::uint32_t deathCount
) {
    // Current L2Server 0x00983b9c.
    L2_WRITE_GUARD(s_lockList);
    s_lstChallengerDbId.push_back(pledgeSid);
    s_lstChallengeeDbId.push_back(enemyPledgeSid);
    s_lstDeclareTime.push_back(declaredTime);
    s_lstPoint.push_back(warPoint);
    s_lstPointPrev.push_back(previousWarPoint);
    s_lstDeathCount.push_back(deathCount);
}

void CPledge::SetAllDeclaredWarToPledges() {
    // Current L2Server 0x00981a64.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lockList);

    std::size_t index = 0;
    for (; index < s_lstChallengerDbId.size(); ++index) {
        CSPointer<CPledge> challenger =
            FindPledge(g_nWorldId, static_cast<int>(runtime::CheckedVectorAt(s_lstChallengerDbId, index)));
        CSPointer<CPledge> challengee =
            FindPledge(g_nWorldId, static_cast<int>(runtime::CheckedVectorAt(s_lstChallengeeDbId, index)));
        if (!challenger) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DeclareWar]Challenger is not valid. dbid[%d]",
            //     s_lstChallengerDbId[index])
            // L2_LOG_ERROR(Log,
            //     L"[DeclareWar]Challenger is not valid. dbid[%d]",
            //     s_lstChallengerDbId[index])
            L2_LOG_ERROR(
                Log,
                "Cannot apply stored clan-war row %llu: challenger clan %u is unavailable in world %d; this row is skipped.",
                static_cast<unsigned long long>(index),
                static_cast<unsigned>(runtime::CheckedVectorAt(s_lstChallengerDbId, index)), g_nWorldId
            );
            continue;
        }
        if (!challengee) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DeclareWar]Challengee is not valid. dbid[%d]",
            //     s_lstChallengeeDbId[index])
            // L2_LOG_ERROR(Log,
            //     L"[DeclareWar]Challengee is not valid. dbid[%d]",
            //     s_lstChallengeeDbId[index])
            L2_LOG_ERROR(
                Log,
                "Cannot apply stored clan-war row %llu: target clan %u is unavailable in world %d; this row is skipped.",
                static_cast<unsigned long long>(index),
                static_cast<unsigned>(runtime::CheckedVectorAt(s_lstChallengeeDbId, index)), g_nWorldId
            );
            continue;
        }

        challenger->DeclareWar(challengee.get(), static_cast<int>(runtime::CheckedVectorAt(s_lstDeclareTime, index)));
        challengee->TargetedBy(challenger.get(), static_cast<int>(runtime::CheckedVectorAt(s_lstDeathCount, index)), true);
        challenger->UpdatePledgeWarPoint(
            static_cast<int>(challengee->Id()), static_cast<int>(challengee->m_data->m_nDbId),
            static_cast<int>(runtime::CheckedVectorAt(s_lstPoint, index)), true
        );
        challenger->SetPrevWarPointByLoading(
            static_cast<int>(challengee->Id()), static_cast<int>(runtime::CheckedVectorAt(s_lstPointPrev, index))
        );
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR],[PledgeWar], CPledge::SetAllDeclaredWarToPledges, "
    //     L"loaded pledge war, LoadWar [%d]",
    //     index)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR],[PledgeWar], CPledge::SetAllDeclaredWarToPledges, "
    //     L"loaded pledge war, LoadWar [%d]",
    //     index)
    L2_LOG_INFO(
        Log, "Stored clan-war application pass finished: %llu rows examined, including any skipped rows.",
        static_cast<unsigned long long>(index)
    );
}

void CPledge::SetFlagReservePledge(const PledgeRequestLoadType type) {
    // Current L2Server 0x00971bf8. The shipping implementation uses the
    // MSVC dword carrier of std::bitset<8>, throws for an invalid position,
    // and signals only when all eight reservation classes are present.
    L2_WRITE_GUARD(s_lockSet);

    const auto position = static_cast<std::uint32_t>(type);
    if (position >= static_cast<std::uint32_t>(PRLT_MAX)) {
        throw std::out_of_range("invalid bitset<N> position");
    }

    s_ReserveBitset |= std::uint32_t{1} << position;
    std::uint32_t bits = s_ReserveBitset & 0xffU;
    int count = 0;
    while (bits != 0) {
        count += static_cast<int>(bits & 1U);
        bits >>= 1U;
    }
    if (count == static_cast<int>(PRLT_MAX)) {
        g_JobFinishSignal.SetResevedFinish();
    }
}

void CPledge::LoadPledge(
    wchar_t* const pledgeName,
    const int pledgeDatabaseId,
    const int masterDatabaseId,
    wchar_t* const masterName,
    const int memberCount,
    const unsigned char* const packet
) {
    // Current L2Server 0x009898d4.
    L2_GUARD_FUNCTION();
    const int worldId = runtime::process::CurrentDatabaseId();
    const std::CDualKey<int, int> pledgeKey(worldId, pledgeDatabaseId);
    const bool worldServer = g_L2ServerType == L2ServerType::WORLDSERVER;

    if (worldServer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][LoadPledge] 1.Load Pledge begin![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
        // Noise: Per-clan construction-stage tracing is routine startup noise and does not establish complete clan-data
        // loading. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][LoadPledge] 1.Load Pledge begin![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
    }
    if (s_pledgeDBMap.ExistKey(pledgeKey)) {
        return;
    }
    if (worldServer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][LoadPledge] 2.Load Pledge begin![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
        // Noise: Per-clan construction-stage tracing is routine startup noise and does not establish complete clan-data
        // loading. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][LoadPledge] 2.Load Pledge begin![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
    }
    if (s_pledgeDBMapTemp.ExistKey(pledgeKey)) {
        return;
    }
    if (worldServer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][LoadPledge] 3.New Pledge begin![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
        // Noise: Per-clan construction-stage tracing is routine startup noise and does not establish complete clan-data
        // loading. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][LoadPledge] 3.New Pledge begin![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
    }

    void* const allocation = CPledge::operator new(sizeof(CPledge));
    CPledge* const created = allocation != nullptr
        ? ::new (allocation) CPledge(pledgeDatabaseId, masterDatabaseId, pledgeName, masterName, worldId)
        : nullptr;
    CSPointer<CPledge> pledge(created, ObjectRefType::TemporaryUse);
    if (worldServer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][LoadPledge] 4.New Pledge End![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
        // Noise: Per-clan construction-stage tracing is routine startup noise and does not establish complete clan-data
        // loading. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][LoadPledge] 4.New Pledge End![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
    }
    if (!pledge) {
        return;
    }

    s_pledgeDBMapTemp.Insert(
        std::CDualKey<int, int>(pledge->m_nWorldId, static_cast<int>(pledge->m_data->m_nDbId)), pledge.get(),
        const_cast<char*>("Pledge.cpp"), 0x174b
    );
    L2_RELEASE(pledge, ObjectRefType::CreateDelete);
    if (worldServer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][LoadPledge] 5.Load Pledge End![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
        // Noise: Per-clan construction-stage tracing is routine startup noise and does not establish complete clan-data
        // loading. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][LoadPledge] 5.Load Pledge End![%d][%s][%d]",
        //     worldId,
        //     pledgeName,
        //     pledgeDatabaseId)
    }

    const unsigned char* cursor = pledge->AddMemberByDisassemble(MASTER_PLEDGE, memberCount, packet);
    std::int32_t academyMemberCount = 0;
    std::array<char16_t, 28> academyName{};
    cursor = ::Disassemble(cursor, "dS", &academyMemberCount, 0x32, academyName.data());
    if (academyName[0] != u'\0') {
        std::wstring hostAcademyName = Utf16ToWide(academyName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        pledge->SetSubPledge(ACADEMY, hostAcademyName.data(), 0, false);
    }
    cursor = pledge->AddAcademyMemberByDisassemble(academyMemberCount, cursor);

    std::int32_t remainedSubPledgeTypeBits = 0;
    std::int32_t skillCount = 0;
    std::int32_t subPledgeSkillCount = 0;
    cursor = ::Disassemble(cursor, "ddd", &remainedSubPledgeTypeBits, &skillCount, &subPledgeSkillCount);
    pledge->SetProperty(cursor);
    if (skillCount != 0 || subPledgeSkillCount != 0) {
        pledge->m_bSkillLoading = true;
        db.RequestLoadPledgeSkill(*pledge.get());
    }
    pledge->SetRemainedSubPledgeTypeToLoad(static_cast<std::uint32_t>(remainedSubPledgeTypeBits));
    pledge->RequestLoadSubPledge();
    db.RequestWaitingTransferPledgeMaster(*pledge.get());
    CAirShipManager::Inst().OnLoadPledge(pledge.get());
    pledge->m_pMastery->LoadFromDB();
    PledgeItemStuff::RequestLoadPledgeItem(pledge.get());
    if (pledge->HasLoadedAllData()) {
        pledge->LoadingCompleted();
    }
    pledge->AddTimer(100000, 0);
    if (pledge->m_nWorldId == g_nWorldId) {
        pledge->AddTimer(BP::_PLEDGE_HEARTBEAT_FOR_EXPIRATION, 1);
    }
}

void CPledge::LoadSubPledge(const unsigned char* packet) {
    // Current L2Server 0x00989d40.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId;
    std::int32_t subPledgeCount;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &pledgeDatabaseId, &subPledgeCount);
    CSPointer<CPledge> pledge =
        s_pledgeDBMapTemp.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), pledgeDatabaseId));
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Invalide subpledge Loading pledgeDBID[%d] "
        //     "subpledgeCount[%d]",
        //     pledgeDatabaseId,
        //     subPledgeCount)
        // L2_LOG_ERROR(Log,
        //     "Invalide subpledge Loading pledgeDBID[%d] "
        //     "subpledgeCount[%d]",
        //     pledgeDatabaseId,
        //     subPledgeCount)
        L2_LOG_ERROR(
            Log, "Cannot load %d subclan records for clan %u: the temporary clan object is unavailable.", subPledgeCount,
            static_cast<unsigned>(pledgeDatabaseId)
        );
        return;
    }

    for (std::int32_t index = 0; index < subPledgeCount; ++index) {
        std::int32_t pledgeTypeValue;
        std::array<char16_t, 28> subPledgeName;
        std::int32_t masterDatabaseId;
        std::int32_t memberCountUpgrade;
        std::int32_t memberCount;
        cursor = ::Disassemble(
            cursor, "dSddd", &pledgeTypeValue, 0x32, subPledgeName.data(), &masterDatabaseId, &memberCountUpgrade,
            &memberCount
        );

        std::wstring hostSubPledgeName = Utf16ToWide(subPledgeName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        const PledgeType pledgeType = static_cast<PledgeType>(pledgeTypeValue);
        pledge->SetSubPledge(
            pledgeType, hostSubPledgeName.data(), static_cast<std::uint32_t>(masterDatabaseId), memberCountUpgrade != 0
        );
        cursor = pledge->AddMemberByDisassemble(pledgeType, memberCount, cursor);
        pledge->SubPledgeLoadingCompleted(pledgeType);
    }

    if (pledge->HasMoreSubPledgeToLoad()) {
        pledge->RequestLoadSubPledge();
    } else if (pledge->HasLoadedAllData()) {
        pledge->LoadingCompleted();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] ::LoadSubPledge LoadPledge PledgeDBID[%d]",
        //     pledgeDatabaseId)
        // Noise: Subclan data is loaded while other asynchronous clan data is still pending. This is a normal startup state.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] ::LoadSubPledge LoadPledge PledgeDBID[%d]",
        //     pledgeDatabaseId)
    }
}

void CPledge::LoadPledgeSkill(const unsigned char* packet) {
    // Current L2Server 0x0098a0bc.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId;
    std::int32_t skillCount;
    std::int32_t subPledgeSkillCount;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &pledgeDatabaseId, &skillCount, &subPledgeSkillCount);
    CSPointer<CPledge> pledge =
        s_pledgeDBMapTemp.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), pledgeDatabaseId));
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%S][%d] Cannot Load Pledge Skill - unknown pledge[%d], "
        //     "skill count[%d]",
        //     L"Pledge.cpp",
        //     0x192a,
        //     pledgeDatabaseId,
        //     skillCount)
        // L2_LOG_ERROR(Log,
        //     "[%S][%d] Cannot Load Pledge Skill - unknown pledge[%d], "
        //     "skill count[%d]",
        //     L"Pledge.cpp",
        //     0x192a,
        //     pledgeDatabaseId,
        //     skillCount)
        L2_LOG_ERROR(
            Log, "Cannot load skills for clan %u: its temporary object is unavailable (clan skills %d, subclan skills %d).",
            static_cast<unsigned>(pledgeDatabaseId), skillCount, subPledgeSkillCount
        );
        return;
    }

    for (std::int32_t index = 0; index < skillCount; ++index) {
        std::int32_t skillId;
        std::int32_t skillLevel;
        cursor = ::Disassemble(cursor, "dd", &skillId, &skillLevel);
        std::int32_t expiryTime = 0;
        cursor = ::Disassemble(cursor, "d", &expiryTime);
        pledge->AddPledgeSkillWithNotify(static_cast<unsigned int>(skillId), skillLevel, expiryTime);
    }

    for (std::int32_t index = 0; index < subPledgeSkillCount; ++index) {
        std::int32_t ignoredSkillId;
        std::int32_t ignoredSkillLevel;
        std::int32_t ignoredPledgeType;
        cursor = ::Disassemble(cursor, "ddd", &ignoredSkillId, &ignoredSkillLevel, &ignoredPledgeType);
        static_cast<void>(ignoredSkillId);
        static_cast<void>(ignoredSkillLevel);
        static_cast<void>(ignoredPledgeType);
    }
    static_cast<void>(cursor);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] ::LoadPledgeSkill LoadPledge PledgeDBID[%d]",
    //     pledgeDatabaseId)
    // Noise: Routine per-clan skill-load trace. Readiness is handled by the following existing state update.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] ::LoadPledgeSkill LoadPledge PledgeDBID[%d]",
    //     pledgeDatabaseId)
    pledge->m_bSkillLoading = false;
    if (pledge->HasLoadedAllData()) {
        pledge->LoadingCompleted();
    }
}

void CPledge::SendDB(const char* format, ...) const {
    L2_GUARD_FUNCTION();
    const int worldId = CThreadManager::GetThreadID() < 16 ? m_nWorldId : g_nWorldId;
    runtime::process::SetCurrentWorldId(worldId);
    CDB::CWorldSetter worldSetter;

    va_list arguments;
    va_start(arguments, format);
    if (runtime::process::CurrentWorldId() < 1) {
        SystemMail::sendNotSetThreadLocalWorldID(mailServer);
    } else {
        db.m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

L2_DEFINE_OBJECT_CLASS_ID(CPledgeTimer)

L2_DEFINE_OBJECT_CLASS_NAME(CPledgeTimer, m_pClassName_CPledgeTimer)

L2_DEFINE_OBJECT_ALLOCATOR(CPledgeTimer, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPledgeTimer)

CPledgeTimer::CPledgeTimer() :
    m_nCheckCount(0),
    m_bNpcServerAlive{false, false},
    m_bAllNpcLoaded(false),
    m_bAllMakerFinished(false),
    m_tLastPong(0),
    m_alignment5cTo60{} {}

CPledgeTimer::~CPledgeTimer() = default;

L2_DEFINE_OBJECT_TYPED_RELEASE(CPledgeTimer)

void CPledgeTimer::TimerExpired(int) {
    L2_GUARD_FUNCTION();

    static constexpr std::int32_t kPingTimeout[NSAT_MAX] = {
        20,
        60,
    };
    static bool reported[NSAT_MAX] = {false, false};

    const std::int32_t now = runtime::crt::Time32Now();
    for (int index = 0; index < NSAT_MAX; ++index) {
        const std::uint32_t elapsed = static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(m_tLastPong);
        const bool alive = elapsed < static_cast<std::uint32_t>(kPingTimeout[index]);
        m_bNpcServerAlive[index] = alive;
        if (alive || m_tLastPong == 0) {
            reported[index] = false;
        } else if (!reported[index]) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] Did not receive PONG from NPCServer. "
            //     L"(now:%d, last-pong:%d, diff:%d)",
            //     now,
            //     m_tLastPong,
            //     static_cast<std::int32_t>(elapsed))
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] Did not receive PONG from NPCServer. "
            //     L"(now:%d, last-pong:%d, diff:%d)",
            //     now,
            //     m_tLastPong,
            //     static_cast<std::int32_t>(elapsed))
            L2_LOG_WARN(
                Log,
                "NPC-server PONG is overdue for the %d-second clan check: elapsed %u seconds, current timestamp %d, last "
                "PONG %d; the NPC server is marked unavailable for this check.",
                kPingTimeout[index], static_cast<unsigned>(elapsed), now, m_tLastPong
            );
            reported[index] = true;
        }
    }

    // Current L2Server 0x00980400 sends the castle-siege liveness ping
    // registered by L2NPC at opcode 0x28. Omitting the variadic argument
    // makes Assemble consume an indeterminate byte.
    g_npcserver.Send("c", 0x28);
    if (m_nCheckCount == (m_nCheckCount / 60) * 60) {
        {
            L2_READ_GUARD(m_mapLock);
            for (const auto& reservation : m_mapDismissReservedPledge) {
                const std::int32_t delta = runtime::Int32FromBits(static_cast<std::uint32_t>(now) - reservation.second);
                if (delta > 0) {
                    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(reservation.first));
                    if (pledge) {
                        db.RequestDeletePledgeByTimer(pledge.get());
                    }
                }
            }
        }
        g_DeferredJob.DoTimerJob();
    }

    ++m_nCheckCount;
    AddTimer(1000, 0);
}

void CPledgeTimer::ReserveDismiss(const std::uint32_t pledgeDatabaseId, const std::uint32_t deletionTime) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_mapLock);
        m_mapDismissReservedPledge[pledgeDatabaseId] = deletionTime;
    }
}

void CPledgeTimer::CancelDismiss(const std::uint32_t pledgeDatabaseId) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_mapLock);
        m_mapDismissReservedPledge.erase(pledgeDatabaseId);
    }
}

void CPledgeTimer::SetLastPongTimeAndState(const L2ServerLong lastPongTime, const int allNpcLoaded) {
    runtime::raw_atomic::StoreI32Relaxed(&m_tLastPong, static_cast<std::int32_t>(lastPongTime));
    runtime::raw_atomic::StoreBool(&m_bAllNpcLoaded, allNpcLoaded != 0);
}

bool CPledgeTimer::IsAllNpcRelatedLoaded() const {
    return IsNpcServerAlive(NSAT_20) && IsAllNpcLoaded() && IsAllMakerFinished();
}

bool CPledgeTimer::IsNpcServerAlive(const NPCServerAliveType type) const {
    return static_cast<std::uint32_t>(type) < NSAT_MAX &&
        runtime::raw_atomic::LoadBool(&m_bNpcServerAlive[static_cast<int>(type)]);
}

bool CPledgeTimer::IsAllNpcLoaded() const { return runtime::raw_atomic::LoadBool(&m_bAllNpcLoaded); }

bool CPledgeTimer::IsAllMakerFinished() const { return runtime::raw_atomic::LoadBool(&m_bAllMakerFinished); }

void CPledgeTimer::SetAllMakerFinished(const bool finished) {
    runtime::raw_atomic::StoreBool(&m_bAllMakerFinished, finished);
}

CObject* CPledge::Copy() { return nullptr; }

int CPledge::GetIndex() const {
    // Current target returns the shared-object index at +0x28.
    return m_data->index;
}

CPledge* PledgeCapture::GetPledge() const {
    CPledge* pledge = CPledge::GetObjectW(m_sid);
    if (pledge != nullptr) {
        return pledge;
    }

    CSPointer<CPledge> found = CPledge::FindPledge(m_worldid, m_dbid);
    return found.get();
}

CPledge* CPledge::GetObjectW(std::uint32_t smartId) {
    L2_GUARD_FUNCTION();
    return s_pledgeSmartIdManager.GetObjectW(smartId);
}

int CPledge::GetDefaultPowerGrade(const PledgeType pledgeType) {
    // Current L2Server 0x0096c194.
    L2_GUARD_FUNCTION();
    switch (pledgeType) {
        case ACADEMY:
            return 9;
        case MASTER_PLEDGE:
            return 6;
        case SUB_1:
        case SUB_2:
            return 7;
        case SUB_1_1:
        case SUB_1_2:
        case SUB_2_1:
        case SUB_2_2:
            return 8;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] invalid pledge type [%d]",
            //     "Pledge.cpp",
            //     0x20be,
            //     static_cast<int>(pledgeType))
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] invalid pledge type [%d]",
            //     "Pledge.cpp",
            //     0x20be,
            //     static_cast<int>(pledgeType))
            L2_LOG_ERROR(
                Log, "Cannot choose a default clan privilege grade for subclan type %d; returning grade 0.",
                static_cast<int>(pledgeType)
            );
            return 0;
    }
}

CSPointer<CPledge> CPledge::FindPledge(int worldId, int pledgeDatabaseId) {
    return s_pledgeDBMap.FindObject(std::CDualKey<int, int>(worldId, pledgeDatabaseId));
}

CSPointer<CPledge> CPledge::FindPledge(const int worldId, SharedUtilsWideChar* const pledgeName) {
    // Current L2Server 0x0098b19c. The target lower-case buffer is exactly
    // 32 UTF-16 code units and its regular expression is a full match.
    std::array<SharedUtilsWideChar, 32> lowered{};
    ToLowerCase(pledgeName, lowered.data());
    const std::size_t length = std::char_traits<SharedUtilsWideChar>::length(lowered.data());
    const std::u16string name =
        SanitizeUtf16(std::u16string_view(reinterpret_cast<const char16_t*>(lowered.data()), length));
    const std::wstring hostName = Utf16ToWide(name);

    const std::wregex expression(L"([^\\W_]*)\\(([\\d]*)\\)");
    std::wsmatch match;
    if (std::regex_match(hostName, match, expression)) {
        const std::wstring world = match[2].str();
        const int parsedWorld = static_cast<int>(std::wcstol(world.c_str(), nullptr, 10));
        CSPointer<CPledge> result = s_pledgeNameMap.FindObject(parsedWorld, WideToUtf16(match[1].str()));
        return result;
    }
    return s_pledgeNameMap.FindObject(worldId, name);
}

User* CPledge::GetMaster() {
    L2_GUARD_FUNCTION();
    User* master = nullptr;
    if (m_data->m_OnlineMaster.id != 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_data->m_OnlineMaster.id);
        if (creature != nullptr && creature->IsPC()) {
            master = creature->CastUser();
        }
        if (master == nullptr) {
            m_data->m_OnlineMaster.id = 0;
            m_data->m_OnlineMaster.index = static_cast<std::uint32_t>(-1);
        }
    }
    return master;
}

User* CPledge::GetAnyElite() {
    // Current L2Server 0x00971a74.
    const std::uint32_t serverId = m_memberList->GetAnyOnlineElite();
    if (serverId == 0) {
        return nullptr;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(serverId);
    return creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
}

User* CPledge::GetMasterOrAnyElite() {
    // Current L2Server 0x00971b14.
    User* const master = GetMaster();
    return master != nullptr ? master : GetAnyElite();
}

// The names stay in the shared UTF-16 record, as in the original.
const char16_t* CPledge::GetPledgeName() const {
    L2_GUARD_FUNCTION();
    return m_data->m_sPledgeName;
}

const char16_t* CPledge::GetPledgeMasterName() const {
    L2_GUARD_FUNCTION();
    return m_data->m_sMasterName;
}

const char16_t* CPledge::GetMasterName() const {
    L2_GUARD_FUNCTION();
    return GetPledgeMasterName();
}

bool CPledge::CanAcceptNewMember() {
    L2_GUARD_FUNCTION();

    if (m_data->m_nOustTime == 0) {
        return true;
    }
    const std::int32_t elapsed =
        runtime::Int32FromBits(static_cast<std::uint32_t>(runtime::crt::Time32Now()) - m_data->m_nOustTime);
    return elapsed >= BP::_PLEDGE_OUST_PENALTY_TIMEOUT;
}

bool CPledge::CanJoinAlliance() const {
    // Current L2Server 0x0096a950.
    L2_GUARD_FUNCTION();
    if (m_nAllianceOustedTime == 0 && m_nAllianceWithdrawTime == 0) {
        return true;
    }

    const std::uint32_t now = static_cast<std::uint32_t>(runtime::crt::Time32Now());
    if (runtime::Int32FromBits(now - m_nAllianceOustedTime) < BP::_ALLIANCE_OUSTED_PENALTY_TIMEOUT) {
        return false;
    }
    return runtime::Int32FromBits(now - m_nAllianceWithdrawTime) >= BP::_ALLIANCE_WITHDRAW_PENALTY_TIMEOUT;
}

bool CPledge::CanCreateAlliance() const {
    // Current L2Server 0x0096aa0c.
    L2_GUARD_FUNCTION();
    if (m_nAllianceDismissTime == 0) {
        return true;
    }

    const std::uint32_t now = static_cast<std::uint32_t>(runtime::crt::Time32Now());
    return runtime::Int32FromBits(now - m_nAllianceDismissTime) >= BP::_ALLIANCE_DISMISS_PENALTY_TIMEOUT;
}

bool CPledge::IsWaitingToDismiss() const {
    L2_GUARD_FUNCTION();
    return m_data->m_nStatus == 3;
}

bool CPledge::IsWaitingTransferMaster() const {
    // Current L2Server 0x0096aab4 is the direct +0x226 byte load.
    return m_bWaitingTransferMaster;
}

void CPledge::SetWaitingTransferMaster(const bool waiting) {
    // Current L2Server 0x0096aabc is the direct +0x226 byte store.
    m_bWaitingTransferMaster = waiting;
}

bool CPledge::CheckValidMasterCandidate(const char16_t* const masterName) {
    // Current L2Server 0x00970040.
    L2_GUARD_FUNCTION();
    return m_memberList->CheckValidMasterCandidate(masterName);
}

bool CPledge::IsMemberInTeamBattleEntry(User* const user) {
    // Current L2Server 0x00978768.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    for (const auto& residenceEntry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = residenceEntry.second;
        if (!residence->IsWorldAgit()) {
            continue;
        }
        CWorldAgit* worldAgit = residence->CastWorldAgit();
        if (!worldAgit->IsTeamBattleAgit()) {
            continue;
        }
        worldAgit = residence->CastWorldAgit();
        CTeamBattleAgit* const teamBattleAgit = worldAgit->CastTeamBattleAgit();
        const std::uint32_t userDatabaseId = user->GetDBID();
        if (teamBattleAgit->IsBattleRoyalEntryMember(pledge->m_data->m_nDbId, userDatabaseId)) {
            return true;
        }
        if (teamBattleAgit->IsMasterEntryMember(user->GetDBID())) {
            return true;
        }
    }
    return false;
}

void CPledge::PledgeMasterTransferByNpc(CNPC* const npc, User* const user, const char16_t* const masterName) {
    // Current L2Server 0x0097bb5c.
    L2_GUARD_FUNCTION();
    if (npc->m_data == nullptr) {
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->m_data->m_bPledgeMaster) {
        g_npcserver.Send(
            "cdddSd", 0x5b, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()),
            masterName, -1
        );
        user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        return;
    }
    if (pledge->IsWaitingToDismiss()) {
        g_npcserver.Send(
            "cdddSd", 0x5b, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()),
            masterName, -2
        );
        return;
    }

    if (IsMemberInTeamBattleEntry(user)) {
        if (pledge->m_pWorldAgit == nullptr || !pledge->m_pWorldAgit->IsTeamBattleAgit()) {
            g_npcserver.Send(
                "cdddSd", 0x5b, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()),
                masterName, -4
            );
            user->GetSocket()->SendSystemMessage(
                SystemMessageId::CANNOT_TRANSFER_CLAN_LEADERSHIP_WHILE_REGISTERED_FOR_TEAM_CLAN_HALL_BATTLE
            );
            return;
        }
        user->GetSocket()->SendSystemMessage(SystemMessageId::PREVIOUS_CLAN_LEADER_PARTICIPATES_IN_CLAN_HALL_WAR);
    }

    if (!pledge->CheckValidMasterCandidate(masterName)) {
        g_npcserver.Send(
            "cdddSd", 0x5b, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()),
            masterName, 0
        );
        return;
    }
    db.RequestPledgeMasterTransferByNpc(npc, user, pledge, masterName);
}

void CPledge::PledgeMasterTransferCancelByNpc(CNPC* const npc, User* const user) {
    // Current L2Server 0x0096d9bc.
    L2_GUARD_FUNCTION();
    if (npc->m_data == nullptr) {
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->m_data->m_bPledgeMaster) {
        g_npcserver.Send("cdddd", 0x5c, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()), -1);
        user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        return;
    }
    if (pledge->IsWaitingToDismiss()) {
        g_npcserver.Send("cdddd", 0x5c, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()), -2);
        return;
    }
    db.RequestPledgeMasterTransferCancelByNpc(npc, user, pledge);
}

void CPledge::CreatePledgeByNpc(CNPC* const npc, User* const user, const char16_t* const pledgeName) {
    // Current L2Server 0x0096a30c.
    L2_GUARD_FUNCTION();
    if (npc->m_data == nullptr) {
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User *pUser] is nullptr",
        //     L"Pledge.cpp",
        //     0xab)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User *pUser] is nullptr",
        //     L"Pledge.cpp",
        //     0xab)
        L2_LOG_ERROR(Log, "Cannot create a clan through an NPC: the user is unavailable.");
        return;
    }

    const auto sendResult = [npc, user](const int result) {
        g_npcserver.Send("cdddd", 0x21, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index, result);
    };

    if (user->m_data->m_nPledgeID != 0) {
        sendResult(0);
        return;
    }
    if (!user->CanCreatePledge()) {
        sendResult(0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CREATE_CLAN_DURING_POST_DISSOLUTION_RESTRICTION);
        return;
    }
    if (!F2PConfig::CanCreatePledge(user)) {
        sendResult(0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::FREE_USER_CANNOT_CREATE_CLAN);
        return;
    }
    if (g_nCountryCode == 3 && user->GetSocket()->m_bEconomicRestriction) {
        sendResult(0);
        user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return;
    }
    if (g_nCountryCode == 8 && !User::CheckCharacterNameDual(pledgeName, 8, false)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CLAN_NAME);
        return;
    }
    if (pledgeName == nullptr || !User::CheckCharacterName(pledgeName, g_nCountryCode, false)) {
        sendResult(0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CLAN_NAME);
        return;
    }

    const std::size_t nameLength = std::char_traits<char16_t>::length(pledgeName);
    if (nameLength - 1U < 0x10U) {
        db.RequestCreatePledgeByNpc(npc, user, pledgeName);
        return;
    }
    sendResult(0);
    user->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CLAN_NAME_LENGTH);
}

void CPledge::DismissPledgeByNpc(CNPC* const npc, User* const user) {
    // Current L2Server 0x0096d554.
    L2_GUARD_FUNCTION();
    if (npc->m_data == nullptr) {
        return;
    }

    const auto sendFailure = [npc, user] {
        g_npcserver.Send("cdddd", 0x22, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index, 0);
    };

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->m_data->m_bPledgeMaster) {
        sendFailure();
        user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        return;
    }
    if (pledge->IsWaitingToDismiss()) {
        sendFailure();
        user->GetSocket()->SendSystemMessage_S(
            SystemMessageId::CLAN_DISSOLUTION_ALREADY_REQUESTED, std::u16string(pledge->GetPledgeName()).c_str()
        );
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t lastDismissPropose = user->GetLastDismissProposeTime();
    if (lastDismissPropose != 0 &&
        runtime::Int32FromBits(static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(lastDismissPropose)) <
            BP::_PLEDGE_DISMISS_TIMEOUT) {
        sendFailure();
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_REPEAT_DISSOLUTION_REQUEST_WITHIN_RESTRICTION_PERIOD);
        return;
    }
    if (pledge->m_data->m_nAllianceDbId != 0) {
        sendFailure();
        // BUG in shipping EXE at 0096d76d: alliance membership reports another clan's dissolution.
        // user->GetSocket()->SendSystemMessage(SystemMessageId::TARGET_CLAN_DISSOLUTION_REQUESTED /* Current 0096d76d:
        // dissolution text on nonzero alliance ID. */)
        user->GetSocket()->SendSystemMessage(SystemMessageId::ALREADY_IN_ALLIANCE);
        return;
    }
    if (pledge->m_nDeclareCount != 0) {
        sendFailure();
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_DISSOLVE_CLAN_DURING_WAR);
        return;
    }
    if (pledge->m_pWorldAgit != nullptr) {
        sendFailure();
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_DISSOLVE_CLAN_OWNING_HALL_OR_CASTLE);
        return;
    }
    if (pledge->m_pCastle != nullptr || pledge->m_pFortress != nullptr) {
        sendFailure();
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_DISSOLVE_CLAN_OWNING_HALL_OR_CASTLE);
        return;
    }
    if (CResidence::IsSiegeRegistered(pledge)) {
        sendFailure();
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_DISSOLVE_CLAN_PARTICIPATING_IN_SIEGE);
        return;
    }
    db.RequestQueryCastleSiegeForDismiss(user, pledge, static_cast<unsigned int>(npc->m_data->index));
}

void CPledge::RevivePledgeByNpc(CNPC* const npc, User* const user) {
    // Current L2Server 0x00981648.
    L2_GUARD_FUNCTION();
    if (npc->m_data == nullptr) {
        return;
    }

    const auto sendResult = [npc, user](const int result) {
        g_npcserver.Send("cdddd", 0x23, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index, result);
    };

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->m_data->m_bPledgeMaster) {
        sendResult(0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        return;
    }
    if (!pledge->IsWaitingToDismiss()) {
        sendResult(0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_CLAN_DISSOLUTION_REQUEST);
        return;
    }

    pledge->m_data->m_nStatus = 0;
    pledge->Save(0xf);
    pledge->Save(0xb);
    pledge->UpdateInfoWindowPacket();
    g_PledgeTimer.CancelDismiss(pledge->m_data->m_nDbId);
    sendResult(1);
}

void CPledge::SetPledgeMemberCount(std::uint32_t memberCount) {
    L2_GUARD_FUNCTION();
    m_data->m_nTotalMemberCount = memberCount;
}

std::uint32_t CPledge::GetAllianceSid() {
    L2_GUARD_FUNCTION();
    return m_nAllianceSid;
}

std::uint32_t CPledge::GetAllianceDismissTime() { return m_nAllianceDismissTime; }

std::uint32_t CPledge::GetAllianceOutTime() { return std::max(m_nAllianceWithdrawTime, m_nAllianceOustedTime); }

void CPledge::SetAllianceDismissTime(const std::uint32_t time) {
    // Current L2Server 0x0096b594.
    m_nAllianceDismissTime = time;
}

void CPledge::SetAllianceOustedTime(std::uint32_t time) { m_nAllianceOustedTime = time; }

void CPledge::SetAllianceWithdrawTime(std::uint32_t time) { m_nAllianceWithdrawTime = time; }

CAlliance* CPledge::GetAlliance() {
    L2_GUARD_FUNCTION();

    CAlliance* alliance = nullptr;
    if (m_nAllianceSid != 0) {
        alliance = AllianceSmartIdManager()->GetObjectW(m_nAllianceSid);
        if (alliance == nullptr) {
            m_nAllianceSid = 0;
        }
    }
    return alliance;
}

bool CPledge::IsAllianceMaster() const {
    // Current L2Server 0x0097394c.
    CAlliance* const alliance = const_cast<CPledge*>(this)->GetAlliance();
    return alliance != nullptr && alliance->m_data->m_nMasterPledgeDbId == m_data->m_nDbId;
}

void CPledge::SetAlliance(CAlliance* const alliance, const bool serveForRaid) {
    // Current L2Server 0x00979858.
    L2_GUARD_FUNCTION();

    bool masterPledge = false;
    if (alliance == nullptr) {
        m_data->m_nAllianceDbId = 0;
        m_nAllianceSid = 0;
    } else {
        m_data->m_nAllianceDbId = alliance->m_data->m_nDbId;
        m_nAllianceSid = alliance->GetSmartID().Sid();
        masterPledge = alliance->m_data->m_nMasterPledgeDbId == m_data->m_nDbId;
    }

    ::SetAlliance service(alliance, masterPledge);
    ServeForAllMember(service, serveForRaid);
}

void CPledge::SetAllianceByDbId(const unsigned int allianceDatabaseId) {
    // Current L2Server 0x0097dac0.
    L2_GUARD_FUNCTION();
    if (allianceDatabaseId == 0) {
        return;
    }

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(m_nWorldId, static_cast<int>(allianceDatabaseId)));
    if (!alliance) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Alliance is not Loaded AllianceDBID[%d] WorldID[%d]",
        //     allianceDatabaseId,
        //     m_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"Alliance is not Loaded AllianceDBID[%d] WorldID[%d]",
        //     allianceDatabaseId,
        //     m_nWorldId)
        L2_LOG_ERROR(
            Log, "Cannot attach alliance %u in world %d: the alliance is not loaded.",
            static_cast<unsigned>(allianceDatabaseId), m_nWorldId
        );
        return;
    }

    alliance->AddMemberPledge(this);
    SetAlliance(alliance.get(), true);
}

CCastle* CPledge::GetCastle() { return m_pCastle; }

void CPledge::SetCastle(CCastle* const castle) {
    // Current L2Server 0x0096e580.
    L2_GUARD_FUNCTION();
    if (m_pCastle == castle) {
        return;
    }

    if (m_pCastle != nullptr) {
        m_pCastle->RemoveResidenceSkill(*this);
        SetDominion(nullptr);
    }

    CDominion* dominion = nullptr;
    if (castle == nullptr) {
        m_data->m_nCastleDbId = 0;
        m_pCastle = nullptr;
    } else {
        m_data->m_nCastleDbId = castle->m_data != nullptr ? static_cast<std::uint32_t>(castle->m_data->m_nResidenceID) : 0;
        m_pCastle = castle;
        castle->AddResidenceSkill(*this, static_cast<CastleType::Enum>(castle->m_data->m_ResidenceType));
        dominion = castle->m_pDominion;
    }
    SetDominion(dominion);
}

void CPledge::SetCastleByDbId(const unsigned int castleDatabaseId) {
    // Current L2Server 0x0096eff0.
    L2_GUARD_FUNCTION();
    if (castleDatabaseId == 0) {
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(castleDatabaseId));
    if (residence == nullptr || !residence->IsCastle()) {
        return;
    }

    CCastle* const castle = residence->CastCastle();
    if (castle == nullptr || castle->m_data == nullptr || castle->m_data->m_nResidenceOwnerWorldId != m_nWorldId ||
        castle->m_data->m_nResidenceOwnerID != m_data->m_nDbId) {
        return;
    }

    castle->SetPledge(this);
    SetCastle(castle);
}

void CPledge::SetFortress(CFortress* const fortress) {
    // Current L2Server 0x0096b170.
    L2_GUARD_FUNCTION();
    if (m_pFortress == fortress) {
        return;
    }

    if (m_pFortress != nullptr) {
        m_pFortress->RemoveResidenceSkill(*this);
    }
    if (fortress == nullptr) {
        m_data->m_nFortressDbId = 0;
        m_pFortress = nullptr;
        return;
    }

    m_data->m_nFortressDbId = fortress->m_data != nullptr ? static_cast<std::uint32_t>(fortress->m_data->m_nResidenceID) : 0;
    m_pFortress = fortress;
    fortress->AddResidenceSkill(*this, CastleType::CASTLE_NOTHING);
}

void CPledge::SetFortressByDbId(const unsigned int fortressDatabaseId) {
    // Current L2Server 0x0096b8fc.
    L2_GUARD_FUNCTION();
    if (fortressDatabaseId == 0) {
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(fortressDatabaseId));
    if (residence == nullptr || !residence->IsFortress()) {
        return;
    }

    CFortress* const fortress = residence->CastFortress();
    fortress->SetPledge(this);
    SetFortress(fortress);
}

void CPledge::SetDominion(CDominion* const dominion) {
    // Current L2Server 0x0096b26c.
    L2_GUARD_FUNCTION();
    if (m_pDominion == dominion) {
        return;
    }
    if (m_pDominion != nullptr) {
        m_pDominion->SetPledge(nullptr);
    }
    m_pDominion = dominion;
    if (m_pDominion != nullptr) {
        m_pDominion->SetPledge(this);
    }
}

CWorldAgit* CPledge::GetWorldAgit() { return m_pWorldAgit; }

void CPledge::SetWorldAgit(CWorldAgit* const worldAgit) {
    // Current L2Server 0x0096afb0.
    L2_GUARD_FUNCTION();
    if (m_pWorldAgit == worldAgit) {
        return;
    }

    if (m_pWorldAgit != nullptr) {
        m_pWorldAgit->RemoveResidenceSkill(*this);
    }
    if (worldAgit == nullptr) {
        m_data->m_nWorldAgitDbId = 0;
        m_pWorldAgit = nullptr;
        return;
    }

    m_data->m_nWorldAgitDbId = static_cast<std::uint32_t>(worldAgit->m_data->m_nResidenceID);
    m_pWorldAgit = worldAgit;
    worldAgit->AddResidenceSkill(*this, CastleType::CASTLE_NOTHING);
}

void CPledge::SetAgitByDbId(const unsigned int agitDatabaseId) {
    // Current L2Server 0x0096b810.
    L2_GUARD_FUNCTION();
    if (agitDatabaseId == 0) {
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(agitDatabaseId));
    if (residence == nullptr || !residence->IsWorldAgit()) {
        return;
    }

    CWorldAgit* const worldAgit = residence->CastWorldAgit();
    worldAgit->SetPledge(this);
    SetWorldAgit(worldAgit);
}

CCompeteAgit* CPledge::GetCompeteAgit() const {
    L2_GUARD_FUNCTION();
    return m_pCompeteAgit;
}

void CPledge::SetCompeteAgit(CCompeteAgit* const competeAgit) {
    // Current L2Server 0x0096b09c.
    L2_GUARD_FUNCTION();
    if (m_pCompeteAgit == competeAgit) {
        return;
    }

    if (competeAgit == nullptr) {
        m_data->m_nCAgitDbid = 0;
        m_pCompeteAgit = nullptr;
        return;
    }

    m_data->m_nCAgitDbid =
        competeAgit->m_data != nullptr ? static_cast<std::uint32_t>(competeAgit->m_data->m_nResidenceID) : 0;
    m_pCompeteAgit = competeAgit;
}

CCastle* CPledge::GetCastleUnderSiege(const int castleIndex) const {
    // Current L2Server 0x0096b398. The original comparison permits index
    // ten even though the physical source array has nine entries. Retain
    // that shipped boundary literally.
    L2_GUARD_FUNCTION();
    const std::uint32_t index = static_cast<std::uint32_t>(castleIndex) - 1U;
    if (index < 10U) {
        return m_pCastleUnderSiege[index];
    }
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[%s][%d] castle_index is wrong!! [%d]",
    //     L"Pledge.cpp",
    //     0xf54,
    //     index)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] castle_index is wrong!! [%d]",
    //     L"Pledge.cpp",
    //     0xf54,
    //     index)
    L2_LOG_ERROR(
        Log,
        "Cannot get the castle under siege: castle index %d is outside 1 through 10; reporting the stack and returning no "
        "castle.",
        castleIndex
    );
    runtime::process::ReportNamedCallStack(L"Siege", 2, false);
    return nullptr;
}

bool CPledge::IsGuilty() { return m_data->m_bIsGuilty; }

bool CPledge::CanOwnFortress(const unsigned int fortressId) {
    // Current L2Server 0x0096e690.
    L2_GUARD_FUNCTION();
    if (m_pCastle != nullptr) {
        return false;
    }
    if (m_pFortress != nullptr) {
        const unsigned int ownedFortressId =
            m_pFortress->m_data != nullptr ? static_cast<unsigned int>(m_pFortress->m_data->m_nResidenceID) : 0U;
        if (ownedFortressId != fortressId && m_pFortress->IsInSiege()) {
            return false;
        }
    }
    return m_pFunction != nullptr && m_pFunction->IsEnable(PledgeFunctionType::FORTRESS_SIEGE);
}

void CPledge::SelectMemberIDs(std::vector<int>& memberIds) const {
    // Current L2Server 0x0096e780.
    L2_GUARD_FUNCTION();
    m_memberList->SelectMemberIDs(memberIds);
}

int CPledge::GetNameValue() {
    const int value = [&] {
        L2_READ_GUARD(m_nameValueLock);
        const int value = m_data->m_nRootNameValue;
        return value;
    }();
    return value;
}

bool CPledge::IsValidNameValue() {
    // Current L2Server 0x009715d8.
    L2_GUARD_FUNCTION();
    return m_data->m_nSkillLevel > 4 && GetNameValue() > 0;
}

void CPledge::SetNameValue(const int value) {
    // Current L2Server 0x0096e874.
    {
        L2_WRITE_GUARD(m_nameValueLock);
        m_data->m_nRootNameValue = value;
    }
}

bool CPledge::UseNameValue(const int amount) {
    // Current L2Server 0x009717a4.
    L2_GUARD_FUNCTION();
    if (amount <= 0 || GetNameValue() < amount) {
        return false;
    }
    db.UpdatePledgeNameValue(Id(), m_data->m_nDbId, GetNameValue(), -amount, m_nWorldId);
    return true;
}

bool CPledge::UpdateNameValueByPledgeWar(
    const unsigned int userServerId, const int delta, const unsigned int enemyPledgeDatabaseId
) {
    // Current L2Server 0x00971690.
    L2_GUARD_FUNCTION();
    if (m_nWorldId != g_nWorldId || !((delta < 0 && delta <= GetNameValue()) || delta > 0)) {
        return false;
    }
    db.UpdatePledgeNameValueByPledgeWar(userServerId, Id(), m_data->m_nDbId, GetNameValue(), delta, enemyPledgeDatabaseId);
    return true;
}

void CPledge::UpdateNameValue(const int delta) {
    L2_GUARD_FUNCTION();
    int messageId = 0;
    {
        L2_WRITE_GUARD(m_nameValueLock);
        const int oldValue = m_data->m_nRootNameValue;
        const std::int64_t sum = static_cast<std::int64_t>(oldValue) + delta;
        const std::int64_t bounded =
            std::clamp(sum, static_cast<std::int64_t>(INT32_MIN), static_cast<std::int64_t>(INT32_MAX));
        m_data->m_nRootNameValue = static_cast<int>(bounded);
        CPledgeRanking::Update(m_data->m_nDbId, m_data->m_nRootNameValue);
        if (oldValue < 0 && m_data->m_nRootNameValue >= 0) {
            messageId = SystemMessageId::CLAN_SKILLS_ACTIVATED_REPUTATION_POSITIVE;
        } else if (oldValue >= 0 && m_data->m_nRootNameValue < 0) {
            messageId = SystemMessageId::CLAN_SKILLS_DISABLED_NEGATIVE_REPUTATION;
        }
    }

    if (messageId != 0) {
        ApplyPledgeSkillsToMembers(false, false, true);
        SystemMessage(messageId);
    }
    UpdateInfoWindowPacket();
}

void CPledge::ApplyPledgeSkillsTo(User* user, const bool sendAcquireMessage, const bool replaceSkill) {
    m_pSkill->ApplySkillsTo(user, GetNameValue() < 0, sendAcquireMessage, replaceSkill, false);
}

namespace PledgeNS {

class Callback {
public:
    virtual void Execute() = 0;
};

class PledgeSkillAcquireCallback : public Callback {
public:
    explicit PledgeSkillAcquireCallback(User* const user) : m_user(user) {}

    void Execute() override;

private:
    User* m_user;
};

class CallbackHolder {
public:
    explicit CallbackHolder(Callback& callback) : m_callback(callback), m_callbackEnable(false) {}

    ~CallbackHolder();

    void SetCallbackExecute(const bool enable) { m_callbackEnable = enable; }

private:
    Callback& m_callback;
    bool m_callbackEnable;
};


void PledgeSkillAcquireCallback::Execute() {
    m_user->SetMaxMagicLevel();
    m_user->m_nParamChanged |= PCB_USER;
    m_user->SendSkillList(nullptr, true, 0);
}

CallbackHolder::~CallbackHolder() {
    if (m_callbackEnable) {
        m_callback.Execute();
    }
}

} // namespace PledgeNS

void CPledge::AddPledgeSkillsTo(User* const pUser, const bool bActivate) {
    L2_GUARD_FUNCTION();
    if (pUser == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x1fd5,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x1fd5,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot %s clan skills: the user is unavailable.", bActivate ? "activate" : "deactivate");
        return;
    }
    if (bActivate && GetNameValue() < 0) {
        return;
    }

    PledgeNS::PledgeSkillAcquireCallback callback(pUser);
    PledgeNS::CallbackHolder holder(callback);
    m_pSkill->ForEach([&holder, pUser, bActivate](const int skillId, const int skillLevel) {
        static constexpr wchar_t functionName[] = L"Pledge.cpp(8166)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());

        {
            L2_GUARD(*pUser->m_lock);
            if (pUser->HaveSkill(skillId, skillLevel)) {
                pUser->SetSkillLock(skillId, skillLevel, 0, bActivate ? 0 : 1);
                holder.SetCallbackExecute(true);
            }
        }
    });

    pUser->ValidateChanged(VB_MAX);
}

bool CPledge::RemovePledgeSkill(const int skillId, const bool synchronizeRaid) {
    // Current L2Server 0x00976808.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(*m_lock);
        if (!m_pSkill->HasSkill(skillId)) {
            return false;
        }

        for (const std::uint32_t memberSid : m_memberSidSet) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(memberSid);
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user == nullptr) {
                continue;
            }

            // Current owner-local helper at 0x0096b788 is compiled from this
            // CPledge access context. Keep the operations here so the original
            // CPledge friendship, instead of an invented User setter, governs
            // the direct m_nParamChanged update.
            user->DeleteAcquireSkill(skillId);
            user->ValidateChanged(VB_MAX);
            user->SetMaxMagicLevel();
            user->m_nParamChanged |= PCB_USER;
            user->SendSkillList(nullptr, true, 0);
            user->GetSocket()->Send("chd", 0xfe, 0x3c, skillId);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][PLEDGE][%s][%d]delete pledge skill: %s, %u",
            //     L"Pledge.cpp",
            //     0x1ef0,
            //     user->GetName(),
            //     skillId)
            // Noise: Per-member skill deletion during a clan-skill update is routine tracing. Skill removal and client
            // updates remain. L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][PLEDGE][%s][%d]delete pledge skill: %s, %u",
            //     L"Pledge.cpp",
            //     0x1ef0,
            //     user->GetName(),
            //     skillId)
        }

        if (!m_pSkill->RemoveSkill(skillId)) {
            return false;
        }

        if (synchronizeRaid) {
            PMS_SyncPledgeSkill service(skillId, 0);
            ServeForAllMember(service, true);
        }
    }
    return true;
}

void CPledge::Save(const unsigned int field) {
    // Current L2Server 0x00973668.
    L2_GUARD_FUNCTION();
    const int fieldValue = GetFieldValue(field);
    if (field == 5 && m_data->m_nSkillLevel == 2) {
        if (GetMaster() != nullptr) {
            const int pledgeId = static_cast<int>(m_data->m_nDbId);
            const char16_t* const masterName = GetMasterName();
            const char16_t* const pledgeName = GetPledgeName();
            User* const master = GetMaster();
            commcli.RequestClanBBSMake(master, pledgeId, pledgeName, masterName);
        }
    }
    db.RequestSetPledgeInfo(m_nWorldId, m_data->m_nDbId, field, static_cast<unsigned int>(fieldValue));
}

void CPledge::SetPledgeLevel(const unsigned int level, const bool save) {
    L2_GUARD_FUNCTION();
    if (m_data->m_nSkillLevel == level) {
        return;
    }

    m_data->m_nSkillLevel = level;
    if (CPledgeRanking::IsInRanking(m_data->m_nDbId)) {
        if (level <= 4) {
            CPledgeRanking::Remove(m_data->m_nDbId);
        }
    } else if (level > 4) {
        CPledgeRanking::Insert(m_data->m_nDbId, GetNameValue());
    } else {
        CPledgeRanking::Remove(m_data->m_nDbId);
    }

    OnChangedPledgeSkillLevel();
    if (save) {
        Save(5);
        CPledgeJoinSystem::Inst()->RequestModifyPledgeInfo(this);
        PMS_SyncPledgeLevel service(static_cast<int>(m_data->m_nSkillLevel));
        ServeForAllMember(service, true);
    }
}

void CPledge::SetPowerGrade(const int gradeId, const unsigned long power) {
    // Current L2Server 0x0097545c. The shipping std::bitset<25>
    // conversion keeps only the low 25 bits of the unsigned-long input.
    // Instructions 0x00975535..0x00975545 address the physical array at
    // +0x5c0 as +0x5bc + gradeId * 4, which is gradeId - 1.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(gradeId) - 1U >= 9U) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Invalid Pledge Power Grade Id(%d)",
        //     gradeId)
        // L2_LOG_ERROR(Log,
        //     "Invalid Pledge Power Grade Id(%d)",
        //     gradeId)
        L2_LOG_ERROR(Log, "Cannot set clan privilege grade %d: valid grades are 1 through 9.", gradeId);
        return;
    }

    m_data->m_gradePowerList[gradeId - 1] = static_cast<std::uint32_t>(power) & 0x01ffffffU;
}

void CPledge::SetMasteryResetTime(const unsigned int time) noexcept { m_data->m_nMasteryResetTime = time; }

bool CPledge::CanResetMastery(const int penaltySeconds) const {
    L2_GUARD_FUNCTION();
    if (penaltySeconds == 0 || m_data->m_nMasteryResetTime == 0) {
        return true;
    }
    const std::uint32_t elapsed = static_cast<std::uint32_t>(runtime::crt::Time32Now()) - m_data->m_nMasteryResetTime;
    return penaltySeconds <= static_cast<std::int32_t>(elapsed);
}

bool CPledge::AddPledgeSkillWithSaveAndNotify(
    const int skillId,
    const int skillLevel,
    const std::int32_t expiryTime,
    const bool synchronizeRaid,
    const bool sendAcquireMessage,
    const bool replaceSkill
) {
    L2_GUARD_FUNCTION();
    if (!m_pSkill->AddSkill(skillId, skillLevel, expiryTime)) {
        return false;
    }

    std::optional<CSendPledgeSkillAdd> sendStorage;
    {
        L2_GUARD(*m_lock);
        CSendPledgeSkillAdd& send = sendStorage.emplace(skillId, skillLevel);
        _ServeForAllMember(send);
    }

    if (synchronizeRaid) {
        PMS_SyncPledgeSkill sync(skillId, skillLevel);
        ServeForAllMember(sync, true);
    }
    if (m_nWorldId == g_nWorldId) {
        SavePledgeSkillToDB(skillId, skillLevel, expiryTime);
    }
    ApplyPledgeSkillsToMembers(sendAcquireMessage, replaceSkill, false);
    return true;
}

bool CPledge::AddPledgeSkillWithNotify(const unsigned int skillId, const int skillLevel, const std::int32_t expiryTime) {
    // Current L2Server 0x00975578.
    L2_GUARD_FUNCTION();
    if (!m_pSkill->AddSkill(skillId, skillLevel, expiryTime)) {
        return false;
    }

    std::optional<CSendPledgeSkillAdd> serviceStorage;
    {
        L2_GUARD(*m_lock);
        CSendPledgeSkillAdd& service = serviceStorage.emplace(static_cast<int>(skillId), skillLevel);
        _ServeForAllMember(service);
    }
    return true;
}

void CPledge::SavePledgeSkillToDB(const int skillId, const int skillLevel, const std::int32_t expiryTime) {
    // Current L2Server 0x0096bed0.
    L2_GUARD_FUNCTION();
    db.RequestSavePledgeSkill(*this, static_cast<unsigned int>(skillId), skillLevel, expiryTime);
}

void CPledge::UnequipMemberCastleItem() {
    // Current L2Server 0x009762dc.
    L2_GUARD_FUNCTION();
    UnequipCastleItem service;
    ServeForAllMember(service, true);
}

void CPledge::RemovePledgeSkills(const bool masteryOnly) {
    L2_GUARD_FUNCTION();
    std::vector<int> skillIds;
    m_pSkill->GetSkillIDs(skillIds, masteryOnly);
    for (const int skillId : skillIds) {
        RemovePledgeSkill(skillId, false);
    }
    if (m_nWorldId == g_nWorldId) {
        for (const int skillId : skillIds) {
            db.RequestDelPledgeSkill(*this, static_cast<unsigned int>(skillId));
        }
    }
}

void CPledge::OnChangedPledgeSkillLevel() {
    L2_GUARD_FUNCTION();
    ApplyPledgeSkillsToMembers(false, false, false);
    SetParamChanged service(std::int64_t{PCB_CHAR_PLEDGESKILLLEVEL});
    ServeForAllMember(service, true);
}

void CPledge::ApplyPledgeSkillsToMembers(const bool sendAcquireMessage, const bool replaceSkill, const bool refresh) {
    L2_GUARD_FUNCTION();
    std::set<std::uint32_t> memberIds;
    {
        L2_GUARD(*m_lock);
        memberIds = m_memberSidSet;
    }
    m_pSkill->ApplySkillsTo(memberIds, GetNameValue() < 0, sendAcquireMessage, replaceSkill, refresh);
}

void CPledge::SystemMessage(const int messageId) {
    L2_GUARD_FUNCTION();
    ::SystemMessage<NoCheck, User> service(messageId);
    ServeForAllMember(service, true);
}

void CPledge::SystemMessage(const int messageId, const int parameter) {
    // Current L2Server 0x00975e9c.
    L2_GUARD_FUNCTION();
    ::SystemMessage_d<NoCheck, User> service(messageId, parameter);
    ServeForAllMember(service, true);
}

void CPledge::SystemMessage(
    const int messageId, const eSMsgType argumentType, const char16_t* const text, const int parameter
) {
    // Current L2Server 0x00975cbc.
    L2_GUARD_FUNCTION();
    ::SystemMessage_S_d<NoCheck, User> service(messageId, argumentType, text, parameter);
    ServeForAllMember(service, true);
}

void CPledge::SystemMessage(
    const int messageId,
    const eSMsgType firstArgumentType,
    const char16_t* const firstText,
    const eSMsgType secondArgumentType,
    const char16_t* const secondText,
    const int parameter
) {
    // Current L2Server 0x00975bac.
    L2_GUARD_FUNCTION();
    ::SystemMessage_S_S_d<NoCheck, User> service(
        messageId, firstArgumentType, firstText, secondArgumentType, secondText, parameter
    );
    ServeForAllMember(service, true);
}

void CPledge::SystemMessage(const int messageId, const int firstParameter, const int secondParameter) {
    // Current L2Server 0x00975f80.
    L2_GUARD_FUNCTION();
    ::SystemMessage_d_d<NoCheck, User> service(messageId, firstParameter, secondParameter);
    ServeForAllMember(service, true);
}

void CPledge::MailArrived() {
    // Current L2Server 0x0097a1b8.
    L2_GUARD_FUNCTION();
    SystemMessage(SystemMessageId::MAIL_ARRIVED);
    ArrivedMail service;
    ServeForAllMember(service, true);
}

void CPledge::SystemMessage(const int messageId, const eSMsgType argumentType, const char16_t* const text) {
    // Current L2Server 0x00975db0.
    L2_GUARD_FUNCTION();
    ::SystemMessage_S<NoCheck, User> service(messageId, argumentType, text);
    ServeForAllMember(service, true);
}

int CPledge::GetFieldValue(unsigned int field) {
    L2_GUARD_FUNCTION();

    switch (field) {
        case 0:
            return m_data->m_nAllianceDbId;
        case 1:
            return static_cast<int>(m_data->m_nChallengeTime);
        case 2:
            return GetNameValue();
        case 4:
            return static_cast<int>(m_data->m_nOustTime);
        case 5:
            return static_cast<int>(m_data->m_nSkillLevel);
        case 6:
            return static_cast<int>(m_data->m_nCastleDbId);
        case 7:
            return static_cast<int>(m_data->m_nWorldAgitDbId);
        case 8:
            return static_cast<int>(m_data->m_nFortressDbId);
        case 9:
            return m_data->m_nRank;
        case 0xb:
            return m_data->m_nStatus;
        case 0xc:
            return static_cast<int>(m_data->m_nPrivateFlag);
        case 0xd:
            return static_cast<int>(m_data->m_nCrestId);
        case 0xe:
            return m_data->m_bIsGuilty ? 1 : 0;
        case 0xf:
            return static_cast<int>(m_data->m_nDismissReservedTime);
        case 0x10:
            return static_cast<int>(m_nAllianceOustedTime);
        case 0x11:
            return static_cast<int>(m_nAllianceWithdrawTime);
        case 0x12:
            return static_cast<int>(m_nAllianceDismissTime);
        case 0x13:
            return static_cast<int>(m_data->m_nMasterDbId);
        case 0x14:
            return static_cast<int>(m_data->m_nEmblemId);
        case 0x15:
            return static_cast<int>(m_data->m_nMasteryResetTime);
        default:
            return 0;
    }
}

int CPledge::GetStatus() { return m_data->m_nStatus; }

void CPledge::SetStatus(const int status) {
    // Current L2Server 0x0096b35c.
    m_data->m_nStatus = status;
}

bool CPledge::BeginTransaction(int* const transaction) {
    // Current L2Server 0x0096b558.
    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_tlDB.m_nTransactionStatus, 1);
    if (previous != 0) {
        *transaction = m_tlDB.m_nTransaction;
        return false;
    }
    m_tlDB.m_nTransaction = *transaction;
    return true;
}

bool CPledge::EndTransaction() {
    // Current L2Server 0x0096b584.
    return runtime::raw_atomic::ExchangeI32(&m_tlDB.m_nTransactionStatus, 0) != 0;
}

bool CPledge::CanDeclareWarBoth(CSPointer<CPledge> enemyPledge) {
    // Current L2Server 0x0097ca34.
    L2_GUARD_FUNCTION();
    if (GetDbId() == enemyPledge->GetDbId()) {
        return false;
    }
    if (DidDeclareWar(static_cast<int>(enemyPledge->Id()))) {
        return false;
    }
    return enemyPledge->DidDeclareWar(static_cast<int>(Id()));
}

bool CPledge::CanDeclareWar(CSPointer<CPledge> enemyPledge, CUserSocket& socket) {
    // Current L2Server 0x0097cc10.
    L2_GUARD_FUNCTION();

    if (GetDbId() == enemyPledge->GetDbId()) {
        socket.SendSystemMessage(SystemMessageId::CANNOT_DECLARE_WAR_ON_OWN_CLAN);
        return false;
    }
    if (DidDeclareWar(static_cast<int>(enemyPledge->Id()))) {
        socket.SendSystemMessage(SystemMessageId::WAR_ALREADY_DECLARED_ON_CLAN);
        return false;
    }
    if (!m_pFunction->IsEnable(PledgeFunctionType::PLEDGE_WAR) || !HasEnoughMemberForWar()) {
        socket.SendSystemMessage(SystemMessageId::CLAN_WAR_REQUIRES_CLAN_LEVEL_AND_MEMBER_COUNT);
        return false;
    }
    if ((!enemyPledge->m_pFunction->IsEnable(PledgeFunctionType::PLEDGE_WAR) || !enemyPledge->HasEnoughMemberForWar()) &&
        !enemyPledge->DidDeclareWar(static_cast<int>(Id()))) {
        socket.SendSystemMessage_S(
            SystemMessageId::CANNOT_DECLARE_WAR_TARGET_CLAN_REQUIREMENTS_NOT_MET,
            std::u16string(enemyPledge->GetPledgeName()).c_str()
        );
        return false;
    }
    if (IsWaitingToDismiss() || enemyPledge->IsWaitingToDismiss()) {
        socket.SendSystemMessage(SystemMessageId::CANNOT_DECLARE_CLAN_WAR_DURING_DISSOLUTION);
        return false;
    }

    int transaction = 2;
    if (!BeginTransaction(&transaction)) {
        socket.SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        return false;
    }
    if (enemyPledge->DidDeclareWar(static_cast<int>(Id())) || m_nDeclareCount < 30) {
        return true;
    }

    socket.SendSystemMessage(SystemMessageId::TOO_MANY_SIMULTANEOUS_CLAN_WAR_DECLARATIONS);
    EndTransaction();
    return false;
}

bool CPledge::HasEnoughMemberForWar() {
    // Current L2Server 0x0096eae8.
    return s_ignoreMemberCountCondition || GetDbMemberCount() > 14;
}

bool CPledge::CanDoPledgeWork(const int timeoutSeconds) {
    // Current L2Server 0x0096ea6c.
    const std::int32_t now = runtime::crt::Time32Now();
    L2_GUARD(*m_lock);

    if (m_tLastPledgeWorkTimeOut == 0 || m_tLastPledgeWorkTimeOut < now) {
        m_tLastPledgeWorkTimeOut = runtime::AddInt32Wrapping(now, timeoutSeconds);
        return true;
    }
    return false;
}

bool CPledge::CanJoinMoreMember(const PledgeType pledgeType) {
    // Current L2Server 0x0096f194.
    L2_GUARD_FUNCTION();
    if (!PledgeStuff::IsValidPledgeType(pledgeType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid pledgeType : %d",
        //     L"Pledge.cpp",
        //     0x1b6b,
        //     static_cast<int>(pledgeType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid pledgeType : %d",
        //     L"Pledge.cpp",
        //     0x1b6b,
        //     static_cast<int>(pledgeType))
        L2_LOG_ERROR(Log, "Cannot check clan member capacity: subclan type %d is invalid.", static_cast<int>(pledgeType));
        return false;
    }

    const int eliteCount = m_memberList->Count(SUB_1);
    if (pledgeType == MASTER_PLEDGE) {
        const int memberCount = m_memberList->Count(MASTER_PLEDGE);
        return runtime::AddInt32Wrapping(memberCount, eliteCount) < m_pFunction->GetMaxMemberCount();
    }
    return eliteCount < m_pFunction->GetMaxEliteCount();
}

bool CPledge::CanAppointElite(const int characterDatabaseId) {
    // Current L2Server 0x0097e088.
    if (characterDatabaseId == 0 || m_data->m_nMasterDbId == static_cast<std::uint32_t>(characterDatabaseId) ||
        !CanJoinMoreMember(SUB_1)) {
        return false;
    }
    return m_memberList->GetPledgeType(static_cast<std::uint32_t>(characterDatabaseId)) == MASTER_PLEDGE &&
        !IsPlayingGame(characterDatabaseId);
}

bool CPledge::CanDismissElite(const int characterDatabaseId) {
    // Current L2Server 0x0097e11c.
    if (characterDatabaseId == 0 || m_data->m_nMasterDbId == static_cast<std::uint32_t>(characterDatabaseId)) {
        return false;
    }
    return m_memberList->GetPledgeType(static_cast<std::uint32_t>(characterDatabaseId)) == SUB_1 &&
        !IsPlayingGame(characterDatabaseId);
}

bool CPledge::SwitchPledgeWarMemberCountCondition() {
    // Current L2Server 0x0096ace8.
    L2_GUARD_FUNCTION();
    s_ignoreMemberCountCondition = !s_ignoreMemberCountCondition;
    return s_ignoreMemberCountCondition;
}

void CPledge::SetPledgeWorkDone() noexcept {
    // Current L2Server 0x0096b664.
    m_tLastPledgeWorkTimeOut = 0;
}

std::uint32_t CPledge::GetCrestId() { return m_data->m_nCrestId; }

void CPledge::SetNewCrestId(const std::uint32_t crestId) {
    // Current L2Server 0x0096dca0.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    m_data->m_nCrestId = crestId;
}

void CPledge::SetNewCrest(const std::uint32_t crestId) {
    // Current L2Server 0x00978910.
    L2_GUARD_FUNCTION();
    SetNewCrestId(crestId);
    SetCrest service(crestId);
    ServeForAllMember(service, true);
    if (m_pCastle != nullptr) {
        m_pCastle->SetAllNPCPledge(m_data->m_nDbId);
    }
}

void CPledge::SetMemberAllianceCrest(const std::uint32_t crestId) {
    // Current L2Server 0x00975adc.
    L2_GUARD_FUNCTION();
    SetAllianceCrest service(crestId);
    ServeForAllMember(service, true);
}

std::uint32_t CPledge::GetEmblemId() { return m_data->m_nEmblemId; }

void CPledge::SetNewEmblemId(const std::uint32_t emblemId) {
    // Current L2Server 0x0096dd74.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    m_data->m_nEmblemId = emblemId;
}

void CPledge::SetNewEmblem(const std::uint32_t emblemId) {
    // Current L2Server 0x00978a00.
    L2_GUARD_FUNCTION();
    SetNewEmblemId(emblemId);
    SetEmblem service(emblemId);
    ServeForAllMember(service, true);
}

std::uint32_t CPledge::GetPledgeLevel() { return m_data->m_nSkillLevel; }

int CPledge::GetMemberCount() const {
    // Current L2Server 0x0096ea20. This is the live SID-set count, not the
    // persisted member-list count used by the donor implementation.
    const int memberCount = [&] {
        L2_GUARD(*m_lock);
        const int memberCount = static_cast<int>(m_memberSidSet.size());
        return memberCount;
    }();
    return memberCount;
}

int CPledge::GatherAllSubPledgeName(CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x0097ab64 traverses the map without
    // taking m_subPledgeLock.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_subPledgeMap) {
        if (entry.first == MASTER_PLEDGE) {
            continue;
        }
        const std::u16string targetName = HostWideToTargetUtf16(_GetSubPledgeName(entry.first));
        packet.Assemble("dS", static_cast<int>(entry.first), targetName.c_str());
    }
    return packet.GetAssembledCount();
}

void CPledge::ClearRecruitInfo() {
    // Current L2Server 0x0096c3b4.
    m_nRecruitType = -1;
    m_recruitPledgeType = MASTER_PLEDGE;
}

void CPledge::SendPledgeInfo(CUserSocket* const socket) {
    // Current L2Server 0x00973858.
    L2_GUARD_FUNCTION();

    const char16_t* allianceName = u"";
    if (CAlliance* const alliance = GetAlliance()) {
        allianceName = alliance->m_data->m_sAllianceName;
    }
    socket->Send("cddSS", 0x89, m_nWorldId, Id(), m_data->m_sPledgeName, allianceName);
}

int CPledge::GetDbMemberCount() {
    L2_GUARD_FUNCTION();
    return m_memberList->Size();
}

std::uint32_t CPledge::GetMasterDbId() { return m_data->m_nMasterDbId; }

void CPledge::SetMasterDbId(const std::uint32_t masterDatabaseId) {
    // Current L2Server 0x0096b5b0.
    m_data->m_nMasterDbId = masterDatabaseId;
}

std::uint32_t CPledge::GetDbId() { return m_data->m_nDbId; }

void CPledge::SetDismissReservedTime(const std::uint32_t time) {
    // Current L2Server 0x0096bd94.
    m_data->m_nDismissReservedTime = time;
}

std::uint32_t CPledge::GetDismissReservedTime() const {
    // Current L2Server 0x0096bda0.
    return m_data->m_nDismissReservedTime;
}

std::uint32_t CPledge::GetLastDismissProposeTime() {
    // Current L2Server 0x0096bdac.
    const std::uint32_t reservedTime = m_data->m_nDismissReservedTime;
    if (reservedTime == 0) {
        return 0;
    }
    return reservedTime - static_cast<std::uint32_t>(BP::_PLEDGE_DISMISS_TIMEOUT);
}

int CPledge::GetTotalLevel() { return m_data->m_nTotalLevel; }

int CPledge::GetAllianceDbId() { return m_data->m_nAllianceDbId; }

int CPledge::GetCastleSiegeDefenceCount() { return runtime::Int32FromBits(m_data->m_CastleSiegeDefenceCount); }

bool CPledge::RequestCastleSiegeDefenceCount(const unsigned int count) {
    // Current L2Server 0x0096de48.
    L2_GUARD_FUNCTION();
    const bool hasCastle = m_pCastle != nullptr;
    if (hasCastle) {
        db.UpdateDefenceSiegeCount(*this, static_cast<int>(count));
    }
    return hasCastle;
}

bool CPledge::SetCastleSiegeDefenceCount(const unsigned int count) {
    // Current L2Server 0x0096df00.
    L2_GUARD_FUNCTION();
    if (m_pCastle == nullptr) {
        return false;
    }
    m_data->m_CastleSiegeDefenceCount = count;
    return true;
}

std::uint32_t CPledge::GetCastleDbId() { return m_data->m_nCastleDbId; }

std::uint32_t CPledge::GetResidenceId() {
    // Current L2Server 0x0096bdd0.
    std::uint32_t residenceId = m_data->m_nCastleDbId;
    if (residenceId == 0) {
        residenceId = m_data->m_nFortressDbId;
        if (residenceId == 0) {
            residenceId = m_data->m_nWorldAgitDbId;
        }
    }
    return residenceId;
}

std::uint32_t CPledge::GetWorldAgitDbId() { return m_data->m_nWorldAgitDbId; }

std::uint32_t CPledge::GetOustTime() { return m_data->m_nOustTime; }

void CPledge::SetOustTime(std::uint32_t time) { m_data->m_nOustTime = time; }

bool CPledge::HaveCastleSiegeStatus(CastleUnderSiege status) const {
    L2_GUARD_FUNCTION();

    for (const std::int32_t current : m_data->m_nNowCastleSiegeStatus) {
        if (current == static_cast<std::int32_t>(status)) {
            return true;
        }
    }
    return false;
}

bool CPledge::HaveCastleUnderSiege() const {
    // Current L2Server 0x0096b488.
    for (CCastle* const castle : m_pCastleUnderSiege) {
        if (castle != nullptr) {
            return true;
        }
    }
    return false;
}

void CPledge::SetCastleUnderSiege(CCastle* const castle, const int status) {
    // Current L2Server 0x00979338. The current unsigned bound deliberately
    // accepts residence index ten although the two published arrays contain
    // nine entries. Spell that physical adjacent-field behavior explicitly
    // so the portable owner keeps it without out-of-bounds host access.
    L2_GUARD_FUNCTION();
    const int residenceId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
    const std::uint32_t index = static_cast<std::uint32_t>(residenceId) - 1U;
    if (index >= 10U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] castle_index is wrong!! [%d]",
        //     L"Pledge.cpp",
        //     0xc49,
        //     index)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] castle_index is wrong!! [%d]",
        //     L"Pledge.cpp",
        //     0xc49,
        //     index)
        L2_LOG_ERROR(
            Log,
            "Cannot update the clan castle-siege state: residence ID %d is outside 1 through 10; no siege state is changed.",
            residenceId
        );
        return;
    }

    const int storedId = status != 0 ? residenceId : 0;
    if (index < 9U) {
        m_data->m_nNowCastleSiegeDbId[index] = storedId;
        m_data->m_nNowCastleSiegeStatus[index] = status;
        m_pCastleUnderSiege[index] = status != 0 ? castle : nullptr;
    } else {
        m_data->m_nNowCastleSiegeStatus[0] = storedId;
        m_data->m_nFortressUnderOffenceSiegeId = status;
        m_pFortressUnderDefenceSiege = status != 0 ? reinterpret_cast<CFortress*>(castle) : nullptr;
    }

    SetCastleSiege service(m_data->m_nNowCastleSiegeDbId, m_data->m_nNowCastleSiegeStatus);
    ServeForAllMember(service, true);
}

void CPledge::SetFortressUnderDefenceSiege(CFortress* const fortress) {
    // Current L2Server 0x009794f8.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr) {
        m_data->m_nFortressUnderDefenceSiegeId = 0;
        m_pFortressUnderDefenceSiege = nullptr;
    } else {
        m_data->m_nFortressUnderDefenceSiegeId =
            fortress->m_data != nullptr ? static_cast<std::uint32_t>(fortress->m_data->m_nResidenceID) : 0U;
        m_pFortressUnderDefenceSiege = fortress;
    }
    SetFortressSiege service(
        static_cast<int>(m_data->m_nFortressUnderDefenceSiegeId), static_cast<int>(m_data->m_nFortressUnderOffenceSiegeId)
    );
    ServeForAllMember(service, true);
}

void CPledge::SetFortressUnderOffenceSiege(CFortress* const fortress) {
    // Current L2Server 0x00979614.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr) {
        m_data->m_nFortressUnderOffenceSiegeId = 0;
        m_pFortressUnderOffenceSiege = nullptr;
    } else {
        m_data->m_nFortressUnderOffenceSiegeId =
            fortress->m_data != nullptr ? static_cast<std::uint32_t>(fortress->m_data->m_nResidenceID) : 0U;
        m_pFortressUnderOffenceSiege = fortress;
    }
    SetFortressSiege service(
        static_cast<int>(m_data->m_nFortressUnderDefenceSiegeId), static_cast<int>(m_data->m_nFortressUnderOffenceSiegeId)
    );
    ServeForAllMember(service, true);
}

void CPledge::InitSiegeKillDeathCountFromDb(const int killCount, const int deathCount) {
    // Current L2Server 0x0096b5a0: exact 8-byte two-field store.
    m_nSiegeKillCount = killCount;
    m_nSiegeDeathCount = deathCount;
}

void CPledge::InitSiegeKillDeathCount() {
    // Current L2Server 0x0096dfac.
    L2_GUARD_FUNCTION();
    m_nSiegeKillCount = 0;
    m_nSiegeDeathCount = 0;
    DxSaveSiegeKillDeath packet;
    packet.mPledgeDbId = static_cast<int>(m_data->m_nDbId);
    packet.mKillNo = 0;
    packet.mDeathNo = 0;
    packet.mReturn = 0;
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x1a), static_cast<int>(sizeof(packet)), packet);
}

void CPledge::UpdateSiegeKillDeathPoint(CPledge* const killerPledge, CPledge* const victimPledge) {
    // Current L2Server 0x0096e330.
    L2_GUARD_FUNCTION();
    if (killerPledge != nullptr) {
        runtime::raw_atomic::IncrementI32(&killerPledge->m_nSiegeKillCount);
        DxSaveSiegeKillDeath packet;
        packet.mPledgeDbId = static_cast<int>(killerPledge->m_data->m_nDbId);
        packet.mKillNo = killerPledge->m_nSiegeKillCount;
        packet.mDeathNo = killerPledge->m_nSiegeDeathCount;
        packet.mReturn = 0;
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x1a), static_cast<int>(sizeof(packet)), packet);
    }
    if (victimPledge != nullptr) {
        runtime::raw_atomic::IncrementI32(&victimPledge->m_nSiegeDeathCount);
        DxSaveSiegeKillDeath packet;
        packet.mPledgeDbId = static_cast<int>(victimPledge->m_data->m_nDbId);
        packet.mKillNo = victimPledge->m_nSiegeKillCount;
        packet.mDeathNo = victimPledge->m_nSiegeDeathCount;
        packet.mReturn = 0;
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x1a), static_cast<int>(sizeof(packet)), packet);
    }
}

void CPledge::SetMemberRelationChanged() {
    // Current L2Server 0x00976144.
    L2_GUARD_FUNCTION();
    SetRelationChanged service;
    ServeForAllMember(service, true);
}

void CPledge::PledgeMissionCheckAndAddCountInCastleSiege(
    const PledgeMission::GoalType goalType, const int firstValue, const int secondValue, const int count
) {
    // Current L2Server 0x00977a44.
    L2_GUARD_FUNCTION();
    PMS_PledgeMissionCheckAndAddCountInCastleSiege service(goalType, firstValue, secondValue, count);
    ServeForAllMember(service, true);
}

void CPledge::PlayMemberMusic() {
    // Current L2Server 0x00976210.
    L2_GUARD_FUNCTION();
    PlayMusic service;
    ServeForAllMember(service, true);
}

void CPledge::MemberAffectedByWar() {
    // Current L2Server 0x0097647c.
    L2_GUARD_FUNCTION();
    AffectedByWar service;
    ServeForAllMember(service, true);
}

int CPledge::GetRank() const { return CPledgeRanking::GetRanking(m_data->m_nDbId); }

bool CPledge::HasResidence() const noexcept {
    return m_pCastle != nullptr || m_pWorldAgit != nullptr || m_pFortress != nullptr;
}

bool CPledge::IsInCombatMode() {
    // Current L2Server 0x00979980.
    L2_GUARD_FUNCTION();
    CombatModeChecker checker;
    ServeForAllMember(checker, true);
    return checker.IsInCombatMode();
}

bool CPledge::IsInWar() const noexcept { return m_nDeclareCount != 0; }

bool CPledge::IsCastleSiegeEnemy(CPledge* pledge) const {
    L2_GUARD_FUNCTION();

    for (std::size_t index = 0; index < 9; ++index) {
        const std::int32_t mine = m_data->m_nNowCastleSiegeStatus[index];
        const std::int32_t theirs = pledge->m_data->m_nNowCastleSiegeStatus[index];
        if (mine != 0 && theirs != 0 && mine != theirs) {
            return true;
        }
    }
    return false;
}

bool CPledge::DidDeclareWar(int pledgeServerId) {
    L2_GUARD_FUNCTION();

    return m_mapDeclare.find(pledgeServerId) != m_mapDeclare.end();
}

bool CPledge::DidDeclareWarBoth(CPledge* const pledge) {
    // Current L2Server 0x00979b34.
    return DidDeclareWar(static_cast<int>(pledge->Id())) && pledge->DidDeclareWar(static_cast<int>(Id()));
}

int CPledge::GetCurrentDeclareWarCount() const noexcept {
    // Current L2Server 0x0096b65c.
    return m_nDeclareCount;
}

int CPledge::GetCurrentTargettedWarCount() const noexcept {
    // Current L2Server 0x0096ea64.
    return static_cast<int>(m_setTargeted.size());
}

void CPledge::ResetCastleSiege() {
    for (std::size_t index = 0; index < 9; ++index) {
        m_pCastleUnderSiege[index] = nullptr;
        m_data->m_nNowCastleSiegeDbId[index] = 0;
        m_data->m_nNowCastleSiegeStatus[index] = 0;
    }
}

bool CPledge::IsAttacker(CCastle* castle) const {
    L2_GUARD_FUNCTION();

    const int residenceId = castle != nullptr && castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
    const std::uint32_t index = static_cast<std::uint32_t>(residenceId) - 1U;
    if (index < 10U) {
        return GetCurrentCastleSiegeDbId(residenceId) == residenceId && GetCurrentCastleSiegeStatus(residenceId) == 1;
    }

    // Log.AddT(
    //     LOG_ERROR,
    //     L"[%s][%d] castle_index is wrong!! [%d]",
    //     L"Pledge.cpp",
    //     0xd90,
    //     index)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] castle_index is wrong!! [%d]",
    //     L"Pledge.cpp",
    //     0xd90,
    //     index)
    L2_LOG_ERROR(
        Log,
        "Cannot check clan attacker status: residence ID %d is outside 1 through 10; reporting the stack and returning "
        "false.",
        residenceId
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

bool CPledge::IsDefender(CCastle* castle) const {
    L2_GUARD_FUNCTION();

    const int residenceId = castle != nullptr && castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
    const std::uint32_t index = static_cast<std::uint32_t>(residenceId) - 1U;
    if (index < 10U) {
        return GetCurrentCastleSiegeDbId(residenceId) == residenceId && GetCurrentCastleSiegeStatus(residenceId) == 2;
    }

    // Log.AddT(
    //     LOG_ERROR,
    //     L"[%s][%d] castle_index is wrong!! [%d]",
    //     L"Pledge.cpp",
    //     0xda3,
    //     index)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] castle_index is wrong!! [%d]",
    //     L"Pledge.cpp",
    //     0xda3,
    //     index)
    L2_LOG_ERROR(
        Log,
        "Cannot check clan defender status: residence ID %d is outside 1 through 10; reporting the stack and returning "
        "false.",
        residenceId
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

CSPointer<CBattleCamp> CPledge::GetBattleCamp() { return m_osBattleCamp.GetObjectW(); }

void CPledge::SetBattleCamp(CBattleCamp* battleCamp) {
    // Current L2Server 0x009718a4.
    L2_GUARD_FUNCTION();
    m_data->m_nBattleCampDbId = battleCamp != nullptr ? battleCamp->GetDBID() : 0;
    m_osBattleCamp.SetObject(battleCamp);
}

int CPledge::GetCurrentCastleSiegeStatus(int castleIndex) const {
    L2_GUARD_FUNCTION();

    const std::uint32_t index = static_cast<std::uint32_t>(castleIndex) - 1U;
    if (index < 9U) {
        return m_data->m_nNowCastleSiegeStatus[index];
    }
    // Current 0096bbc4 accepts index 10 and so reads the next
    // physical dword after the nine-element status array.
    if (index == 9U) {
        return runtime::Int32FromBits(m_data->m_nFortressUnderOffenceSiegeId);
    }
    return 0;
}

int CPledge::GetCurrentCastleSiegeDbId(int castleIndex) const {
    L2_GUARD_FUNCTION();

    const std::uint32_t index = static_cast<std::uint32_t>(castleIndex) - 1U;
    if (index < 9U) {
        return m_data->m_nNowCastleSiegeDbId[index];
    }
    // Current 0096bc78 has the same inclusive-ten boundary and reads the
    // first status dword immediately following the DBID array.
    if (index == 9U) {
        return m_data->m_nNowCastleSiegeStatus[0];
    }
    return -1;
}

void CPledge::CopyCastleSiegeStatusToUser(User* const user) {
    // Current L2Server 0x0096bd34.
    if (user == nullptr) {
        return;
    }

    std::copy_n(m_data->m_nNowCastleSiegeDbId, 9, user->m_nCastleSiegeId);
    std::copy_n(m_data->m_nNowCastleSiegeStatus, 9, user->m_nCastleSiegeStatus);
}

void CPledge::GetAllMember(std::vector<User*>& members, bool append) {
    L2_GUARD_FUNCTION();

    if (!append) {
        members.clear();
    }

    L2_GUARD(*m_lock);
    for (const std::uint32_t memberSid : m_memberSidSet) {
        CCreature* const object = g_CreatureSmartIdManager.GetObjectW(memberSid);
        User* const user = object && object->IsPC() ? object->CastUser() : nullptr;
        if (user) {
            members.push_back(user);
        }
    }
}

void CPledge::TeleportToLocation(
    int x, int y, int z, int randomRange, std::uint32_t parameter, TeleportSourceType::Enum source, CCreature* creature
) {
    // Current L2Server 0x00985070.
    L2_GUARD_FUNCTION();

    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }

        const int lower = randomRange < 0 ? randomRange : 0;
        const int upper = randomRange < 0 ? 0 : randomRange;
        const int width = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(upper, lower), 1);
        const int offsetX = runtime::Win64CvttDoubleToInt32(
            ::genrand64_real2() * static_cast<double>(width) + static_cast<double>(lower)
        );
        const int offsetY = runtime::Win64CvttDoubleToInt32(
            ::genrand64_real2() * static_cast<double>(width) + static_cast<double>(lower)
        );

        member->StopMove();
        static_cast<void>(member->TeleportToLocation(
            runtime::AddInt32Wrapping(x, offsetX), runtime::AddInt32Wrapping(y, offsetY), z, parameter, 0x10000,
            source, creature, 0
        ));
    }
}

void CPledge::GetAllMemberIdSet(std::set<std::uint32_t>& memberIds) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_lock);
    for (const std::uint32_t memberSid : m_memberSidSet) {
        memberIds.insert(memberSid);
    }
}

void CPledge::GetAllMemberId(std::vector<std::uint32_t>& memberIds) {
    L2_GUARD_FUNCTION();

    memberIds.clear();
    L2_GUARD(*m_lock);
    for (const std::uint32_t memberSid : m_memberSidSet) {
        memberIds.push_back(memberSid);
    }
}

void CPledge::SendToMembers(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member) {
            member->GetSocket()->Send("b", packet.GetLength(), packet.GetPacket());
        }
    }
}

void CPledge::SendSystemMessageToMembersWithNpcId(int messageId, int npcId) {
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> packet;
    packet.Assemble("chccd", 0x62, static_cast<std::int16_t>(messageId), 1, 2, npcId);
    SendToMembers(packet);
}

void CPledge::SetAllOnlineMemberRenamed(const int dominionId) {
    // Current L2Server 0x00985c18.
    L2_GUARD_FUNCTION();

    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member != nullptr) {
            member->SetDominionRenamedID(dominionId);
        }
    }
}

void CPledge::RemovePledgeSkillsFrom(User* const user) {
    // Current L2Server 0x0096b684.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x147b,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x147b,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot remove clan skills: the user is unavailable.");
        return;
    }

    m_pSkill->RemoveSkillsFrom(user);
    user->ValidateChanged(VB_MAX);
    user->SetMaxMagicLevel();
    user->m_nParamChanged |= PCB_USER;
}

void CPledge::RemovePledgeSkillsFromMembers() {
    // Current L2Server 0x00980d10.
    L2_GUARD_FUNCTION();
    std::set<std::uint32_t> memberIds;
    {
        L2_GUARD(*m_lock);
        memberIds = m_memberSidSet;
    }
    for (const std::uint32_t memberSid : memberIds) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(memberSid);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            RemovePledgeSkillsFrom(user);
        }
    }
}

void CPledge::ResetAllNickName() {
    // Current L2Server 0x0096fd4c.
    L2_GUARD_FUNCTION();
    m_memberList->ResetAllNickName(m_nWorldId);
}

void CPledge::AddPledgeSkillByEliteTo(User* const member) {
    // Current L2Server 0x00971d60.
    L2_GUARD_FUNCTION();
    if (member == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x150b,
        //     L"(pMember != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x150b,
        //     L"(pMember != NULL)")
        L2_LOG_ERROR(Log, "Cannot apply the clan advent skill: the member is unavailable.");
        return;
    }

    if (!m_pFunction->IsEnable(PledgeFunctionType::CLAN_ADVENT)) {
        return;
    }

    User* const elite = GetMasterOrAnyElite();
    if (elite == nullptr) {
        return;
    }

    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(g_nClanAdventSkill, 1), nullptr);
    if (skill != nullptr && skill->ActivateSkill(elite, member, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false)) {
        member->m_nTimeActivatePledgeSkill = static_cast<std::int32_t>(runtime::TickCount32());
    }
}

void CPledge::RemovePledgeSkillByEliteFrom(User* member) {
    // Current L2Server 0x0097d924.
    L2_GUARD_FUNCTION();
    if (member == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x1564,
        //     L"(pMember != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"Pledge.cpp",
        //     0x1564,
        //     L"(pMember != NULL)")
        L2_LOG_ERROR(Log, "Cannot remove the clan advent skill: the member is unavailable.");
        return;
    }

    L2_GUARD(member->m_InfectedSkillsSect);
    for (std::size_t index = 0; index < member->m_InfectedSkills.size(); ++index) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(member->m_InfectedSkills, index);
        if (infected.pSkillInfo == nullptr || infected.pSkillInfo->m_nSkillId != g_nClanAdventSkill) {
            continue;
        }

        DeleteAbnormalStatusFromSummonsOf(member, g_nClanAdventSkill);
        member->_DeleteAbnormalStatus(static_cast<int>(index), member, infected.pSkillInfo, true, SPET_DISPEL, true);
        member->m_nTimeActivatePledgeSkill = 0;
        return;
    }
}

bool CPledge::IsSubMaster(User* const user, const PledgeType pledgeType) {
    // Current L2Server 0x0097a588.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    const auto found = m_subPledgeMap.find(pledgeType);
    if (found == m_subPledgeMap.end()) {
        return false;
    }
    return found->second->m_masterDbId == user->GetDBID();
}

bool CPledge::IsSubMaster(const std::uint32_t userDatabaseId) {
    // Current L2Server 0x009766c4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    for (const auto& entry : m_subPledgeMap) {
        if (entry.second != nullptr && entry.second->m_masterDbId == userDatabaseId) {
            return true;
        }
    }
    return false;
}

PledgeType CPledge::GetSubMasterPledgeType(const std::uint32_t userDatabaseId) {
    // Current L2Server 0x00976dac.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    for (const auto& entry : m_subPledgeMap) {
        if (entry.second != nullptr && entry.second->m_masterDbId == userDatabaseId) {
            return entry.second->m_pledgeType;
        }
    }
    return static_cast<PledgeType>(0);
}

PledgeType CPledge::ChangeMemberPledgeType(const std::uint32_t characterDatabaseId, const PledgeType pledgeType) {
    // Current L2Server 0x00976eec.
    L2_GUARD_FUNCTION();
    const PledgeType previousType = m_memberList->ChangeMemberPledgeType(characterDatabaseId, pledgeType);
    PMS_SyncPledgeType service(characterDatabaseId, pledgeType);
    ServeForAllMember(service, true);
    return previousType;
}

void CPledge::UpdatePledgeMemberListPacket(const std::uint32_t userDatabaseId) {
    // Current L2Server 0x00976c18.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    packet.Assemble("c", 0x5b);
    if (m_memberList->GatherMemberInfo(userDatabaseId, packet)) {
        GenericService service(&packet);
        ServeForAllMember(service, true);
    }
}

void CPledge::UpdatePledgeInfoPacket() {
    // Current L2Server 0x00978adc.
    L2_GUARD_FUNCTION();
    SetParamChanged service(PCB_PLEDGE);
    ServeForAllMember(service, true);
}

void CPledge::DeleteDbMember(const std::uint32_t userDatabaseId) {
    // Current L2Server 0x00978eb0.
    L2_GUARD_FUNCTION();
    if (m_memberList->Remove(userDatabaseId)) {
        --m_data->m_nTotalMemberCount;
        UpdatePledgeMemberListPacket(userDatabaseId);
    }
}

void CPledge::DeleteMember(User* const user) {
    // Current L2Server 0x0097c6dc.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    const std::uint32_t userId = user->Id();
    const auto member = m_memberSidSet.find(userId);
    if (member != m_memberSidSet.end()) {
        m_memberSidSet.erase(member);
    }

    for (IdIndexPair& onlineMember : m_data->m_OnlineMember) {
        if (onlineMember.id != userId) {
            continue;
        }

        onlineMember.id = 0;
        onlineMember.index = static_cast<std::uint32_t>(-1);
        --m_data->m_nOnlineMemberCount;

        if (user->m_data->m_bIsRelated && user->m_data->m_nRelatedIndex != 0) {
            const int relatedSmartId =
                CCreatureFactory::Inst().GetIdByIndex(static_cast<int>(user->m_data->m_nRelatedIndex));
            CCreature* const relatedCreature =
                g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(relatedSmartId));
            User* const relatedUser =
                relatedCreature != nullptr && relatedCreature->IsPC() ? relatedCreature->CastUser() : nullptr;
            if (relatedUser != nullptr) {
                user->m_data->m_nRelatedIndex = 0;
                user->m_data->m_bIsRelated = false;
                relatedUser->m_data->m_nRelatedIndex = 0;
                relatedUser->m_data->m_bIsRelated = false;
            }
        }
        break;
    }
}

void CPledge::AddDbMember(
    const std::uint32_t characterDatabaseId,
    const char16_t* const characterName,
    const char16_t* const nickname,
    const int level,
    const int classId,
    const int gender,
    const int race,
    const std::uint32_t serverId,
    const PledgeType pledgeType,
    const int gradeId,
    const int missionCheckTime
) {
    // Current L2Server 0x0096e084.
    L2_GUARD_FUNCTION();
    if (m_memberList->Add(
            characterDatabaseId, characterName, nickname, level, classId, gender, race, serverId, pledgeType, gradeId,
            missionCheckTime
        )) {
        ++m_data->m_nTotalMemberCount;
    }
}

void CPledge::AddDbMember(User* const user) {
    // Current L2Server 0x0096e1e8.
    L2_GUARD_FUNCTION();
    const char16_t* const nickname = user->m_sNickName;
    AddDbMember(
        user->GetDBID(), user->GetName(), nickname, user->m_data->m_nLevel, user->m_data->m_nClass,
        static_cast<int>(user->m_data->m_Sex), static_cast<int>(user->m_data->m_Race), user->GetServerId(),
        static_cast<PledgeType>(user->m_data->m_nPledgeType), user->m_data->m_nGradeId,
        user->tPledgeBonusTimeStamp[PledgeBonus::ACCESS_MISSION_COMPLETE]
    );
}

bool CPledge::ChangeMemberName(const std::uint32_t characterDatabaseId, wchar_t* const name) {
    // Current L2Server 0x0096e488.
    L2_GUARD_FUNCTION();
    return m_memberList->ChangeMemberName(characterDatabaseId, name);
}

void CPledge::SetNickName(const std::uint32_t characterDatabaseId, wchar_t* const nickname) {
    // Current L2Server 0x0096fc54.
    L2_GUARD_FUNCTION();
    m_memberList->ChangeMemberNickName(characterDatabaseId, nickname);
}

const unsigned char* CPledge::AddMemberByDisassemble(
    const PledgeType pledgeType, const int memberCount, const unsigned char* packet
) {
    // Current L2Server 0x0096ece8.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = packet;
    for (int index = 0; index < memberCount; ++index) {
        std::int32_t characterDatabaseId;
        std::array<char16_t, 28> characterName;
        std::array<char16_t, 28> nickname;
        std::int32_t level;
        std::int32_t classId;
        std::int32_t gender;
        std::int32_t race;
        std::int32_t gradeId;
        std::int32_t missionCheckTime;
        cursor = ::Disassemble(
            cursor, "dSSdddddd", &characterDatabaseId, 0x32, characterName.data(), 0x32, nickname.data(), &level, &classId,
            &gender, &race, &gradeId, &missionCheckTime
        );

        AddDbMember(
            static_cast<std::uint32_t>(characterDatabaseId), characterName.data(), nickname.data(), level, classId, gender,
            race, 0, pledgeType, gradeId, missionCheckTime
        );
    }
    return cursor;
}

void CPledge::Online(User* const user) {
    // Current L2Server 0x00978d8c.
    L2_GUARD_FUNCTION();
    m_memberList->Online(user);
    UpdateMemberList service(user, true);
    ServeForAllMember(service, true);
}

void CPledge::SetMaster(User* const user) {
    // Current L2Server 0x0096ab80.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        m_data->m_OnlineMaster.id = 0;
        m_data->m_OnlineMaster.index = UINT32_MAX;
        return;
    }
    m_data->m_OnlineMaster.id = user->Id();
    m_data->m_OnlineMaster.index = static_cast<std::uint32_t>(user->GetIndex());
}

void CPledge::SetAcademyMasterStudentRelationship(User* const user) {
    // Current L2Server 0x0097c3b0.
    L2_GUARD_FUNCTION();

    const std::uint32_t relatedDatabaseId = user->m_data->m_nPledgeType == -1
        ? m_memberList->GetAcademyMasterDbId(user->GetDBID())
        : m_memberList->GetAcademyStudentDbId(user->GetDBID());
    if (relatedDatabaseId == 0) {
        return;
    }

    user->m_data->m_bIsRelated = true;
    CSPointer<User> related =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(relatedDatabaseId)));
    if (!related) {
        return;
    }

    user->m_data->m_nRelatedIndex = static_cast<std::uint32_t>(related->m_data->index);
    related->m_data->m_nRelatedIndex = static_cast<std::uint32_t>(user->m_data->index);
    related->m_data->m_bIsRelated = true;
    related->GetSocket()->SendSystemMessage_Char(
        user->m_data->m_nPledgeType == -1 ? SystemMessageId::CLAN_ACADEMY_APPRENTICE_LOGGED_IN
                                          : SystemMessageId::CLAN_ACADEMY_SPONSOR_LOGGED_IN,
        user->GetName()
    );
}

void CPledge::AddMember(User* const user) {
    // Current L2Server 0x0098071c.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    const std::uint32_t userId = user->Id();
    m_memberSidSet.insert(userId);
    for (const IdIndexPair& member : m_data->m_OnlineMember) {
        if (member.id == userId) {
            return;
        }
    }
    bool overflow = true;
    for (IdIndexPair& member : m_data->m_OnlineMember) {
        if (member.id == 0) {
            member.id = userId;
            member.index = static_cast<std::uint32_t>(user->m_data->index);
            ++m_data->m_nOnlineMemberCount;
            overflow = false;
            break;
        }
    }
    SetAcademyMasterStudentRelationship(user);
    if (overflow) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CPledge[%d]::AddMember[%s]-> Overflow",
        //     m_data->m_nDbId,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"CPledge[%d]::AddMember[%s]-> Overflow",
        //     m_data->m_nDbId,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "Clan %u has no free online-member slot for '%s'. The member object was already added to the clan set.",
        //     static_cast<unsigned>(m_data->m_nDbId), WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Clan %u has no free online-member slot for '%s'; the member object was already added to the clan set.",
            static_cast<unsigned>(m_data->m_nDbId), Utf16ToUtf8(user->GetName()).c_str()
        );
    }
}

void CPledge::SetPledgeMember(User* const user) {
    // Current L2Server 0x00980c1c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    AddMember(user);
    user->SetPledge(this);
    if (user->GetDBID() == m_data->m_nMasterDbId) {
        SetMaster(user);
    }
    user->m_nParamChanged |= std::int64_t{PCB_PLEDGE | PCB_USER};
    runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
}

void CPledge::SetAcademyMember(
    const std::uint32_t characterDatabaseId, const int joinLevel, const std::uint32_t masterDatabaseId
) {
    // Current L2Server 0x0096ec64.
    m_memberList->AddAcademyMember(characterDatabaseId, joinLevel, masterDatabaseId);
}

void CPledge::Join(User* const user) {
    // Current L2Server 0x0098186c.
    L2_GUARD_FUNCTION();
    AddDbMember(user);
    AddMember(user);
    if (user->m_data->m_nPledgeType == ACADEMY) {
        SetAcademyMember(user->GetDBID(), user->m_data->m_nLevel, 0);
    }
    user->SetPledge(this);
    SendPledgeSkillsTo(user);
    ApplyPledgeSkillsTo(user, false, false);
    user->BroadcastCharInfoUpdate(false);
    user->m_nParamChanged |= std::int64_t{PCB_PLEDGE};
    runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);

    ::SystemMessage_S<SameMemberCheck, User> joined(
        SystemMessageId::NEW_MEMBER_JOINED_CLAN, EST_CHAR_NAME, user->GetName(), user
    );
    ServeForAllMember(joined, true);

    AddWindowPacket addWindow(user);
    ServeForAllMember(addWindow, true);
    OpenAllWindowPacket(user, false, nullptr);
    SendPledgeOnlineMemberCount();
    ValidateTotalLevel();
    PledgeRaidSystem::inst().CheckPledgeRaidInzoneBinding(user);
    user->PledgeMissionLoadingCompleted();
}

void CPledge::AcquireSkillForAcademy(const std::uint32_t characterDatabaseId, const bool notify) {
    // Current L2Server 0x0096f43c.
    L2_GUARD_FUNCTION();
    m_memberList->AcquireSkillForAcademy(true, characterDatabaseId, notify);
}

void CPledge::DeleteSkillForAcademy(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x0096f538.
    L2_GUARD_FUNCTION();
    m_memberList->AcquireSkillForAcademy(false, characterDatabaseId, false);
}

void CPledge::ClearAllSkillForAcademy() {
    // Current L2Server 0x0096f624.
    L2_GUARD_FUNCTION();
    m_memberList->ClearAllSkillForAcademy();
}

void CPledge::TakeOustPenalty() {
    // Current L2Server 0x009737a0.
    L2_GUARD_FUNCTION();
    m_data->m_nOustTime = static_cast<std::uint32_t>(runtime::crt::Time32Now());
    Save(4);
}

bool CPledge::SetAcademyMaster(const char16_t* const characterName, const char16_t* const masterName) {
    // Current L2Server 0x0096f344.
    L2_GUARD_FUNCTION();
    return m_memberList->SetAcademyMaster(characterName, masterName);
}

std::uint32_t CPledge::SetPledgePowerGrade(wchar_t* const characterName, const int gradeId) {
    // Current L2Server 0x0096f7f0.
    L2_GUARD_FUNCTION();
    return m_memberList->SetPledgePowerGrade(characterName, gradeId);
}

std::uint32_t CPledge::GetMemberDbId(wchar_t* const characterName) {
    // Current L2Server 0x0096fe74.
    L2_GUARD_FUNCTION();
    return m_memberList->GetCharDbId(characterName);
}

bool CPledge::IsPledgeTypeMember(wchar_t* const characterName, const PledgeType pledgeType) {
    // Current L2Server 0x00970128.
    L2_GUARD_FUNCTION();
    return m_memberList->GetPledgeType(GetMemberDbId(characterName)) == pledgeType;
}

bool CPledge::CanBeAcademyMaster(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x0096fab0.
    L2_GUARD_FUNCTION();
    return m_memberList->CanBeAcademyMaster(characterDatabaseId);
}

std::uint32_t CPledge::GetAcademyMasterDbId(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x0096fb94.
    return m_memberList->GetAcademyMasterDbId(characterDatabaseId);
}

wchar_t* CPledge::GetMemberName(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x0096ff5c.
    L2_GUARD_FUNCTION();
    return m_memberList->GetCharName(characterDatabaseId);
}

int CPledge::GetPledgePowerGrade(wchar_t* const characterName) {
    // Current L2Server 0x0096f708.
    L2_GUARD_FUNCTION();
    return m_memberList->GetGradeByCharName(characterName);
}

void CPledge::SendPledgPowerListPacket(User* const user) {
    // Current L2Server 0x0097236c.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    m_memberList->GetGradeIdCountPacket(packet);
    user->GetSocket()->Send("chdb", 0xfe, 0x3d, 9, packet.GetLength(), packet.GetPacket());
}

void CPledge::ViewPledgPowerPacket(User* const user, const PledgeType pledgeType, wchar_t* const characterName) {
    // Current L2Server 0x00973b60. The decoded pledge type is
    // unused by the current owner.
    L2_GUARD_FUNCTION();
    static_cast<void>(pledgeType);
    const int gradeId = GetPledgePowerGrade(characterName);
    const std::uint32_t gradeIndex = static_cast<std::uint32_t>(gradeId) - 1U;
    const std::uint32_t power = gradeIndex < 9U ? m_data->m_gradePowerList[gradeIndex] : 0U;
    user->GetSocket()->Send(
        "chdSd", 0xfe, 0x3e, gradeId, target_utf16::FromRecoveredWindowsWide(characterName).c_str(), power
    );
}

void CPledge::ViewPledgeMemberInfo(User* const user, const PledgeType pledgeType, wchar_t* const characterName) {
    // Current L2Server 0x009724e0.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    if (m_memberList->GetMemberInfo(packet, pledgeType, characterName)) {
        user->GetSocket()->Send("chb", 0xfe, 0x3f, packet.GetLength(), packet.GetPacket());
    }
}

void CPledge::ViewPledgeMemberInfo(User* const user, wchar_t* const characterName) {
    // Current L2Server 0x00972660.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    if (m_memberList->GetMemberInfo(packet, characterName)) {
        user->GetSocket()->Send("chb", 0xfe, 0x3f, packet.GetLength(), packet.GetPacket());
    }
}

void CPledge::ViewPledgeMemberInfo(wchar_t* const characterName) {
    // Current L2Server 0x00976548.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    if (m_memberList->GetMemberInfo(packet, characterName)) {
        ::ViewPledgeMemberInfo service(&packet);
        ServeForAllMember(service, true);
    }
}

void CPledge::GraduateAcademy(User* const user) {
    // Current L2Server 0x0096bfa0.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nPledgeType == -1) {
        db.RequestGraduateAcademy(*this, user);
    }
}

void CPledge::ReplyGraduateAcademy(User* const user) {
    // Current L2Server 0x00980174.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nPledgeType != ACADEMY) {
        return;
    }

    DeleteSkillForAcademy(GetAcademyMasterDbId(user->GetDBID()));
    DeleteDbMember(user->GetDBID());
    DeleteMember(user);
    RemovePledgeSkillsFrom(user);
    user->SetPledge(nullptr);
    user->m_nParamChanged |= std::int64_t{PCB_PLEDGE | PCB_USER};
    runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
    user->SendSystemMessage(SystemMessageId::CLAN_ACADEMY_GRADUATION_AND_IMMEDIATE_REJOIN_NOTICE);
    user->GetSocket()->Send("chdddddddddddS", 0xfe, 0x39, 0, 0x6d5, 2, 0, 0, 0, 1, 0, 5000, 0, -1, u"");

    ::SystemMessage_S<NoCheck, User> graduated(SystemMessageId::MEMBER_LEFT_CLAN, EST_CHAR_NAME, user->GetName());
    ServeForAllMember(graduated, true);

    CloseWindowPacket closeWindow(user->GetName());
    ServeForAllMember(closeWindow, true);
    user->CloseAllPledgeWindowPacket();
    db.RequestResetUserNickname(m_nWorldId, user->GetDBID());
    SendPledgeOnlineMemberCount();
    ValidateTotalLevel();
}

bool CPledge::HasPledgeType(const PledgeType pledgeType) {
    // Current L2Server 0x0097e1a4.
    L2_GUARD_FUNCTION();
    return HasSubPledgeType(pledgeType);
}

bool CPledge::HasSubPledgeType(const PledgeType pledgeType) {
    // Current L2Server 0x0097a7c8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    return m_subPledgeMap.find(pledgeType) != m_subPledgeMap.end();
}

void CPledge::OpenAllWindowPacket(User* const user, const bool update, const char16_t* const targetName) {
    // Current L2Server 0x0097f4d0.
    L2_GUARD_FUNCTION();

    std::uint32_t allianceId = 0;
    const char16_t* allianceName = u"";
    std::uint32_t allianceCrestId = 0;
    int isNotInPeace = 0;
    if (CAlliance* const alliance = GetAlliance()) {
        CSharedAllianceData* const allianceData = alliance->m_data;
        allianceId = allianceData->m_nDbId;
        allianceName = allianceData->m_sAllianceName;
        allianceCrestId = allianceData->m_nCrestId;
        for (const EnemyWarPair& war : allianceData->m_EnemyWarPair) {
            if (war.nEnemyId != 0) {
                isNotInPeace = 1;
                break;
            }
        }
    }

    int residenceId = static_cast<int>(m_data->m_nWorldAgitDbId);
    int isInstantResidence = 0;
    if (residenceId == 0) {
        CInstantAgit* const instantAgit = CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(m_data->m_nDbId));
        if (instantAgit != nullptr) {
            residenceId = instantAgit->GetInstantZoneTypeId();
            isInstantResidence = 1;
        }
    }

    if (isNotInPeace == 0 && m_nDeclareCount != 0) {
        isNotInPeace = 1;
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble(
        "dddddddddddSddd", m_data->m_nCrestId, m_data->m_nSkillLevel, m_data->m_nCastleDbId, isInstantResidence, residenceId,
        m_data->m_nFortressDbId, CPledgeRanking::GetRanking(m_data->m_nDbId), GetNameValue(), m_data->m_nStatus,
        m_data->m_bIsGuilty ? 1 : 0, allianceId, allianceName, allianceCrestId, isNotInPeace, m_data->m_nDominionSiegeId
    );

    {
        L2_GUARD(m_subPledgeLock);
        for (auto current = m_subPledgeMap.begin(); current != m_subPledgeMap.end();) {
            auto next = current;
            ++next;
            _OpenAllWindowPacket(user, update, targetName, current->first, packet, next != m_subPledgeMap.end());
            current = next;
        }
    }
    PledgeStuff::Send_S_EX_PLEDGE_SHOW_INFO_UPDATE(user, this);
}

void CPledge::_OpenAllWindowPacket(
    User* const user,
    const bool update,
    const char16_t* const targetName,
    const PledgeType pledgeType,
    CPacketHelperEx<20480>& packet,
    const bool hasNext
) {
    // Current L2Server 0x0097be00.
    L2_GUARD_FUNCTION();
    MemberInfo memberInfo;
    m_memberList->GatherMemberInfo(memberInfo, pledgeType);

    CUserSocket* const socket = user->GetSocket();
    const wchar_t* const subPledgeMasterName = _GetSubPledgeMasterName(pledgeType);
    const wchar_t* const subPledgeName = _GetSubPledgeName(pledgeType);
    const std::u16string targetSubPledgeMasterName = HostWideToTargetUtf16(subPledgeMasterName);
    const std::u16string targetSubPledgeName = HostWideToTargetUtf16(subPledgeName);

    if (!update) {
        socket->Send(
            "cddddSSbdb", 0x5a, hasNext ? 1 : 0, Id(), m_nWorldId, static_cast<int>(pledgeType), targetSubPledgeName.c_str(),
            targetSubPledgeMasterName.c_str(), packet.GetLength(), packet.GetPacket(), memberInfo.GetMemberCount(),
            memberInfo.GetLength(), memberInfo.GetPacket()
        );
        return;
    }

    socket->Send(
        "cdSddSSbdb", 0x96, hasNext ? 1 : 0, targetName, Id(), static_cast<int>(pledgeType), targetSubPledgeName.c_str(),
        targetSubPledgeMasterName.c_str(), packet.GetLength(), packet.GetPacket(), memberInfo.GetMemberCount(),
        memberInfo.GetLength(), memberInfo.GetPacket()
    );
}

void CPledge::OpenAllWindowPacket() {
    // Current L2Server 0x00978bb0.
    ::OpenAllWindowPacket service(this);
    ServeForAllMember(service, true);
}

void CPledge::SendPledgeSkillsTo(User* const user) {
    // Current L2Server 0x009727e0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_pSkill == nullptr) {
        return;
    }
    CPacketHelperEx<20480> packet;
    int pledgeSkillCount = 0;
    m_pSkill->Assemble(packet, pledgeSkillCount);
    CSendPledgeSkillList service(pledgeSkillCount, 0, &packet);
    service.Serve(user);
}

void CPledge::SendPledgeSkillsToMembers() {
    // Current L2Server 0x0097a070.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    int pledgeSkillCount = 0;
    m_pSkill->Assemble(packet, pledgeSkillCount);
    CSendPledgeSkillList service(pledgeSkillCount, 0, &packet);
    ServeForAllMember(service, true);
}

void CPledge::AcademyOfflineMsg(User* const user) {
    // Current L2Server 0x009734bc.
    L2_GUARD_FUNCTION();
    if (user == nullptr || !user->m_data->m_bIsRelated || user->m_data->m_nRelatedIndex == 0) {
        return;
    }
    const int relatedId = CCreatureFactory::Inst().GetIdByIndex(static_cast<int>(user->m_data->m_nRelatedIndex));
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(relatedId));
    User* const related = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (related != nullptr && related->GetSocket() != nullptr) {
        related->GetSocket()->SendSystemMessage_S(
            user->m_data->m_nGradeId == -1 ? SystemMessageId::CLAN_ACADEMY_APPRENTICE_LOGGED_OUT
                                           : SystemMessageId::CLAN_ACADEMY_SPONSOR_LOGGED_OUT,
            user->GetName()
        );
    }
}

void CPledge::Offline(User* const user) {
    // Current L2Server 0x00978c00.
    L2_GUARD_FUNCTION();
    m_memberList->Offline(user);
    UpdateMemberList service(user, false);
    ServeForAllMember(service, true);
    if (m_nWorldId != g_nWorldId && m_memberList->Size() == 0) {
        RaidServerManager::inst().DoNotRelayForPledge(*this);
    }
}

void CPledge::SendPledgeOnlineMemberCount() {
    // Current L2Server 0x009770f0.
    L2_GUARD_FUNCTION();
    CSendPledgeOnlineMemberCount service(m_memberList->GetOnlineMemberCount());
    ServeForAllMember(service, true);
}

bool CPledge::HasAcademyMaster(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x0096f8e8.
    L2_GUARD_FUNCTION();
    return m_memberList->HasAcademyMaster(characterDatabaseId);
}

bool CPledge::HasAcademyStudent(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x0096f9cc.
    L2_GUARD_FUNCTION();
    return m_memberList->HasAcademyStudent(characterDatabaseId);
}

void CPledge::ValidateTotalLevel() {
    // Current L2Server 0x0097c1e8.
    L2_GUARD_FUNCTION();
    const int totalLevel = m_memberList->GetTotalLevel();
    if (m_data->m_nTotalLevel != totalLevel) {
        m_data->m_nTotalLevel = totalLevel;
        UpdateInfoWindowPacket();
    }
}

void CPledge::AddSubPledgeTypeBits(const PledgeType pledgeType) {
    // Current L2Server 0x0097217c.
    L2_GUARD_FUNCTION();
    const std::uint32_t position = g_PledgeTypeValidator.GetIndex(pledgeType);
    if (position >= 8U) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    m_data->m_subPledgeTypeBits |= std::uint32_t{1} << position;
}

void CPledge::SetSubPledge(
    const PledgeType pledgeType,
    wchar_t* const subPledgeName,
    const std::uint32_t masterDatabaseId,
    const bool memberCountUpgrade
) {
    // Current L2Server 0x0097dc04.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    if (m_subPledgeMap.find(pledgeType) != m_subPledgeMap.end()) {
        return;
    }

    m_subPledgeMap.emplace(pledgeType, new CSubPledge(pledgeType, subPledgeName, masterDatabaseId, memberCountUpgrade));
    AddSubPledgeTypeBits(pledgeType);
    _SetSubPledgeMasterName(pledgeType, masterDatabaseId);
}

void CPledge::RenameSubpledge(const PledgeType pledgeType, wchar_t* const newName) {
    // Current L2Server 0x0097a8d8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    const auto found = m_subPledgeMap.find(pledgeType);
    if (found != m_subPledgeMap.end()) {
        AssignTargetSubPledgeName(found->second->m_subPledgeName, newName);
    }
}

CSubPledge* CPledge::GetSubPledge(const PledgeType pledgeType) {
    // Current L2Server 0x0097aa24.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    const auto found = m_subPledgeMap.find(pledgeType);
    return found != m_subPledgeMap.end() ? found->second : nullptr;
}

wchar_t* CPledge::_GetSubPledgeName(const PledgeType pledgeType) {
    // Current L2Server 0x0097a344. The public wrapper owns the lock.
    L2_GUARD_FUNCTION();
    const auto found = m_subPledgeMap.find(pledgeType);
    if (found == m_subPledgeMap.end() || found->second == nullptr) {
        return const_cast<wchar_t*>(L"");
    }
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return reinterpret_cast<wchar_t*>(const_cast<char16_t*>(found->second->m_subPledgeName.c_str()));
#else
    return TargetSubPledgeNameToHostWide(found->second->m_subPledgeName);
#endif
}

wchar_t* CPledge::_GetSubPledgeMasterName(const PledgeType pledgeType) {
    // Current L2Server 0x0097a440. The public wrapper owns the lock.
    L2_GUARD_FUNCTION();
    const auto found = m_subPledgeMap.find(pledgeType);
    if (found == m_subPledgeMap.end() || found->second == nullptr) {
        return const_cast<wchar_t*>(L"");
    }
    return m_memberList->GetCharName(found->second->m_masterDbId);
}

void CPledge::_SetSubPledgeMasterName(const PledgeType pledgeType, const std::uint32_t masterDatabaseId [[maybe_unused]]) {
    // Current L2Server 0x0097a6f0.
    L2_GUARD_FUNCTION();
    const wchar_t* const masterName = _GetSubPledgeMasterName(pledgeType);
    const std::uint32_t index = g_PledgeTypeValidator.GetIndex(pledgeType);
    CopyTargetSubPledgeMasterName(m_data->m_subpledgeMasterName[index], masterName);
}

void CPledge::UpdateSubPledgeMaster(const PledgeType pledgeType, const std::uint32_t masterDatabaseId) {
    // Current L2Server 0x0097e3b8.
    L2_GUARD_FUNCTION();

    std::uint32_t previousMasterDatabaseId = 0;
    bool found = false;
    {
        L2_GUARD(m_subPledgeLock);
        const auto subPledge = m_subPledgeMap.find(pledgeType);
        if (subPledge != m_subPledgeMap.end() && subPledge->second != nullptr) {
            previousMasterDatabaseId = subPledge->second->m_masterDbId;
            subPledge->second->m_masterDbId = masterDatabaseId;
            _SetSubPledgeMasterName(pledgeType, masterDatabaseId);
            found = true;
        }
    }

    if (!found) {
        return;
    }

    if (previousMasterDatabaseId > 0) {
        CSPointer<User> previousMaster =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(previousMasterDatabaseId)));
        if (previousMaster) {
            previousMaster->TakeOffItems();
            ApplyPledgeSkillsTo(previousMaster.get(), true, false);
            previousMaster->m_nParamChanged |= std::int64_t{PCB_USER};
        }
    }

    if (masterDatabaseId != 0) {
        CSPointer<User> newMaster =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(masterDatabaseId)));
        if (newMaster) {
            ApplyPledgeSkillsTo(newMaster.get(), true, false);
            newMaster->m_nParamChanged |= std::int64_t{PCB_USER};
        }
    }
}

wchar_t* CPledge::GetSubPledgeName(const PledgeType pledgeType) {
    // Current L2Server 0x0097ddbc.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    return _GetSubPledgeName(pledgeType);
}

wchar_t* CPledge::GetSubPledgeMasterName(const PledgeType pledgeType) {
    // Current L2Server 0x0097de98.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_subPledgeLock);
    return _GetSubPledgeMasterName(pledgeType);
}

void CPledge::SendSubPledgeUpdatedPacket(const PledgeType pledgeType) {
    // Current L2Server 0x0097e248. Keep the target packet's two S fields
    // UTF-16LE on hosts whose wchar_t is four bytes.
    L2_GUARD_FUNCTION();
    const wchar_t* const masterName = GetSubPledgeMasterName(pledgeType);
    const std::u16string targetMasterName = WideToUtf16(std::wstring_view(masterName));
    const wchar_t* const subPledgeName = GetSubPledgeName(pledgeType);
    const std::u16string targetSubPledgeName = WideToUtf16(std::wstring_view(subPledgeName));

    CPacketHelperEx<20480> packet;
    packet.Assemble("ddSS", Id(), static_cast<int>(pledgeType), targetSubPledgeName.c_str(), targetMasterName.c_str());
    SubPledgeUpdated service(&packet);
    ServeForAllMember(service, true);
}

std::uint32_t CPledge::GetRemainSubPledgeTypeBits() {
    // Current L2Server 0x009739b4.
    const std::uint32_t result = [&] {
        L2_READ_GUARD(m_remainedSubPledgeTypeBitsLock);
        const std::uint32_t result = m_remainedSubPledgeTypeBits;
        return result;
    }();
    return result;
}

bool CPledge::HasMoreSubPledgeToLoad() {
    // Current L2Server 0x0096f0f0.
    const bool result = [&] {
        L2_READ_GUARD(m_remainedSubPledgeTypeBitsLock);
        const bool result = m_remainedSubPledgeTypeBits != 0;
        return result;
    }();
    return result;
}

void CPledge::RequestLoadSubPledge() {
    // Current L2Server 0x00971ecc.
    L2_GUARD_FUNCTION();
    if (!HasMoreSubPledgeToLoad()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] ::RequestLoadSubPledge LoadPledge "
        //     L"SubPledgeXX PledgeDBID[%d]",
        //     m_data->m_nDbId)
        // Noise: No subclan types remain to load. The extra request exits normally.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] ::RequestLoadSubPledge LoadPledge "
        //     L"SubPledgeXX PledgeDBID[%d]",
        //     m_data->m_nDbId)
        return;
    }
    db.RequestLoadSubPledge(*this);
}

bool CPledge::HasLoadedAllData() {
    // Current L2Server 0x00971fa4.
    L2_GUARD_FUNCTION();
    return m_pMastery->IsLoaded() && m_pItem->m_bLoadedFromDB && !m_bSkillLoading && !HasMoreSubPledgeToLoad();
}

void CPledge::SubPledgeLoadingCompleted(const PledgeType pledgeType) {
    // Current L2Server 0x00972078.
    L2_GUARD_FUNCTION();
    const bool invalidPosition = [&] {
        L2_WRITE_GUARD(m_remainedSubPledgeTypeBitsLock);
        const std::uint32_t position = g_PledgeTypeValidator.GetIndex(pledgeType);
        if (position >= 8U) {
            return true;
        }
        m_remainedSubPledgeTypeBits &= ~(std::uint32_t{1} << position);
        return false;
    }();
    if (invalidPosition) {
        throw std::out_of_range("invalid bitset<N> position");
    }
}

void CPledge::RegisterPledge(CSPointer<CPledge> pledge) {
    // Current L2Server 0x00989408.
    if (!pledge) {
        return;
    }

    const std::u16string loweredName = LowerPledgeNameKey(pledge->GetPledgeName());
    s_pledgeNameMap.Insert(pledge->m_nWorldId, loweredName, pledge.get());

    const std::CDualKey<int, int> key(pledge->m_nWorldId, static_cast<int>(pledge->m_data->m_nDbId));
    if (!s_pledgeDBMap.Insert(key, pledge.get(), const_cast<char*>("Pledge.cpp"), 0x1397)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d],[PledgeWar], CPledge::RegisterPledge, "
        //     L"insert failed, %u, %s",
        //     L"Pledge.cpp",
        //     0x139f,
        //     pledge->m_data->m_nDbId,
        //     pledge->GetPledgeName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d],[PledgeWar], CPledge::RegisterPledge, "
        //     L"insert failed, %u, %s",
        //     L"Pledge.cpp",
        //     0x139f,
        //     pledge->m_data->m_nDbId,
        //     pledge->GetPledgeName())
        L2_LOG_ERROR(
            Log,
            "Cannot register clan '%s' (ID %u, world %d) in the database-ID map; name registration already ran and "
            "temporary-map removal follows.",
            Utf16ToUtf8(pledge->GetPledgeName()).c_str(), static_cast<unsigned>(pledge->m_data->m_nDbId), pledge->m_nWorldId
        );
    }
    s_pledgeDBMapTemp.DeleteObject(key, const_cast<char*>("Pledge.cpp"), 0x13a4);
}

void CPledge::UnregisterPledge(CSPointer<CPledge> pledge) {
    // Current L2Server 0x00987f00.
    if (!pledge) {
        return;
    }

    const std::u16string loweredName = LowerPledgeNameKey(pledge->GetPledgeName());
    s_pledgeNameMap.DeleteObject(pledge->m_nWorldId, loweredName, const_cast<char*>("Pledge.cpp"), 0x13b1);
    s_pledgeDBMap.DeleteObject(
        std::CDualKey<int, int>(pledge->m_nWorldId, static_cast<int>(pledge->m_data->m_nDbId)),
        const_cast<char*>("Pledge.cpp"), 0x13b2
    );
}

void CPledge::SetPledgeName(wchar_t* const pledgeName) {
    // Current L2Server 0x0096db40.
    L2_GUARD_FUNCTION();
    CopyTargetPledgeName(m_data->m_sPledgeName, pledgeName);
}

void CPledge::SetPledgeMasterName(const wchar_t* const masterName) {
    // Current L2Server 0x0096dbf0.
    L2_GUARD_FUNCTION();
    CopyTargetPledgeName(m_data->m_sMasterName, masterName);
}

void CPledge::Dismiss() {
    // Current L2Server 0x00988bc4.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::CompareExchangeI32(&m_Dismissed, 1, 0) != 0) {
        return;
    }

    CancelWarWhoTargetedMe();
    RemovePledgeSkillsFromMembers();
    CPledgeRanking::Remove(m_data->m_nDbId);

    User* const master = GetMaster();
    if (master != nullptr) {
        static constexpr std::array<int, 9> kDismissedMasterSkills{
            0x00f6, 0x4a5a, 0x4a5b, 0x00f7, 0x4a82, 0x4a83, 0x3bb3, 0x3bb4, 0x3bb5};
        for (const int skillId : kDismissedMasterSkills) {
            master->DeleteAcquireSkill(skillId);
        }
        master->ValidateChanged(VB_MAX);
    }

    ClearAllSkillForAcademy();
    ::DismissPledge dismiss(GetPledgeName());
    ServeForAllMember(dismiss, true);
    m_memberList->ResetAllNickName(m_nWorldId);
    ::CloseAllWindowPacket closeAllWindows;
    ServeForAllMember(closeAllWindows, true);

    CSPointer<CPledge> retained(this, ObjectRefType::FunctionChain);
    UnregisterPledge(retained);
    s_pledgeSmartIdManager.DeleteObject(Id());
}

void CPledge::Oust(const std::uint32_t characterDatabaseId, const wchar_t* const characterName) {
    // Current L2Server 0x0097f87c.
    L2_GUARD_FUNCTION();
    if (characterDatabaseId == m_data->m_nMasterDbId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pledge master cannot be ousted! [%s][%s]",
        //     L"Pledge.cpp",
        //     0x766,
        //     GetPledgeName(),
        //     characterName)
        // Noise: A clan leader cannot be expelled. This is a normal restriction and the existing early return preserves it.
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] pledge master cannot be ousted! [%s][%s]",
        //     L"Pledge.cpp",
        //     0x766,
        //     GetPledgeName(),
        //     characterName)
        return;
    }

    const PledgeType pledgeType = m_memberList->GetPledgeType(characterDatabaseId);
    if (pledgeType == ACADEMY) {
        DeleteSkillForAcademy(GetAcademyMasterDbId(characterDatabaseId));
    }
    if (HasAcademyStudent(characterDatabaseId)) {
        DeleteSkillForAcademy(characterDatabaseId);
    }
    DeleteDbMember(characterDatabaseId);

    ::SystemMessage_S<NoCheck, User> ousted(
        SystemMessageId::CLAN_MEMBER_EXPELLED, EST_CHAR_NAME, WideToUtf16(characterName).c_str()
    );
    ServeForAllMember(ousted, true);
    CloseWindowPacket closeWindow(WideToUtf16(characterName).c_str());
    ServeForAllMember(closeWindow, true);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(characterDatabaseId)));
    if (!user) {
        const std::uint32_t now = static_cast<std::uint32_t>(runtime::crt::Time32Now());
        if (pledgeType != ACADEMY) {
            db.RequestSetUserPledgeInfo(this, characterDatabaseId, 1, now);
        }
        g_DeferredJob.ReserveDeferredJob(static_cast<int>(characterDatabaseId), 0, static_cast<int>(Id()));
    } else {
        RemovePledgeSkillByEliteFrom(user.get());
        RemovePledgeSkillsFrom(user.get());
        DeleteMember(user.get());
        if (pledgeType != ACADEMY) {
            user->ResetDismissPenalty();
            user->TakeOustedPenalty();
            if (BP::_PLEDGE_OUSTED_PENALTY_TIMEOUT < 0x15180) {
                user->SendSystemMessage_d(
                    SystemMessageId::EXPELLED_FROM_CLAN_WITH_REJOIN_DELAY_IN_MINUTES, BP::_PLEDGE_OUSTED_PENALTY_TIMEOUT / 60
                );
            } else {
                user->SendSystemMessage(SystemMessageId::EXPELLED_FROM_CLAN_WITH_REJOIN_RESTRICTION);
            }
        }

        user->GetSocket()->Send("c", 0x36);
        user->SetPledge(nullptr);
        user->m_nParamChanged |= std::int64_t{PCB_USER};
        user->m_nParamChanged |= std::int64_t{PCB_PLEDGE};
        (void)runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
        user->TakeOffItems();
        user->CloseAllPledgeWindowPacket();
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->Send("chddd", 0xfe, 0x17d, 5, m_data->m_nDbId, m_nWorldId);
        }
    }

    db.RequestResetUserNickname(m_nWorldId, characterDatabaseId);
    if (pledgeType != ACADEMY) {
        TakeOustPenalty();
    }
    SendPledgeOnlineMemberCount();
    ValidateTotalLevel();
}

void CPledge::Withdraw(User* const user) {
    // Current L2Server 0x0097fecc.
    L2_GUARD_FUNCTION();
    const PledgeType pledgeType = m_memberList->GetPledgeType(user->GetDBID());
    if (pledgeType == ACADEMY) {
        DeleteSkillForAcademy(GetAcademyMasterDbId(user->GetDBID()));
    }
    if (HasAcademyStudent(user->GetDBID())) {
        DeleteSkillForAcademy(user->GetDBID());
    }
    DeleteDbMember(user->GetDBID());
    DeleteMember(user);
    RemovePledgeSkillByEliteFrom(user);
    RemovePledgeSkillsFrom(user);
    if (pledgeType != ACADEMY) {
        user->ResetDismissPenalty();
        user->TakeWithdrawPenalty();
    }
    user->SetPledge(nullptr);
    user->m_nParamChanged |= std::int64_t{PCB_PLEDGE | PCB_USER};
    (void)runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);

    ::SystemMessage_S<NoCheck, User> withdrew(SystemMessageId::MEMBER_LEFT_CLAN, EST_CHAR_NAME, user->GetName());
    ServeForAllMember(withdrew, true);
    CloseWindowPacket closeWindow(user->GetName());
    ServeForAllMember(closeWindow, true);
    user->TakeOffItems();
    user->CloseAllPledgeWindowPacket();
    db.RequestResetUserNickname(m_nWorldId, user->GetDBID());
    SendPledgeOnlineMemberCount();
    ValidateTotalLevel();
}

void CPledge::DeleteReservedLoaded(const int worldId, const std::uint32_t pledgeDatabaseId) {
    // Current L2Server 0x0097f3c8.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(s_lockSet);
        if (!s_setPledgeToBeLoaded.empty()) {
            s_setPledgeToBeLoaded.erase(std::CDualKey<int, std::uint32_t>(worldId, pledgeDatabaseId));
            if (s_setPledgeToBeLoaded.empty()) {
                g_JobFinishSignal.SetAllPledgeLoaded();
            }
        }
    }
}

void CPledge::LoadingCompleted() {
    // Current L2Server 0x009896f4.
    L2_GUARD_FUNCTION();
    RegisterPledge(CSPointer<CPledge>(this, ObjectRefType::FunctionChain));
    m_memberList->RefreshMemberList();
    ::SetPledgeMember setMember(this);
    m_memberList->ServeForAllMember(setMember);
    ValidateTotalLevel();
    UpdatePledgeInfoPacket();
    OpenAllWindowPacket();
    PledgeStuff::AddPrivilegeByLevelAndMastery(this, false);
    SendPledgeSkillsToMembers();
    ApplyPledgeSkillsToMembers(false, false, false);
    DeleteReservedLoaded(m_nWorldId, m_data->m_nDbId);
    PledgeRaidSystem::inst().SetPledgeRaidInzoneBindingAllUserInPledge(this, false);
}

void CPledge::_ServeForAllMember(CPledgeMemberService& service) {
    L2_GUARD_FUNCTION();
    for (const std::uint32_t memberSid : m_memberSidSet) {
        if (!service.CanServe()) {
            break;
        }
        CCreature* const object = g_CreatureSmartIdManager.GetObjectW(memberSid);
        User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
        // Current dispatch calls the guarded Serve wrapper for
        // a null resolution too. Serve itself owns the null check.
        service.Serve(user);
    }
}

void CPledge::ServeForAllMember(CPledgeMemberService& service, bool serveForRaid) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        _ServeForAllMember(service);
    }
    if (serveForRaid) {
        service.ServeForRaid(*this);
    }
}

void CPledge::Say(
    User* const user, const char16_t* const message, const SayPacketType type, const bool relay, const char16_t* const targetName
) {
    // Current L2Server 0x009757a0.
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->m_PledgeGameUser.IsPlayer()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CHAT_IN_CEREMONY_OF_CHAOS);
        return;
    }

    ::Say service(user, targetName, message, type, relay);
    ServeForAllMember(service, true);
}

void CPledge::Say(
    const int tellerServerId,
    const int tellerDatabaseId,
    const char16_t* const tellerName,
    const char16_t* const message,
    const SayPacketType type
) {
    // Current L2Server 0x009758d4.
    L2_GUARD_FUNCTION();
    ::SayForRaid service(tellerServerId, tellerDatabaseId, tellerName, message, type);
    ServeForAllMember(service, false);
}

void CPledge::SayByMobile(
    const int serverId,
    const int characterDatabaseId,
    const char16_t* const characterName,
    const char16_t* const message,
    const SayPacketType type
) {
    // Current L2Server 0x009759dc.
    L2_GUARD_FUNCTION();
    ::SayByMobile service(serverId, characterDatabaseId, characterName, message, type);
    ServeForAllMember(service, false);
}

void CPledge::CancelAllRenamed() {
    L2_GUARD_FUNCTION();
    CancelRenamed service;
    ServeForAllMember(service, true);
}

void CPledge::SetDominionUnderSiege(CDominion* dominion, const bool underSiege) {
    L2_GUARD_FUNCTION();

    int status = 0;
    if (dominion == nullptr) {
        m_data->m_nDominionSiegeId = 0;
    } else {
        m_data->m_nDominionSiegeId =
            dominion->m_data != nullptr ? static_cast<std::uint32_t>(dominion->m_data->m_nResidenceID) : 0;
        status = underSiege ? 2 : 0;
    }

    SetDominionSiege service(static_cast<int>(m_data->m_nDominionSiegeId), status);
    ServeForAllMember(service, true);
}

void CPledge::SendDominionChannelSet(const bool enabled) {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member != nullptr) {
            member->PushSerializedMessage(new CSerializedDominionChannelSetMessage(member, enabled));
        }
    }
}

void CPledge::SendDominionEventToAllOnlineMember(
    const int npcId, const int dominionId, const int eventType, const bool start
) {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member != nullptr) {
            member->PushSerializedMessage(new CSerializedDominionEventMessage(member, eventType, npcId, dominionId, start));
        }
    }
}

void CPledge::SendDominionScriptEventToAllOnlineMember(const int arg1, const int arg2, const int arg3) {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member != nullptr) {
            member->PushSerializedMessage(new CSerializedDominionScriptEventMessage(member, arg1, arg2, arg3));
        }
    }
}

void CPledge::SendDominionMessageToAllOnlineMember(
    const int messageId,
    const int argument,
    const eSMsgType argumentType,
    const char16_t* const text,
    const bool argumentFirst
) {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member == nullptr || member->GetSocket() == nullptr) {
            continue;
        }
        if (argumentFirst) {
            member->GetSocket()->Send(
                "chccSch", 0x62, static_cast<std::int16_t>(messageId), 2, argumentType, text, EST_CASTLEID,
                static_cast<std::int16_t>(argument)
            );
        } else {
            member->GetSocket()->Send(
                "chcchcS", 0x62, static_cast<std::int16_t>(messageId), 2, EST_CASTLEID, static_cast<std::int16_t>(argument),
                argumentType, text
            );
        }
    }
}

void CPledge::SendDominionMessageToAllOnlineMember2(const int messageId, const int argument) {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members, false);
    for (User* const member : members) {
        if (member == nullptr || member->GetSocket() == nullptr) {
            continue;
        }
        if (argument == 0) {
            member->GetSocket()->SendSystemMessage(messageId);
        } else {
            member->GetSocket()->SendSystemMessage_d(messageId, argument);
        }
    }
}

void CPledge::CloseMemberWindowPacket(User* user) {
    L2_GUARD_FUNCTION();
    CloseMemberWindowPacket(user->GetName());
}

void CPledge::CloseMemberWindowPacket(const char16_t* const characterName) {
    L2_GUARD_FUNCTION();
    CloseWindowPacket service(characterName);
    ServeForAllMember(service, true);
}

void CPledge::UpdateInfoWindowPacket() {
    L2_GUARD_FUNCTION();
    UpdatePledgeInfoWindow service(this);
    ServeForAllMember(service, true);
}

void CPledge::SetPledgeMissionCheck(User* const user, const int missionCheckTime) {
    // Current L2Server 0x009777a0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        m_memberList->SetPledgeMissionCheck(0, missionCheckTime);
        ResetPledgeActivityMark service;
        ServeForAllMember(service, true);
    } else {
        m_memberList->SetPledgeMissionCheck(user->GetDBID(), missionCheckTime);
        UpdateMemberList service(user, true);
        ServeForAllMember(service, true);
    }
}

void CPledge::SetPledgeRaidInzoneBinding(const bool force) {
    L2_GUARD_FUNCTION();
    PMS_PledgeRaidInzoneBinding service(runtime::crt::Time32Now(), force);
    ServeForAllMember(service, true);
}

CPledgeBonusSetting::CPledgeBonusSetting() {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < 4; ++index) {
        m_nAccessRewardSkill[index][0] = 0;
        m_nHuntRewardItem[index] = 0;
    }
    m_nExpToHuntpointRate = 0x733c;
}

CPledgeBonusSetting::~CPledgeBonusSetting() = default;

CPledgeBonusSetting& CPledgeBonusSetting::Inst() {
    static CPledgeBonusSetting setting;
    return setting;
}

PledgeBonus::LastUpdateTime CPledgeBonus::CheckSaveTime(L2ServerLong currentTime, L2ServerLong lastSaveTime) {
    L2_GUARD_FUNCTION();

    const std::uint32_t adjustedBits = static_cast<std::uint32_t>(currentTime) - 0x5b68u;
    const std::int32_t adjustedTime = runtime::Int32FromBits(adjustedBits);
    const std::tm* const localTime = runtime::crt::LocalTime32(adjustedTime);

    std::uint32_t deltaBits = 0x186u;
    deltaBits -= static_cast<std::uint32_t>(localTime->tm_hour) * 0x3cu;
    deltaBits -= static_cast<std::uint32_t>(localTime->tm_min);
    deltaBits *= 0x3cu;
    deltaBits -= static_cast<std::uint32_t>(localTime->tm_sec);
    deltaBits -= static_cast<std::uint32_t>(lastSaveTime);
    deltaBits += adjustedBits;

    const std::int32_t delta = runtime::Int32FromBits(deltaBits);
    if (delta < 0) {
        return PledgeBonus::TODAY;
    }
    if (delta < 0x15180) {
        return PledgeBonus::YESTERDAY;
    }
    return PledgeBonus::BEFORE_YESTERDAY;
}

int CPledgeBonus::SetRewardLev(int enabled, int currentPoint, int maximumPoint) {
    L2_GUARD_FUNCTION();

    if (enabled != 1 || maximumPoint <= 0) {
        return 0;
    }

    int ratio = 0;
    if (maximumPoint <= 0x989680) {
        ratio = runtime::MultiplyInt32Wrapping(currentPoint, 100) / maximumPoint;
    } else {
        ratio = currentPoint / (maximumPoint / 100);
    }

    if (ratio >= 100) {
        return 4;
    }
    if (ratio >= 75) {
        return 3;
    }
    if (ratio >= 50) {
        return 2;
    }
    if (ratio >= 25) {
        return 1;
    }
    return 0;
}

int CPledgeBonus::AddBonusPoint(int bonusType, int point, L2ServerLong saveTime) {
    L2_GUARD_FUNCTION();

    PledgeBonusInfo& bonus = m_PledgeBonus[bonusType];
    const std::int32_t added = runtime::AddInt32Wrapping(bonus.m_nCurrentBonusPoint, point);
    const std::int32_t updated = std::min(added, bonus.m_nMaxBonusPoint);
    bonus.m_nLastSaveTime = static_cast<std::int32_t>(saveTime);
    bonus.m_nCurrentBonusPoint = updated;
    return updated;
}

void CPledgeBonus::RefreshBonusPoint(int bonusType, L2ServerLong saveTime, int pledgeLevel, int currentPoint, int enabled) {
    L2_GUARD_FUNCTION();

    PledgeBonusInfo& bonus = m_PledgeBonus[bonusType];
    bonus.m_nLastSaveTime = static_cast<std::int32_t>(saveTime);
    bonus.m_nTodayRewardLev = SetRewardLev(enabled, bonus.m_nCurrentBonusPoint, bonus.m_nMaxBonusPoint);
    bonus.m_nCurrentBonusPoint = currentPoint;

    bonus.m_nMaxBonusPoint = CPledgeBonusSetting::Inst().GetGoalRate(bonusType, pledgeLevel);
}

int CPledge::ProcessBonusPoint(User* user, int bonusType, int point, int enabled, L2ServerLong saveTime) {
    // Current L2Server 0x00977208.
    L2_GUARD_FUNCTION();

    PledgeBonusInfo& selected = m_PledgeBonus.m_PledgeBonus[bonusType];
    if (selected.m_nLastSaveTime > 0) {
        const PledgeBonus::LastUpdateTime updateTime = m_PledgeBonus.CheckSaveTime(saveTime, selected.m_nLastSaveTime);
        if (updateTime != PledgeBonus::TODAY) {
            const int oldAccessPoint = m_PledgeBonus.m_PledgeBonus[0].m_nCurrentBonusPoint;
            const int oldHuntPoint = m_PledgeBonus.m_PledgeBonus[1].m_nCurrentBonusPoint;
            const int oldAccessMaximum = m_PledgeBonus.m_PledgeBonus[0].m_nMaxBonusPoint;
            const int oldHuntMaximum = m_PledgeBonus.m_PledgeBonus[1].m_nMaxBonusPoint;

            for (int index = 0; index < 2; ++index) {
                int initialPoint = 0;
                if (point > 0 && index == bonusType) {
                    CLogFormatter formatter;
                    formatter.WriteLog1(
                        0x815, user, nullptr, GetPledgeName(), nullptr, nullptr, m_data->m_nDbId, m_data->m_nSkillLevel,
                        bonusType, point, point, selected.m_nLastSaveTime, 0
                    );
                    initialPoint = point;
                }

                m_PledgeBonus.RefreshBonusPoint(
                    index, saveTime, m_data->m_nSkillLevel, initialPoint, static_cast<int>(updateTime)
                );
                const PledgeBonusInfo& refreshed = m_PledgeBonus.m_PledgeBonus[index];
                db.RequestInitUserPledgeBonus(
                    user, this, index, refreshed.m_nLastSaveTime, refreshed.m_nCurrentBonusPoint, refreshed.m_nMaxBonusPoint,
                    refreshed.m_nTodayRewardLev
                );
            }

            CLogFormatter formatter;
            formatter.WriteLog(
                0x816, m_data->m_nDbId, 0, 0, 0, 0, 0, 0, reinterpret_cast<const char16_t*>(m_data->m_sPledgeName), u"", u"",
                m_data->m_nSkillLevel, oldAccessPoint, oldAccessMaximum, oldHuntPoint, oldHuntMaximum,
                m_PledgeBonus.m_PledgeBonus[0].m_nTodayRewardLev, m_PledgeBonus.m_PledgeBonus[1].m_nTodayRewardLev, 0, 0, 0,
                u"", u"", u"", u"", 0
            );
            return m_PledgeBonus.m_PledgeBonus[bonusType].m_nTodayRewardLev;
        }
    }

    const int oldPoint = selected.m_nCurrentBonusPoint;
    const int maximumPoint = selected.m_nMaxBonusPoint;
    if (point <= 0 || oldPoint >= maximumPoint) {
        return selected.m_nTodayRewardLev;
    }

    const int loggedPoint = std::min(runtime::AddInt32Wrapping(oldPoint, point), maximumPoint);
    {
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x815, user, nullptr, GetPledgeName(), nullptr, nullptr, m_data->m_nDbId, m_data->m_nSkillLevel, bonusType,
            point, loggedPoint, selected.m_nLastSaveTime, 0
        );
    }

    const int newPoint = m_PledgeBonus.AddBonusPoint(bonusType, point, saveTime);
    db.RequestUpdateUserPledgeBonus(user, this, bonusType, saveTime, point, selected.m_nMaxBonusPoint);

    const int newRewardLevel = m_PledgeBonus.SetRewardLev(1, newPoint, maximumPoint);
    const int oldRewardLevel = m_PledgeBonus.SetRewardLev(1, oldPoint, maximumPoint);
    const int changedRewardLevel = newRewardLevel != oldRewardLevel ? newRewardLevel : 0;

    if (bonusType == 0) {
        UpdatePledgeBonusPoint service(0, newPoint, changedRewardLevel);
        ServeForAllMember(service, true);
    } else if (bonusType == 1) {
        if (changedRewardLevel < 1) {
            user->GetSocket()->Send("chcd", 0xfe, 0x1b1, 1, newPoint);
        } else {
            UpdatePledgeBonusPoint service(1, newPoint, changedRewardLevel);
            ServeForAllMember(service, true);
        }
    }

    (void)enabled;
    return selected.m_nTodayRewardLev;
}

bool CPledge::CheckPowerByGradeId(int gradeId, PledgePowerEnum power) {
    L2_GUARD_FUNCTION();

    if (gradeId < 1 || gradeId > 9) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Invalid Pledge Power Grade Id(%d)",
        //     gradeId)
        // L2_LOG_ERROR(Log,
        //     "Invalid Pledge Power Grade Id(%d)",
        //     gradeId)
        L2_LOG_ERROR(
            Log, "Cannot check clan privileges for grade %d: valid grades are 1 through 9; access is denied.", gradeId
        );
        return false;
    }
    if (power < PP_NO || power >= PP_MAX) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    return m_data != nullptr && ((m_data->m_gradePowerList[gradeId - 1] >> static_cast<unsigned int>(power)) & 1U) != 0;
}

std::uint32_t CPledge::GetPowerGrade(const int gradeId) {
    // Current L2Server 0x00973c80.
    L2_GUARD_FUNCTION();
    return static_cast<std::uint32_t>(gradeId - 1) < 9U ? m_data->m_gradePowerList[gradeId - 1] : 0U;
}
