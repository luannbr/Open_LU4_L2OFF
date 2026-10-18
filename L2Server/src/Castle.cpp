#include "Castle.h"
#include "Alliance.h"
#include "Area.h"
#include "BattleCamp.h"
#include "CConfig.h"
#include "CWorld.h"
#include "ControlTower.h"
#include "DB.h"
#include "Dominion.h"
#include "Door.h"
#include "Fortress.h"
#include "HolyThing.h"
#include "NpcServer.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeInfo.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Relay.h"
#include "SSQ.h"
#include "Setting.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/GIP.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/LegacyCrtRandom.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/net/Serialize.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace NameValueForCastle {

// Current L2Server .data 0x015cc578..0x015cc584.
int SUCCESS_FROM_NPC = 145000;
int SUCCESS_FROM_PC = 180000;
int SUCCESS_DEFENCE = 220000;
int FAILED_DEFENCE = -60000;

} // namespace NameValueForCastle

std::int32_t CCastle::s_tBaseSiegeTime = 0;
std::int32_t CCastle::s_nMaxConcurrentSiege = 0;
RWLock CCastle::s_lock;
std::vector<CCastle*> CCastle::s_lstCastle;
std::set<int> CCastle::s_setReservableSiegeTime;
std::multimap<int, int> CCastle::s_mmapReservedSiegeTime;
std::vector<std::int32_t> CCastle::s_lstWday;
std::vector<std::int32_t> CCastle::s_lstHour;

CCastle::CCastle() :
    m_bAttackerListCached(false),
    m_alignment4D1To4D4{},
    m_tAttackerListLastCached(0),
    m_bDefenderListCached(false),
    m_alignment4D9To4DC{},
    m_tDefenderListLastCached(0),
    m_nAttackerListTotalPage(0),
    m_nDefenderListTotalPage(0),
    m_bufForAttackerListCaching{},
    m_bufForDefenderListCaching{},
    m_MusicName(nullptr),
    m_nTotalNumDomainFortress(0),
    m_alignment1469CTo146A0{},
    m_pDominion(nullptr),
    m_nPrevDominionStatus(0),
    m_bSupportBroken(false),
    m_alignment146ADTo146B0{},
    m_tPreviousSiegeEnd(0),
    m_nSiegeInform(0),
    m_nDbSaveTick(0),
    m_tPossessed(0),
    m_tPaused(0),
    m_bPausable(true),
    m_alignment146C5To146C8{},
    m_nValidate(0),
    m_nValidateWaiting(0),
    m_pNewOwnerPledge(nullptr),
    m_eNewResidenceType(CastleType::CASTLE_NOTHING),
    m_bHappenDestructTowerEvent(false),
    m_alignment146DDTo146E0{},
    m_PreviousOwnerPledgeDbId(0),
    m_nPrevOwnerPledgeWorldId(0),
    m_nAttackerScore(0),
    m_nDefenderScore(0) {
    // Current L2Server 0x00595cac.
    L2_GUARD_FUNCTION();
    m_sAllDomainFortress.clear();
    m_mapEventController.clear();
    InitKillScore();
}

CCastle::~CCastle() {
    // Current L2Server 0x00596354.
    L2_GUARD_FUNCTION();
    m_pNewOwnerPledge = nullptr;
    m_eNewResidenceType = CastleType::CASTLE_NOTHING;
    m_pDominion = nullptr;
    m_MusicName = nullptr;
}

bool CCastle::IsCastle() const { return true; }

CCastle* CCastle::CastCastle() const { return const_cast<CCastle*>(this); }

bool CCastle::IsExtended() const {
    return m_pDominion != nullptr && m_pDominion->m_data != nullptr && m_pDominion->m_data->m_nDominionStatus == 2;
}

int CCastle::GetDominionSiegeID() const {
    return m_pDominion != nullptr && m_pDominion->m_data != nullptr ? m_pDominion->m_data->m_nResidenceID : 0;
}

bool CCastle::IsUnderDominionSiege() const {
    return m_pDominion != nullptr && m_pDominion->m_data != nullptr &&
        m_pDominion->m_data->m_ResidenceState == RS_DOMINION_SIEGE;
}

bool CCastle::IsUnderSiege() const {
    if (m_data == nullptr) {
        return IsUnderDominionSiege();
    }
    return m_data->m_ResidenceState == RS_SIEGE || m_data->m_ResidenceState == RS_MID_VICTORY || IsUnderDominionSiege();
}

void CCastle::ChangeTaxRate() {
    // Current L2Server 0x005902c4.
    L2_GUARD_FUNCTION();
    (void)SSQSystem::inst().GetMaxTaxRateOfCastle();
    if (m_data != nullptr &&
        m_data->m_TaxInfoData[static_cast<std::size_t>(m_data->m_ResidenceType)].m_nTaxRate !=
            static_cast<std::uint32_t>(m_data->m_nTaxRateToChange)) {
        db.ReqWriteTax(
            this, 1, m_data->m_TaxInfoData[static_cast<std::size_t>(m_data->m_ResidenceType)].m_nTaxRate,
            m_data->m_nTaxRateToChange, IsCastle()
        );
    }
}

void CCastle::ChangeOwner(CPledge* const pledge, const int reason, bool) {
    // Current L2Server 0x005903d0.
    L2_GUARD_FUNCTION();
    (void)runtime::raw_atomic::CompareExchangePointer(&m_pNewOwnerPledge, pledge, static_cast<CPledge*>(nullptr));
    if (pledge != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Change Owner [%s] (no error -_-)",
        //     pledge->GetPledgeName())
        // L2_LOG_ERROR(Log,
        //     L"Change Owner [%s] (no error -_-)",
        //     pledge->GetPledgeName())
        L2_LOG_INFO(
            Log, "Castle %d: ownership change requested for clan '%s', residence type %d.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, Utf16ToUtf8(pledge->GetPledgeName()).c_str(), reason
        );
    }
    m_eNewResidenceType = static_cast<CastleType::Enum>(reason);
}

bool CCastle::SetSiegeDate(const std::int32_t siegeDate) {
    // Current L2Server 0x005904b4.
    L2_GUARD_FUNCTION();
    m_data->m_NextSiege = siegeDate;
    return true;
}

void CCastle::InitCastle() {
    // Current L2Server 0x00590558.
    L2_GUARD_FUNCTION();
    db.RequestLoadAllCastle();
    if (g_JobFinishSignal.m_nMustLoadCastle < 1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Castle]: count: 0")
        // L2_LOG_INFO(Log,
        //     L"Load Success[Castle]: count: 0")
        L2_LOG_INFO(
            Log,
            "Castle startup skips the completion wait: expected database count is %d; proceeding with the startup "
            "notification.",
            g_JobFinishSignal.m_nMustLoadCastle
        );
        g_JobFinishSignal.NotifyFinish(JFT_LOAD_CASTLE);
        CPledge::SetFlagReservePledge(PRLT_CASTLE);
    } else if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_CASTLE, 10)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Problem occurred in Loading Castle")
        // L2_LOG_ERROR(Log,
        //     L"Problem occurred in Loading Castle")
        L2_LOG_ERROR(
            Log,
            "Castle startup continues without load confirmation after a wait with a 10-second timeout; expected database "
            "count %d.",
            g_JobFinishSignal.m_nMustLoadCastle
        );
    } else {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Castle]: count: %d",
        //     g_JobFinishSignal.m_nMustLoadCastle)
        // L2_LOG_INFO(Log,
        //     L"Load Success[Castle]: count: %d",
        //     g_JobFinishSignal.m_nMustLoadCastle)
        L2_LOG_INFO(
            Log, "Castle load completion signal received; expected database count %d.", g_JobFinishSignal.m_nMustLoadCastle
        );
    }
}

void CCastle::ClearSiegeDate() {
    if (m_data != nullptr) {
        m_data->m_NextSiege = 0;
    }
}

void CCastle::ChangeCastleTypeOnTimerExpired(const std::int32_t now) {
    // Current L2Server 0x005906f0.
    L2_GUARD_FUNCTION();
    if (m_tPreviousSiegeEnd + BP::_CASTLE_CHANGE_TIME < now) {
        g_npcserver.Send("cddd", 0x26, m_data != nullptr ? m_data->m_nResidenceID : 0, 10, 0);
        ChangeResidenceState(RS_REGISTRATION);
    }
}

void CCastle::InitKillScore() {
    // Current L2Server 0x005908e4.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI64(&m_nAttackerScore, 0);
    runtime::raw_atomic::ExchangeI64(&m_nDefenderScore, 0);
}

void CCastle::IncreaseAttakerScore() {
    // Current L2Server 0x00590980.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::AddI64(&m_nAttackerScore, 1);
}

void CCastle::IncreaseDefenderScore() {
    // Current L2Server 0x00590a10.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::AddI64(&m_nDefenderScore, 1);
}

void CCastle::Set(
    const int ownerPledgeId,
    const int ownerWorldId,
    const std::int32_t nextSiege,
    int,
    const int residenceState,
    SharedUtilsWideChar* const residenceName,
    int,
    const std::int64_t taxIncome,
    const int siegeElapsed,
    const CastleType::Enum castleType,
    const int successCampaign
) {
    // Current L2Server 0x00590ca0 ignores siegeTerm and
    // previousSiegeEnd.
    L2_GUARD_FUNCTION();
    if (m_data == nullptr) {
        return;
    }
    m_data->m_NextSiege = nextSiege;
    m_data->m_nResidenceOwnerID = static_cast<std::uint32_t>(ownerPledgeId);
    m_data->m_nResidenceOwnerWorldId = ownerWorldId;
    m_data->m_ResidenceState = static_cast<ResidenceState>(residenceState);
    m_data->m_nTaxIncome = taxIncome;
    const std::size_t nameLength =
        residenceName != nullptr ? std::char_traits<SharedUtilsWideChar>::length(residenceName) : 0;
    const std::size_t count = std::min(nameLength, m_data->m_sResidenceName.size() - 1);
    if (count != 0) {
        std::memcpy(m_data->m_sResidenceName.data(), residenceName, count * sizeof(char16_t));
    }
    m_data->m_sResidenceName[count] = u'\0';
    m_data->m_ResidenceType = static_cast<int>(castleType);
    m_data->m_nSuccessCampaign = successCampaign;
    m_nSiegeElapsed = siegeElapsed;
}

bool CCastle::IsRegisteredForSiege(CPledge* const pledge) const {
    if (pledge == nullptr) {
        return false;
    }
    return const_cast<CCastle*>(this)->GetReservedStatus(
               static_cast<unsigned int>(pledge->GetDbId()), pledge->GetWorldId()
           ) != 0;
}

void CCastle::ValidateLifeControlLevel(const bool sendEvent) {
    // Current L2Server 0x00590b4c.
    L2_GUARD_FUNCTION();
    int workingCount = 0;
    for (CControlTower* const tower : m_lstLifeControlTower) {
        if (tower != nullptr && tower->m_eStatus == ControlTowerStatusEnum::Working) {
            ++workingCount;
        }
    }

    {
        L2_WRITE_GUARD(*this);
        if (!m_bHappenDestructTowerEvent && sendEvent && workingCount == 1) {
            m_bHappenDestructTowerEvent = true;
            g_npcserver.Send("cddd", 0x26, m_data != nullptr ? m_data->m_nResidenceID : 0, 4, 0);
        }
    }
    if (m_data != nullptr) {
        m_data->m_nLifeControlLevel = workingCount;
    }
}

void CCastle::PauseSiege() {
    // Current L2Server 0x00590dc4.
    L2_GUARD_FUNCTION();
    if (m_bPaused) {
        return;
    }

    MakeDoorsBreakable(false);
    for (CControlTower* const tower : m_lstLifeControlTower) {
        if (tower != nullptr) {
            tower->OnSiegePaused();
        }
    }
    ValidateLifeControlLevel(false);
    SetFreezeField(true);
    m_bPaused = true;
    m_tPaused = runtime::crt::Time32Now();
}

void CCastle::ResumeSiege() {
    // Current L2Server 0x00590ee4.
    L2_GUARD_FUNCTION();
    SetFreezeField(false);
    MakeDoorsBreakable(true);
    for (CControlTower* const tower : m_lstLifeControlTower) {
        if (tower != nullptr) {
            tower->OnSiegeResumed();
        }
    }
    ValidateLifeControlLevel(false);

    if (!m_bIsDominionSiege) {
        g_npcserver.Send("cddd", 0x26, m_data != nullptr ? m_data->m_nResidenceID : 0, 1, 1);
    } else {
        const int dominionId =
            m_pDominion != nullptr && m_pDominion->m_data != nullptr ? m_pDominion->m_data->m_nResidenceID : 0;
        g_npcserver.Send("cddd", 0x26, dominionId, 1, 1);
        if (m_pDominion != nullptr && m_pDominion->IsExtended()) {
            g_npcserver.Send("cddd", 0x26, dominionId, 6, 1);
        }
    }

    if (!m_bIsDominionSiege) {
        if (m_data != nullptr && m_data->m_nResidenceOwnerID != 0) {
            // Current target reloads mercenary state here.
            db.RequestLoadMercenary(*this);
        }
        if (m_tPaused != 0) {
            m_nLostTime += runtime::crt::Time32Now() - m_tPaused;
        }
        if (m_nLostTime < 0) {
            m_nLostTime = 0;
        }
    }
    m_bPaused = false;
    m_tPaused = 0;
}

int CCastle::GetReservedAttackerCount() {
    // Current L2Server 0x00591120.
    L2_GUARD_FUNCTION();
    int count;
    {
        L2_READ_GUARD(*this);
        count = static_cast<int>(m_mapAttackerReservedPledge.size());
    }
    return count;
}

int CCastle::GetReservedDefenderCount() {
    // Current L2Server 0x005911e0.
    L2_GUARD_FUNCTION();
    int count;
    {
        L2_READ_GUARD(*this);
        count = static_cast<int>(m_mapDefenderReservedPledge.size());
    }
    return count;
}

int CCastle::GetNumChildDomainFortress() {
    // Current L2Server 0x005912a0.
    L2_GUARD_FUNCTION();
    int count;
    {
        L2_READ_GUARD(*this);
        count = static_cast<int>(m_sDomainFortress.size());
    }
    return count;
}

int CCastle::GetReservedStatus(const unsigned int pledgeId, const int worldId) {
    // Current L2Server 0x00593f14.
    L2_GUARD_FUNCTION();
    int status;
    {
        L2_READ_GUARD(*this);
        const PledgeKey key(worldId, static_cast<int>(pledgeId));
        status = 0;
        if (m_mapAttackerReservedPledge.find(key) != m_mapAttackerReservedPledge.end()) {
            status = 1;
        } else if (m_mapDefenderReservedPledge.find(key) != m_mapDefenderReservedPledge.end()) {
            status = 2;
        }
    }
    return status;
}

bool CCastle::CancelCastleAttackSiege(CPledge* const pledge) {
    // Current L2Server 0x0059402c.
    L2_GUARD_FUNCTION();
    bool cancelled;
    {
        L2_WRITE_GUARD(*this);
        cancelled = false;
        if (pledge != nullptr) {
            const PledgeKey key(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()));
            const auto found = m_mapAttackerReservedPledge.find(key);
            if (found != m_mapAttackerReservedPledge.end()) {
                std::free(found->second);
                m_mapAttackerReservedPledge.erase(found);
                m_bAttackerListCached = false;
                pledge->m_data->m_nReservedOffenceCastleSiegeDbId = 0;
                cancelled = true;
            }
        }
    }
    return cancelled;
}

bool CCastle::CancelCastleDefenseSiege(CPledge* const pledge) {
    // Current L2Server 0x0059417c.
    L2_GUARD_FUNCTION();
    bool cancelled;
    {
        L2_WRITE_GUARD(*this);
        cancelled = false;
        if (pledge != nullptr) {
            const PledgeKey key(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()));
            const auto found = m_mapDefenderReservedPledge.find(key);
            if (found == m_mapDefenderReservedPledge.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] [CASTLE][CancelCastleDefenseSiege] "
                //     L"No exist pledge in mapDefenderReservedPledge "
                //     L"ResidenceID[%d] PledgeID[%d] WorldID[%d]",
                //     L"Castle.cpp",
                //     0x9f8,
                //     m_data != nullptr ? m_data->m_nResidenceID : 0,
                //     pledge->GetDbId(),
                //     pledge->GetWorldId())
                // Noise: The attack-cancellation reply also removes any defender entry. An absent defender reservation is an
                // expected no-op. L2_LOG_ERROR(Log,
                //     L"[%s][%d] [CASTLE][CancelCastleDefenseSiege] "
                //     L"No exist pledge in mapDefenderReservedPledge "
                //     L"ResidenceID[%d] PledgeID[%d] WorldID[%d]",
                //     L"Castle.cpp",
                //     0x9f8,
                //     m_data != nullptr ? m_data->m_nResidenceID : 0,
                //     pledge->GetDbId(),
                //     pledge->GetWorldId())
            } else {
                std::free(found->second);
                m_mapDefenderReservedPledge.erase(found);
                m_bDefenderListCached = false;
                pledge->m_data->m_nReservedDefenceCastleSiegeDbId = 0;
                cancelled = true;
            }
        }
    }
    return cancelled;
}

bool CCastle::ApproveDefence(const unsigned int pledgeId, const unsigned int worldId) {
    // Current L2Server 0x0059432c.
    L2_GUARD_FUNCTION();
    bool approved;
    {
        L2_WRITE_GUARD(*this);
        const PledgeKey key(static_cast<int>(worldId), static_cast<int>(pledgeId));
        const auto found = m_mapDefenderReservedPledge.find(key);
        approved = found != m_mapDefenderReservedPledge.end();
        if (approved) {
            found->second->nStatus = 3;
            m_bDefenderListCached = false;
        }
    }
    return approved;
}

void CCastle::FreeReserveCastleSiegeList() {
    // Current L2Server 0x00594430.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapDefenderReservedPledge) {
        std::free(entry.second);
    }
    for (const auto& entry : m_mapAttackerReservedPledge) {
        std::free(entry.second);
    }
    m_mapDefenderReservedPledge.clear();
    m_mapAttackerReservedPledge.clear();
    m_bAttackerListCached = false;
    m_bDefenderListCached = false;
}

void CCastle::ReserveAttacker(CPledge* const pledge, const int proposeTime) {
    // Current L2Server 0x005954a4.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        if (pledge != nullptr) {
            const PledgeKey key(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()));
            const auto found = m_mapAttackerReservedPledge.find(key);
            if (found == m_mapAttackerReservedPledge.end()) {
                std::unique_ptr<CastleSiegeData, decltype(&std::free)> data(
                    static_cast<CastleSiegeData*>(std::malloc(sizeof(CastleSiegeData))), &std::free
                );
                if (!data) {
                    L2_LOG_ERROR(
                        Log,
                        "Castle %d: cannot record attacker reservation for clan %u in world %d because allocation failed.",
                        m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(pledge->GetDbId()),
                        pledge->GetWorldId()
                    );
                    return;
                }
                data->nServerId = static_cast<int>(pledge->Id());
                data->nProposeTime = proposeTime;
                data->nStatus = 0;
                if (m_mapAttackerReservedPledge.emplace(key, data.get()).second) {
                    data.release();
                    pledge->m_data->m_nReservedOffenceCastleSiegeDbId = m_data != nullptr ? m_data->m_nResidenceID : 0;
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] [CASTLE][ReserveAttacker]  Already exist "
                //     L"pledge in mapAttacketReservePledge ResidenceID[%d] "
                //     L"PledgeID[%d] WorldID[%d]",
                //     L"Castle.cpp",
                //     0x998,
                //     m_data != nullptr ? m_data->m_nResidenceID : 0,
                //     pledge->GetDbId(),
                //     pledge->GetWorldId())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] [CASTLE][ReserveAttacker]  Already exist "
                //     L"pledge in mapAttacketReservePledge ResidenceID[%d] "
                //     L"PledgeID[%d] WorldID[%d]",
                //     L"Castle.cpp",
                //     0x998,
                //     m_data != nullptr ? m_data->m_nResidenceID : 0,
                //     pledge->GetDbId(),
                //     pledge->GetWorldId())
                L2_LOG_WARN(
                    Log,
                    "Castle %d: duplicate attacker reservation for clan %u in world %d is ignored; the existing reservation "
                    "is retained.",
                    m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(pledge->GetDbId()),
                    pledge->GetWorldId()
                );
            }
            m_bAttackerListCached = false;
        }
    }
}

void CCastle::ReserveDefender(CPledge* const pledge, const int proposeTime, const int status) {
    // Current L2Server 0x00595680.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        if (pledge != nullptr) {
            const PledgeKey key(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()));
            const auto found = m_mapDefenderReservedPledge.find(key);
            if (found == m_mapDefenderReservedPledge.end()) {
                std::unique_ptr<CastleSiegeData, decltype(&std::free)> data(
                    static_cast<CastleSiegeData*>(std::malloc(sizeof(CastleSiegeData))), &std::free
                );
                if (!data) {
                    L2_LOG_ERROR(
                        Log,
                        "Castle %d: cannot record defender reservation for clan %u in world %d because allocation failed.",
                        m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(pledge->GetDbId()),
                        pledge->GetWorldId()
                    );
                    return;
                }
                data->nServerId = static_cast<int>(pledge->Id());
                data->nProposeTime = proposeTime;
                data->nStatus = status;
                if (m_mapDefenderReservedPledge.emplace(key, data.get()).second) {
                    data.release();
                    pledge->m_data->m_nReservedDefenceCastleSiegeDbId = m_data != nullptr ? m_data->m_nResidenceID : 0;
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] [CASTLE][ReserveDefender]  Already exist "
                //     L"pledge in mapDefenderReservedPledge ResidenceID[%d] "
                //     L"PledgeID[%d] WorldID[%d]",
                //     L"Castle.cpp",
                //     0x9b6,
                //     m_data != nullptr ? m_data->m_nResidenceID : 0,
                //     pledge->GetDbId(),
                //     pledge->GetWorldId())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] [CASTLE][ReserveDefender]  Already exist "
                //     L"pledge in mapDefenderReservedPledge ResidenceID[%d] "
                //     L"PledgeID[%d] WorldID[%d]",
                //     L"Castle.cpp",
                //     0x9b6,
                //     m_data != nullptr ? m_data->m_nResidenceID : 0,
                //     pledge->GetDbId(),
                //     pledge->GetWorldId())
                L2_LOG_WARN(
                    Log,
                    "Castle %d: duplicate defender reservation for clan %u in world %d is ignored; the existing reservation "
                    "is retained instead of requested status %d.",
                    m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(pledge->GetDbId()),
                    pledge->GetWorldId(), status
                );
            }
            m_bDefenderListCached = false;
        }
    }
}

void CCastle::CancelAttackSiegeAtAllCastle(CSPointer<CPledge> pledge) {
    // Current L2Server 0x005947d0.
    for (CCastle* const castle : s_lstCastle) {
        if (castle != nullptr) {
            castle->CancelCastleAttackSiege(pledge.get());
        }
    }
}

void CCastle::StartAllTimer() {
    // Current L2Server 0x00594864.
    L2_GUARD_FUNCTION();
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence != nullptr && residence->IsCastle()) {
            CCastle* const castle = residence->CastCastle();
            if (castle != nullptr) {
                castle->StartTimer();
            }
        }
    }
}

void CCastle::SetAllCastleSiegeToPledges() {
    // Current L2Server 0x00595870.
    L2_GUARD_FUNCTION();
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence == nullptr || !residence->IsCastle()) {
            continue;
        }
        CCastle* const castle = residence->CastCastle();
        if (castle == nullptr) {
            continue;
        }

        if (castle->m_data != nullptr && castle->m_data->m_nResidenceOwnerID != 0) {
            CSPointer<CPledge> owner = CPledge::FindPledge(
                castle->m_data->m_nResidenceOwnerWorldId, static_cast<int>(castle->m_data->m_nResidenceOwnerID)
            );
            if (owner) {
                castle->SetPledge(owner.get());
                owner->SetCastle(castle);
            }
        }

        const std::size_t count = castle->m_lstTempReservedPledgeWorldId.size();
        for (std::size_t index = 0; index < count; ++index) {
            CSPointer<CPledge> pledge = CPledge::FindPledge(
                runtime::CheckedVectorAt(castle->m_lstTempReservedPledgeWorldId, index),
                runtime::CheckedVectorAt(castle->m_lstTempReservedPledgeDbId, index)
            );
            if (!pledge) {
                continue;
            }
            if (runtime::CheckedVectorAt(castle->m_lstTempReservedProposeStatus, index) == 0) {
                castle->ReserveAttacker(pledge.get(), runtime::CheckedVectorAt(castle->m_lstTempReservedProposeTime, index));
            } else {
                castle->ReserveDefender(
                    pledge.get(), runtime::CheckedVectorAt(castle->m_lstTempReservedProposeTime, index),
                    runtime::CheckedVectorAt(castle->m_lstTempReservedProposeStatus, index)
                );
            }
        }
    }
}

bool CCastle::ReserveSiegeTime(const int residenceId, const std::int32_t siegeTime) {
    // Current L2Server 0x00595b48.
    L2_GUARD_FUNCTION();
    bool reserved;
    {
        L2_WRITE_GUARD(s_lock);
        reserved = false;
        const auto sameTime = s_mmapReservedSiegeTime.equal_range(siegeTime);
        std::size_t concurrentCount = 0;
        for (auto it = sameTime.first; it != sameTime.second; ++it) {
            ++concurrentCount;
        }
        if (concurrentCount < static_cast<std::size_t>(s_nMaxConcurrentSiege)) {
            for (auto it = s_mmapReservedSiegeTime.begin(); it != s_mmapReservedSiegeTime.end(); ++it) {
                if (it->second == residenceId) {
                    s_mmapReservedSiegeTime.erase(it);
                    break;
                }
            }
            s_mmapReservedSiegeTime.emplace(siegeTime, residenceId);
            reserved = true;
        }
    }
    return reserved;
}

void CCastle::ExpelNonPledgeMember() {
    // Current L2Server 0x00596808.
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    CPledge* const owner = GetResidenceOwnerPledge();
    if (owner == nullptr) {
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            if (user->IsInRealWorld() && IsInResidence(user->GetPos(), true)) {
                users.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
        });
    } else {
        const int ownerPledgeId = static_cast<int>(owner->GetDbId());
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            if (user->IsInRealWorld() && ownerPledgeId != static_cast<int>(user->m_data->m_nPledgeID) &&
                IsInResidence(user->GetPos(), true)) {
                users.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
        });
    }
    ReserveExpelOfUsers(users);
}

void CCastle::BanNonDefendingPlayers(const BAN_TYPE type) {
    // Current L2Server 0x00596ca0.
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    CPledge* const owner = GetResidenceOwnerPledge();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (!user->IsInRealWorld() || user->m_bIsObserving) {
            continue;
        }
        if (owner != nullptr && IsDefendingPledge(static_cast<int>(user->m_data->m_nPledgeID), user->m_data->m_nWorldId)) {
            continue;
        }
        if (!IsInResidence(user->GetPos(), true)) {
            continue;
        }
        users.push_back(user);
        L2_ADD_REF(user, ObjectRefType::EventRetain);
    });

    for (User* const user : users) {
        if (user == nullptr) {
            continue;
        }
        const std::uint32_t random = static_cast<std::uint32_t>(::LegacyCrtRand());
        FVector position{};

        if (type >= BAN_TYPE_0 && type < BAN_TYPE_2) {
            const int sealOwner = static_cast<int>(SSQSystem::inst().GetSealOwner(SSQSeal::STRIFE));
            if (sealOwner >= SSQPart::NONE && sealOwner < SSQPart::DAWN) {
                position = Singleton<CSetting>::inst().GetRestartPoint(
                    user->m_data->m_Race, user->m_data->m_Pos, user->m_data->m_nKarma < 0, user->m_data->m_nPKCount
                );
            } else if (sealOwner == SSQPart::DAWN) {
                auto& restartPoints = !user->m_bEinhasadeTransformed && user->m_data->m_nKarma >= 0
                    ? m_OtherRestartVillageList
                    : m_ChaoRestartPointList;
                position = restartPoints[static_cast<std::size_t>(
                    runtime::Win64UnsignedRemainder(random, restartPoints.size())
                )];
            }
        } else if (type == BAN_TYPE_2) {
            position = m_BanishPointList[static_cast<std::size_t>(
                runtime::Win64UnsignedRemainder(random, m_BanishPointList.size())
            )];
        }

        if (type != BAN_TYPE_0) {
            CPledge* const pledge = user->GetPledge();
            if (pledge != nullptr) {
                if (pledge->IsDefender(this)) {
                    position = m_OwnerRestartPointList[static_cast<std::size_t>(
                        runtime::Win64UnsignedRemainder(random, m_OwnerRestartPointList.size())
                    )];
                } else if (pledge->IsAttacker(this)) {
                    CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
                    if (battleCamp && battleCamp->IsActive() &&
                        battleCamp->GetBaseResidenceId() == (m_data != nullptr ? m_data->m_nResidenceID : 0)) {
                        position = battleCamp->GetTeleportPos();
                    }
                }
            }
        }
        g_ExpelTimer.ReserveExpelWithLock(user, position);
    }
}

void CCastle::OnDefenceSiegeSuccess(CSPointer<CPledge> pledge) {
    // Current L2Server 0x00591b30.
    L2_GUARD_FUNCTION();
    if (!pledge) {
        return;
    }
    pledge->RequestCastleSiegeDefenceCount(static_cast<unsigned int>(pledge->GetCastleSiegeDefenceCount() + 1));
    PledgeFunction* const function = pledge->GetFunction();
    if (function != nullptr && function->IsEnable(PledgeFunctionType::CASTLE_SIEGE)) {
        PledgeStuff::UpdateNameValueOf(pledge.get(), NameValueForCastle::SUCCESS_DEFENCE);
    }
}

bool CCastle::IsRelatedFortress(const int fortressId) {
    // Current L2Server 0x00592500.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence == nullptr || !residence->IsFortress() || m_data == nullptr) {
        return false;
    }
    return std::find(std::begin(m_data->m_nRelatedFortress), std::end(m_data->m_nRelatedFortress), fortressId) !=
        std::end(m_data->m_nRelatedFortress);
}

bool CCastle::IsChildFortress(CFortress* const fortress) {
    // Current L2Server 0x005930fc.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr || fortress->GetContractStatus() != FCS_CASTLE_CONTRACT ||
        fortress->GetParentCastleId() != (m_data != nullptr ? m_data->m_nResidenceID : 0)) {
        return false;
    }

    bool found;
    {
        L2_READ_GUARD(*this);
        found = fortress->m_eLocation == FLT_TERRITORY_DOMAIN
            ? m_sDomainFortress.find(fortress) != m_sDomainFortress.end()
            : m_sBoundaryFortress.find(fortress) != m_sBoundaryFortress.end();
    }
    return found;
}

bool CCastle::IsAllChildFortressRelated() {
    // Current L2Server 0x00592c84.
    L2_GUARD_FUNCTION();
    bool allRelated = true;
    {
        L2_WRITE_GUARD(*this);
        for (CFortress* const fortress : m_sDomainFortress) {
            if (fortress != nullptr && fortress->GetContractStatus() == FCS_INDEPENDENT) {
                allRelated = false;
            }
        }
    }
    return allRelated;
}

void CCastle::NotifySupportBroken() {
    // Current L2Server 0x00592db4.
    L2_GUARD_FUNCTION();
    if (CDominionManager::Inst()->m_nState == DSST_DOMINION_SIEGE) {
        for (CDoor* const door : m_DoorList) {
            door->Open(false, false);
        }
        for (CControlTower* const tower : m_ControlTowerList) {
            tower->OnSiegeEnded();
        }
        ValidateLifeControlLevel(false);
        m_bSupportBroken = true;
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] dominion is not in siege! id[%d]",
    //     L"Castle.cpp",
    //     0xe75,
    //     m_data != nullptr ? m_data->m_nResidenceID : 0)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] dominion is not in siege! id[%d]",
    //     L"Castle.cpp",
    //     0xe75,
    //     m_data != nullptr ? m_data->m_nResidenceID : 0)
    L2_LOG_WARN(
        Log, "Castle %d: support-break notification ignored because the dominion siege is not active.",
        m_data != nullptr ? m_data->m_nResidenceID : 0
    );
}

bool CCastle::IsAttackerPledge(CPledge* const pledge) {
    // Current L2Server 0x005932a0.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return false;
    }
    const PledgeKey key(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()));
    bool found;
    {
        L2_READ_GUARD(*this);
        found = m_mapAttackerReservedPledge.find(key) != m_mapAttackerReservedPledge.end();
    }
    return found;
}

bool CCastle::IsDefenderPledge(CPledge* const pledge) {
    // Current L2Server 0x005933d8.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return false;
    }
    const PledgeKey key(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()));
    bool found;
    {
        L2_READ_GUARD(*this);
        found = m_mapDefenderReservedPledge.find(key) != m_mapDefenderReservedPledge.end();
    }
    return found;
}

bool CCastle::IsCastleSiegeRegistered(CPledge* const pledge, const int worldId) {
    // Current L2Server 0x00594694 checks only the first castle.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr || worldId < 1 || s_lstCastle.empty()) {
        return false;
    }

    CCastle* const castle = s_lstCastle.front();
    if (castle == nullptr) {
        return false;
    }
    const PledgeKey key(worldId, static_cast<int>(pledge->GetDbId()));
    bool found;
    {
        L2_READ_GUARD(*castle);
        found = castle->m_mapAttackerReservedPledge.find(key) != castle->m_mapAttackerReservedPledge.end() ||
            castle->m_mapDefenderReservedPledge.find(key) != castle->m_mapDefenderReservedPledge.end();
    }
    return found;
}

void CCastle::MakeEventController(CastleType::Enum, int) {
    // Current L2Server 0x00590674 is guard-only.
    L2_GUARD_FUNCTION();
}

void CCastle::SetDominion(CDominion* const dominion) {
    // Current L2Server 0x005907e0.
    L2_GUARD_FUNCTION();
    if (dominion == nullptr) {
        return;
    }

    const int dominionId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
    if (m_pDominion == nullptr) {
        m_pDominion = dominion;
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] dominion already exists for castle. "
    //     L"castle id[%d] dominion id[%d]",
    //     L"Castle.cpp",
    //     0xe64,
    //     m_data != nullptr ? m_data->m_nResidenceID : 0,
    //     dominionId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] dominion already exists for castle. "
    //     L"castle id[%d] dominion id[%d]",
    //     L"Castle.cpp",
    //     0xe64,
    //     m_data != nullptr ? m_data->m_nResidenceID : 0,
    //     dominionId)
    L2_LOG_ERROR(
        Log, "Cannot assign dominion %d to castle %d: a dominion is already assigned; the existing binding is retained.",
        dominionId, m_data != nullptr ? m_data->m_nResidenceID : 0
    );
}

void CCastle::AddRelatedFortress(CFortress* const fortress) {
    // Current L2Server 0x0059497c.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr) {
        return;
    }

    const int fortressId = fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0;
    for (int index = 0; index < 5; ++index) {
        int& relatedFortress = m_data->m_nRelatedFortress[index];
        if (relatedFortress == fortressId) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] related fortress already exists for castle. "
            //     L"castle id[%d] fortress id[%d]",
            //     L"Castle.cpp",
            //     0xd56,
            //     m_data != nullptr ? m_data->m_nResidenceID : 0,
            //     fortressId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] related fortress already exists for castle. "
            //     L"castle id[%d] fortress id[%d]",
            //     L"Castle.cpp",
            //     0xd56,
            //     m_data != nullptr ? m_data->m_nResidenceID : 0,
            //     fortressId)
            L2_LOG_ERROR(
                Log, "Castle %d already lists related fortress %d; the duplicate binding is not added.",
                m_data != nullptr ? m_data->m_nResidenceID : 0, fortressId
            );
            return;
        }
        if (relatedFortress == -1) {
            relatedFortress = fortressId;
            if (fortress->m_eLocation == FLT_TERRITORY_DOMAIN) {
                ++m_nTotalNumDomainFortress;
                m_sAllDomainFortress.insert(fortress);
            }
            return;
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] fortress list not inserted! overflow. "
    //     L"MAX_RELATED_FORTRESS[%d] castle id[%d] fortress id[%d]",
    //     L"Castle.cpp",
    //     0xd69,
    //     5,
    //     m_data != nullptr ? m_data->m_nResidenceID : 0,
    //     fortressId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] fortress list not inserted! overflow. "
    //     L"MAX_RELATED_FORTRESS[%d] castle id[%d] fortress id[%d]",
    //     L"Castle.cpp",
    //     0xd69,
    //     5,
    //     m_data != nullptr ? m_data->m_nResidenceID : 0,
    //     fortressId)
    L2_LOG_ERROR(
        Log, "Cannot add related fortress %d to castle %d: all 5 list slots are occupied.", fortressId,
        m_data != nullptr ? m_data->m_nResidenceID : 0
    );
}

int CCastle::GetBattleFieldCastleId(FVector position) {
    L2_GUARD_FUNCTION();
    if (!CWorld::Inst().GetGeo()->IsField(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
            GEO_FIELD_BATTLE_RESIDENCE
        )) {
        return 0;
    }

    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence == nullptr || !residence->IsCastle() || !residence->IsInBattleField(position, true)) {
            continue;
        }
        return residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
    }
    return 0;
}

void CCastle::RegisterSiegeGolemSummoner(User* user) {
    // Current L2Server 0x005945a8.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        m_SiegeGolemSummonerSet.insert(static_cast<int>(user->GetServerId()));
    }
}

void CCastle::SetDefendingCastle(const bool enabled, std::vector<User*>& users) {
    // Current L2Server 0x00591720.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_data->m_DefendingPledgeListSize; ++index) {
        const PledgeInfo& info = m_data->m_stDefendingPledgeList[static_cast<std::size_t>(index)];
        CSPointer<CPledge> pledge = CPledge::FindPledge(info.m_nWorldID, info.m_nPledgeID);
        if (!pledge) {
            continue;
        }
        pledge->SetCastleUnderSiege(this, enabled ? 2 : 0);
        pledge->m_nSiegeDeathCount = 0;
        pledge->m_nSiegeKillCount = 0;
        pledge->GetAllMember(users, true);
    }
}

void CCastle::SetOffendingCastle(const bool enabled, std::vector<User*>& users) {
    // Current L2Server 0x00591924.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_data->m_OffendingPledgeListSize; ++index) {
        const PledgeInfo& info = m_data->m_stOffendingPledgeList[static_cast<std::size_t>(index)];
        CSPointer<CPledge> pledge = CPledge::FindPledge(info.m_nWorldID, info.m_nPledgeID);
        if (!pledge) {
            continue;
        }
        pledge->SetCastleUnderSiege(this, enabled ? 1 : 0);
        pledge->m_nSiegeDeathCount = 0;
        pledge->m_nSiegeKillCount = 0;
        pledge->GetAllMember(users, true);
    }
}

void CCastle::ValidateStartSiege() {
    // Current L2Server 0x005927fc.
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_DoorList) {
        door->Close(false, false);
    }
}

void CCastle::FreeSiegeGolem() {
    // Current L2Server 0x00595200.
    L2_GUARD_FUNCTION();
    for (const int userSid : m_SiegeGolemSummonerSet) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userSid);
        if (creature == nullptr || !creature->IsPC()) {
            continue;
        }
        User* const user = creature->CastUser();
        FOR_ALL_SUMMON_IN_SUMMON_LIST(
            user, summon,
            // Recovered source used:
            // if (summon->m_data->m_nClassID == 0x18) {
            // Current 0x00595200 reads shared-data offset +0x90, which is
            // m_Race. M_nClassID is at +0x358c and is not read here.
            if (static_cast<int>(summon->m_data->m_Race) == 0x18) {
                user->UnlinkSummon(summon);
                summon->Delete();
            }
        );
    }
}

void CCastle::GetDefenderPledge(std::vector<CPledge*>& pledges, std::vector<int>& proposeTimes, std::vector<int>& statuses) {
    // Current L2Server 0x00598f24.
    L2_GUARD_FUNCTION();
    pledges.clear();
    proposeTimes.clear();
    statuses.clear();

    if (CPledge* const owner = GetResidenceOwnerPledge()) {
        pledges.push_back(owner);
        proposeTimes.push_back(0);
        statuses.push_back(1);
    }

    for (const auto& entry : m_mapDefenderReservedPledge) {
        CastleSiegeData* const data = entry.second;
        CPledge* pledge = nullptr;
        if (CThreadManager::GetThreadID() < CThreadManager::GetThreadCount()) {
            pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(data->nServerId));
        } else {
            pledge = CPledge::FindPledge(entry.first.m_PrimaryKey, entry.first.m_SecondaryKey).get();
        }
        if (pledge != nullptr && pledge->GetCastle() == nullptr) {
            pledges.push_back(pledge);
            proposeTimes.push_back(data->nProposeTime);
            statuses.push_back(data->nStatus);
        }
    }
}

void CCastle::GetAttackerPledge(std::vector<CPledge*>& pledges, std::vector<int>& proposeTimes) {
    // Current L2Server 0x00599198.
    L2_GUARD_FUNCTION();
    pledges.clear();
    proposeTimes.clear();

    for (const auto& entry : m_mapAttackerReservedPledge) {
        CastleSiegeData* const data = entry.second;
        CPledge* pledge = nullptr;
        if (CThreadManager::GetThreadID() < CThreadManager::GetThreadCount()) {
            pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(data->nServerId));
        } else {
            pledge = CPledge::FindPledge(entry.first.m_PrimaryKey, entry.first.m_SecondaryKey).get();
        }
        if (pledge != nullptr && pledge->GetCastle() == nullptr) {
            pledges.push_back(pledge);
            proposeTimes.push_back(data->nProposeTime);
        }
    }
}

void CCastle::SendCastleSiegeInfo(CUserSocket* const socket, User* const user) {
    // Current L2Server 0x005993a8.
    L2_GUARD_FUNCTION();
    CPledge* const owner = GetResidenceOwnerPledge();
    CResidenceData* const initialData = m_data;
    if (owner == nullptr) {
        const int nextSiege = initialData != nullptr ? initialData->m_NextSiege : 0;
        const std::int32_t now = runtime::crt::Time32Now();
        socket->Send(
            "cdddSSdSddd", 0xc9, m_data != nullptr ? m_data->m_nResidenceID : 0, 0, 0, u"", u"", 0, u"", now, nextSiege, 0
        );
        return;
    }

    const int residenceId = initialData != nullptr ? initialData->m_nResidenceID : 0;
    int canRegister = 0;
    if (user->m_data->m_nPledgeID == owner->GetDbId() && user->m_data->m_nWorldId == owner->GetWorldId() &&
        (user->m_data->m_bPledgeMaster || user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        canRegister = 1;
    }

    const std::uint32_t ownerDatabaseId = owner->GetDbId();
    const char16_t* const ownerName = owner->GetPledgeName();
    const char16_t* const ownerMasterName = owner->GetMasterName();
    std::uint32_t allianceDatabaseId = 0;
    const char16_t* allianceName = u"";
    if (CAlliance* const alliance = owner->GetAlliance()) {
        allianceDatabaseId = alliance->m_data->m_nDbId;
        allianceName = alliance->m_data->m_sAllianceName;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    CResidenceData* const data = m_data;
    const int siegeDate = data != nullptr && data->m_ResidenceState != RS_ANNOUNCING ? data->m_NextSiege : 0;
    std::array<int, 24> siegeTimes{};
    int siegeTimeCount = 0;
    if (canRegister != 0 && m_tPreviousSiegeEnd != 0) {
        siegeTimeCount =
            GetSelectableSiegeTimes(m_tPreviousSiegeEnd, siegeTimes.data(), static_cast<int>(siegeTimes.size()), 0);
    }

    if (siegeTimeCount < 1) {
        socket->Send(
            "cdddSSdSddd", 0xc9, residenceId, canRegister, static_cast<int>(ownerDatabaseId), ownerName, ownerMasterName,
            static_cast<int>(allianceDatabaseId), allianceName, now, siegeDate, 0
        );
    } else {
        socket->Send(
            "cdddSSdSdddb", 0xc9, residenceId, canRegister, static_cast<int>(ownerDatabaseId), ownerName, ownerMasterName,
            static_cast<int>(allianceDatabaseId), allianceName, now, siegeDate, siegeTimeCount, siegeTimeCount << 2,
            siegeTimes.data()
        );
    }
}

void CCastle::SendCastleSiegeAttackerList(CUserSocket* const socket, User* const user) {
    // Current L2Server 0x00599704.
    L2_GUARD_FUNCTION();
    const int residenceId = m_data->m_nResidenceID;
    int canManage = 0;
    if (user->m_data->m_nPledgeID != 0 && user->m_data->m_nPledgeID == m_data->m_nResidenceOwnerID &&
        (user->m_data->m_bPledgeMaster || user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        canManage = 1;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    std::optional<std::vector<CPledge*>> pledgesStorage;
    std::optional<std::vector<int>> proposeTimesStorage;
    {
        L2_READ_GUARD(*this);
        const bool rebuild =
            !m_bAttackerListCached || runtime::SubtractInt32Wrapping(now, m_tAttackerListLastCached) > 0x3b;
        if (rebuild) {
            auto& pledges = pledgesStorage.emplace();
            auto& proposeTimes = proposeTimesStorage.emplace();
            GetAttackerPledge(pledges, proposeTimes);

            const int attackerCount = static_cast<int>(pledges.size());
            const int lastPageCount = attackerCount % 0x40;
            m_nAttackerListTotalPage = attackerCount / 0x40 + 1;
            if (m_nAttackerListTotalPage > 0 && lastPageCount == 0) {
                --m_nAttackerListTotalPage;
            }

            if (m_nAttackerListTotalPage == 0) {
                socket->Send("cdddddd", 0xca, residenceId, canManage, 0, 0, 0, 0);
            } else {
                std::array<char, 0x2008> payload;
                int payloadSize = 0;
                int pageIndex = 0;
                int recordNumber = 1;
                for (int index = 0; index < attackerCount; ++index) {
                    CPledge* const pledge = runtime::CheckedVectorAt(pledges, static_cast<std::size_t>(index));
                    static_cast<void>(pledge->GetDbId());

                    const std::u16string localPledgeName = std::u16string(pledge->GetPledgeName());
                    char16_t remotePledgeName[30];
                    std::memset(remotePledgeName, 0, sizeof(remotePledgeName));
                    const char16_t* pledgeName = localPledgeName.c_str();
                    if (pledge->GetWorldId() != g_nWorldId) {
                        std::u16string formatted = localPledgeName;
                        formatted.push_back(u'_');
                        const std::string world = std::to_string(pledge->GetWorldId());
                        for (const char value : world) {
                            formatted.push_back(static_cast<char16_t>(value));
                        }
                        std::memcpy(remotePledgeName, formatted.c_str(), (formatted.size() + 1) * sizeof(char16_t));
                        pledgeName = remotePledgeName;
                    }

                    const std::u16string pledgeMasterName = std::u16string(pledge->GetMasterName());
                    std::uint32_t allianceDatabaseId = 0;
                    std::uint32_t allianceCrestId = 0;
                    const char16_t* allianceName = u"";
                    std::u16string allianceMasterName;
                    if (CAlliance* const alliance = pledge->GetAlliance()) {
                        allianceDatabaseId = alliance->m_data->m_nDbId;
                        allianceName = alliance->m_data->m_sAllianceName;
                        if (CPledge* const masterPledge = alliance->GetMasterPledge()) {
                            allianceMasterName = std::u16string(masterPledge->GetMasterName());
                        }
                        allianceCrestId = alliance->m_data->m_nCrestId;
                    }

                    payloadSize += ::Assemble(
                        payload.data() + payloadSize, 0x2004 - payloadSize, "dSSdddSSd", static_cast<int>(pledge->Id()),
                        pledgeName, pledgeMasterName.c_str(), static_cast<int>(pledge->GetCrestId()),
                        runtime::CheckedVectorAt(proposeTimes, static_cast<std::size_t>(index)),
                        static_cast<int>(allianceDatabaseId), allianceName, allianceMasterName.c_str(),
                        static_cast<int>(allianceCrestId)
                    );

                    if ((recordNumber & 0x3f) == 0) {
                        BufForCaching& cache = m_bufForAttackerListCaching[pageIndex];
                        cache.nBufSize =
                            ::Assemble(cache.pBuf, 0x2004, "ddb", attackerCount, 0x40, payloadSize, payload.data());
                        socket->Send(
                            "cddddb", 0xca, residenceId, canManage, m_nAttackerListTotalPage, pageIndex, cache.nBufSize,
                            cache.pBuf
                        );
                        payloadSize = 0;
                        ++pageIndex;
                    }
                    ++recordNumber;
                }

                if (lastPageCount != 0) {
                    BufForCaching& cache = m_bufForAttackerListCaching[pageIndex];
                    cache.nBufSize =
                        ::Assemble(cache.pBuf, 0x2004, "ddb", attackerCount, lastPageCount, payloadSize, payload.data());
                    socket->Send(
                        "cddddb", 0xca, residenceId, canManage, m_nAttackerListTotalPage, pageIndex, cache.nBufSize,
                        cache.pBuf
                    );
                }
            }
            m_bAttackerListCached = true;
            m_tAttackerListLastCached = now;
            return;
        }

        if (m_nAttackerListTotalPage == 0) {
            socket->Send("cdddddd", 0xca, residenceId, canManage, 0, 0, 0, 0);
        } else if (m_nAttackerListTotalPage > 0) {
            for (int pageIndex = 0; pageIndex < m_nAttackerListTotalPage; ++pageIndex) {
                const BufForCaching& cache = m_bufForAttackerListCaching[pageIndex];
                socket->Send(
                    "cddddb", 0xca, residenceId, canManage, m_nAttackerListTotalPage, pageIndex, cache.nBufSize, cache.pBuf
                );
            }
        }
    }
}

void CCastle::SendCastleSiegeDefenderList(CUserSocket* const socket, User* const user) {
    // Current L2Server 0x00599ddc.
    L2_GUARD_FUNCTION();
    const int residenceId = m_data->m_nResidenceID;
    int canManage = 0;
    if (user->m_data->m_nPledgeID != 0 && user->m_data->m_nPledgeID == m_data->m_nResidenceOwnerID &&
        (user->m_data->m_bPledgeMaster || user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        canManage = 1;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    std::optional<std::vector<CPledge*>> pledgesStorage;
    std::optional<std::vector<int>> proposeTimesStorage;
    std::optional<std::vector<int>> statusesStorage;
    {
        L2_READ_GUARD(*this);
        const bool rebuild =
            !m_bDefenderListCached || runtime::SubtractInt32Wrapping(now, m_tDefenderListLastCached) > 0x3b;
        if (rebuild) {
            auto& pledges = pledgesStorage.emplace();
            auto& proposeTimes = proposeTimesStorage.emplace();
            auto& statuses = statusesStorage.emplace();
            GetDefenderPledge(pledges, proposeTimes, statuses);

            const int defenderCount = static_cast<int>(pledges.size());
            const int lastPageCount = defenderCount % 0x40;
            m_nDefenderListTotalPage = defenderCount / 0x40 + 1;
            if (m_nDefenderListTotalPage > 0 && lastPageCount == 0) {
                --m_nDefenderListTotalPage;
            }

            if (m_nDefenderListTotalPage == 0) {
                socket->Send("cdddddd", 0xcb, residenceId, canManage, 0, 0, 0, 0);
            } else {
                std::array<char, 0x2008> payload;
                int payloadSize = 0;
                int pageIndex = 0;
                int recordNumber = 1;
                for (int index = 0; index < defenderCount; ++index) {
                    CPledge* const pledge = runtime::CheckedVectorAt(pledges, static_cast<std::size_t>(index));
                    static_cast<void>(pledge->GetDbId());

                    const std::u16string localPledgeName = std::u16string(pledge->GetPledgeName());
                    char16_t remotePledgeName[30];
                    std::memset(remotePledgeName, 0, sizeof(remotePledgeName));
                    const char16_t* pledgeName = localPledgeName.c_str();
                    if (pledge->GetWorldId() != g_nWorldId) {
                        std::u16string formatted = localPledgeName;
                        formatted.push_back(u'_');
                        const std::string world = std::to_string(pledge->GetWorldId());
                        for (const char value : world) {
                            formatted.push_back(static_cast<char16_t>(value));
                        }
                        std::memcpy(remotePledgeName, formatted.c_str(), (formatted.size() + 1) * sizeof(char16_t));
                        pledgeName = remotePledgeName;
                    }

                    const std::u16string pledgeMasterName = std::u16string(pledge->GetMasterName());
                    std::uint32_t allianceDatabaseId = 0;
                    std::uint32_t allianceCrestId = 0;
                    const char16_t* allianceName = u"";
                    std::u16string allianceMasterName;
                    if (CAlliance* const alliance = pledge->GetAlliance()) {
                        allianceDatabaseId = alliance->m_data->m_nDbId;
                        allianceName = alliance->m_data->m_sAllianceName;
                        if (CPledge* const masterPledge = alliance->GetMasterPledge()) {
                            allianceMasterName = std::u16string(masterPledge->GetMasterName());
                        }
                        allianceCrestId = alliance->m_data->m_nCrestId;
                    }

                    payloadSize += ::Assemble(
                        payload.data() + payloadSize, 0x2004 - payloadSize, "dSSddddSSd", static_cast<int>(pledge->Id()),
                        pledgeName, pledgeMasterName.c_str(), static_cast<int>(pledge->GetCrestId()),
                        runtime::CheckedVectorAt(proposeTimes, static_cast<std::size_t>(index)),
                        runtime::CheckedVectorAt(statuses, static_cast<std::size_t>(index)), static_cast<int>(allianceDatabaseId),
                        allianceName, allianceMasterName.c_str(), static_cast<int>(allianceCrestId)
                    );

                    if ((recordNumber & 0x3f) == 0) {
                        BufForCaching& cache = m_bufForDefenderListCaching[pageIndex];
                        cache.nBufSize =
                            ::Assemble(cache.pBuf, 0x2004, "ddb", defenderCount, 0x40, payloadSize, payload.data());
                        socket->Send(
                            "cddddb", 0xcb, residenceId, canManage, m_nDefenderListTotalPage, pageIndex, cache.nBufSize,
                            cache.pBuf
                        );
                        payloadSize = 0;
                        ++pageIndex;
                    }
                    ++recordNumber;
                }

                if (lastPageCount != 0) {
                    BufForCaching& cache = m_bufForDefenderListCaching[pageIndex];
                    cache.nBufSize =
                        ::Assemble(cache.pBuf, 0x2004, "ddb", defenderCount, lastPageCount, payloadSize, payload.data());
                    socket->Send(
                        "cddddb", 0xcb, residenceId, canManage, m_nDefenderListTotalPage, pageIndex, cache.nBufSize,
                        cache.pBuf
                    );
                }
            }
            m_bDefenderListCached = true;
            m_tDefenderListLastCached = now;
            return;
        }

        if (m_nDefenderListTotalPage == 0) {
            socket->Send("cdddddd", 0xcb, residenceId, canManage, 0, 0, 0, 0);
        } else if (m_nDefenderListTotalPage > 0) {
            for (int pageIndex = 0; pageIndex < m_nDefenderListTotalPage; ++pageIndex) {
                const BufForCaching& cache = m_bufForDefenderListCaching[pageIndex];
                socket->Send(
                    "cddddb", 0xcb, residenceId, canManage, m_nDefenderListTotalPage, pageIndex, cache.nBufSize, cache.pBuf
                );
            }
        }
    }
}

void CCastle::SetCastleSiegeRole(const bool initializeCounts) {
    // Current L2Server 0x0059a790.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> defenders;
    std::vector<int> defenderProposeTimes;
    std::vector<int> defenderStatuses;
    std::vector<CPledge*> attackers;
    std::vector<int> attackerProposeTimes;
    GetDefenderPledge(defenders, defenderProposeTimes, defenderStatuses);
    GetAttackerPledge(attackers, attackerProposeTimes);

    m_data->m_DefendingPledgeListSize = 0;
    for (std::size_t index = 0; index < defenders.size(); ++index) {
        if (runtime::CheckedVectorAt(defenderStatuses, index) != 3 && runtime::CheckedVectorAt(defenderStatuses, index) != 1) {
            continue;
        }
        CPledge* const pledge = runtime::CheckedVectorAt(defenders, index);
        PledgeInfo& info = m_data->m_stDefendingPledgeList[static_cast<std::size_t>(m_data->m_DefendingPledgeListSize)];
        info.m_nPledgeID = static_cast<int>(pledge->GetDbId());
        info.m_nWorldID = pledge->GetWorldId();
        ++m_data->m_DefendingPledgeListSize;
        if (initializeCounts) {
            pledge->InitSiegeKillDeathCount();
        }
        pledge->SetCastleUnderSiege(this, 2);
        pledge->SetMemberRelationChanged();
    }

    m_data->m_OffendingPledgeListSize = 0;
    for (CPledge* const pledge : attackers) {
        PledgeInfo& info = m_data->m_stOffendingPledgeList[static_cast<std::size_t>(m_data->m_OffendingPledgeListSize)];
        info.m_nPledgeID = static_cast<int>(pledge->GetDbId());
        info.m_nWorldID = pledge->GetWorldId();
        ++m_data->m_OffendingPledgeListSize;
        if (initializeCounts) {
            pledge->InitSiegeKillDeathCount();
        }
        pledge->SetCastleUnderSiege(this, 1);
        pledge->SetMemberRelationChanged();
    }
}

void CCastle::BroadcastCastleState() {
    // Current L2Server 0x00591c5c.
    L2_GUARD_FUNCTION();
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chdd", 0xfe, 0x12d, m_data != nullptr ? m_data->m_nResidenceID : 0, m_data->m_ResidenceType
    );
}

void CCastle::BroadcastToAttacker(const int messageId) {
    // Current L2Server 0x0059160c.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_data->m_OffendingPledgeListSize; ++index) {
        const PledgeInfo& info = m_data->m_stOffendingPledgeList[static_cast<std::size_t>(index)];
        CSPointer<CPledge> pledge = CPledge::FindPledge(info.m_nWorldID, info.m_nPledgeID);
        if (pledge) {
            pledge->SystemMessage(messageId);
        }
    }
}

void CCastle::BroadcastEndRegister() {
    // Current L2Server 0x005939d8.
    L2_GUARD_FUNCTION();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user->IsInRealWorld()) {
            const short residenceId = static_cast<short>(m_data != nullptr ? m_data->m_nResidenceID : 0);
            user->GetSocket()->Send("chcch", 0x62, SystemMessageId::CASTLE_SIEGE_REGISTRATION_ENDED, 1, 5, residenceId);
        }
    });
    g_npcserver.Send("cddd", 0x26, m_data != nullptr ? m_data->m_nResidenceID : 0, 3, 0);
}

void CCastle::BroadcastSetSiegeTime() {
    // Current L2Server 0x00593ca0.
    L2_GUARD_FUNCTION();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user != nullptr) {
            const short residenceId = static_cast<short>(m_data != nullptr ? m_data->m_nResidenceID : 0);
            user->GetSocket()->Send("chcch", 0x62, SystemMessageId::CASTLE_SIEGE_TIME_ANNOUNCED, 1, 5, residenceId);
        }
    });
}

void CCastle::PassSiege() {
    // Current L2Server 0x00594d74.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        std::fill_n(m_data->m_stDefendingPledgeList.begin(), m_data->m_DefendingPledgeListSize, PledgeInfo{});
        std::fill_n(m_data->m_stOffendingPledgeList.begin(), m_data->m_OffendingPledgeListSize, PledgeInfo{});
        m_data->m_DefendingPledgeListSize = 0;
        m_data->m_OffendingPledgeListSize = 0;
        db.RequestResetCastleSiegePledge(this);
        FreeReserveCastleSiegeList();

        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            if (user->IsInRealWorld()) {
                const short residenceId = static_cast<short>(m_data != nullptr ? m_data->m_nResidenceID : 0);
                user->GetSocket()->Send(
                    "chcch", 0x62, SystemMessageId::CASTLE_SIEGE_CANCELLED_NO_PARTICIPATING_CLANS, 1, 5, residenceId
                );
            }
        });

        int eventResidenceId = 0;
        if (!m_bIsDominionSiege) {
            eventResidenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        } else if (m_pDominion != nullptr && m_pDominion->m_data != nullptr) {
            eventResidenceId = m_pDominion->m_data->m_nResidenceID;
        }
        g_npcserver.Send("cddd", 0x26, eventResidenceId, 5, 0);

        m_nLostTime = 0;
        m_nSiegeElapsed = 0;
        for (CDoor* const door : m_DoorList) {
            door->ResetDoorState(false, 1.0);
        }
        MakeDoorsBreakable(false);
        if (m_data->m_nResidenceOwnerID != 0) {
            CSPointer<CPledge> owner =
                CPledge::FindPledge(m_data->m_nResidenceOwnerWorldId, static_cast<int>(m_data->m_nResidenceOwnerID));
            OnDefenceSiegeSuccess(owner);
        }
    }
}

void CCastle::RegistrationOnTimerExpired(const std::int32_t now) {
    // Current L2Server 0x00595138.
    L2_GUARD_FUNCTION();
    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
    if (nextSiege - BP::_CASTLE_STANDBY_TIME < now) {
        ChangeResidenceState(RS_STANDBY);
        BroadcastEndRegister();
    }
}

void CCastle::StandByOnTimerExpired(const std::int32_t now) {
    // Current L2Server 0x0059b3c0 keeps the duplicated comparison.
    L2_GUARD_FUNCTION();
    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
    if (((now <= nextSiege - 10 || nextSiege < now) && nextSiege < now) &&
        g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        if (GetReservedAttackerCount() < 1) {
            PassSiege();
            SetSiegeDate(0);
            m_tPreviousSiegeEnd = nextSiege;
            int automaticTime = 0;
            const int castleId = m_data != nullptr ? m_data->m_nResidenceID : 0;
            if (GetSelectableSiegeTimes(nextSiege, &automaticTime, 1, castleId) == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot Set Siege Time castle[%d] previous[%d] "
                //     L"autoTime[%d]",
                //     castleId,
                //     m_tPreviousSiegeEnd,
                //     automaticTime)
                // L2_LOG_ERROR(Log,
                //     L"Cannot Set Siege Time castle[%d] previous[%d] "
                //     L"autoTime[%d]",
                //     castleId,
                //     m_tPreviousSiegeEnd,
                //     automaticTime)
                L2_LOG_ERROR(
                    Log,
                    "Castle %d: no selectable siege time after an uncontested siege (previous end %d, candidate %d; Unix "
                    "seconds); switching to ANNOUNCING.",
                    castleId, m_tPreviousSiegeEnd, automaticTime
                );
                ChangeResidenceState(RS_ANNOUNCING);
            } else {
                db.RequestSetNextCastleSiege(nullptr, this, automaticTime);
                SetSiegeDate(automaticTime);
                BroadcastSetSiegeTime();
                ChangeResidenceState(RS_TYPE_CHANGE);
            }

            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] [NO_ERROR][CASTLE][PassSiege]  StartSiege "
            //     L"CastleID[%d] AttackerCount[%d]",
            //     L"Castle.cpp",
            //     0x7ea,
            //     castleId,
            //     GetReservedAttackerCount())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] [NO_ERROR][CASTLE][PassSiege]  StartSiege "
            //     L"CastleID[%d] AttackerCount[%d]",
            //     L"Castle.cpp",
            //     0x7ea,
            //     castleId,
            //     GetReservedAttackerCount())
            L2_LOG_INFO(
                Log, "Castle %d: siege passed without registered attackers; preparing the NPC-server notification.", castleId
            );
            g_npcserver.Send("cddd", 0x26, castleId, 9, 0);
        } else {
            StartSiege();
            ChangeResidenceState(m_bMidVictory ? RS_MID_VICTORY : RS_SIEGE);
        }
    }
}

void CCastle::AnnouncingOnTimerExpired(const std::int32_t now) {
    // Current L2Server 0x0059b60c.
    L2_GUARD_FUNCTION();
    if (m_tPreviousSiegeEnd == 0 || m_tPreviousSiegeEnd + BP::_CASTLE_ANNOUNCE_TIME >= now) {
        return;
    }

    int automaticTime = 0;
    const int castleId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    if (GetSelectableSiegeTimes(m_tPreviousSiegeEnd, &automaticTime, 1, castleId) == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot Set Siege Time castle[%d] previous[%d] "
        //     L"autoTime[%d]",
        //     castleId,
        //     m_tPreviousSiegeEnd,
        //     automaticTime)
        // L2_LOG_ERROR(Log,
        //     L"Cannot Set Siege Time castle[%d] previous[%d] "
        //     L"autoTime[%d]",
        //     castleId,
        //     m_tPreviousSiegeEnd,
        //     automaticTime)
        L2_LOG_ERROR(
            Log,
            "Castle %d: no selectable siege time during ANNOUNCING (previous end %d, candidate %d; Unix seconds); "
            "scheduling remains unchanged.",
            castleId, m_tPreviousSiegeEnd, automaticTime
        );
        return;
    }

    db.RequestSetNextCastleSiege(nullptr, this, automaticTime);
    SetSiegeDate(automaticTime);
    ChangeResidenceState(RS_REGISTRATION);
    BroadcastSetSiegeTime();
}

void CCastle::ValidateStatus() {
    // Current L2Server 0x0059b768.
    L2_GUARD_FUNCTION();
    if (m_nValidate == 0) {
        return;
    }
    if (m_nValidate == 1) {
        const int previousWaiting = m_nValidateWaiting--;
        if (previousWaiting > 0) {
            return;
        }
        ValidateStartSiege();
    } else if (m_nValidate == 2) {
        const int previousWaiting = m_nValidateWaiting--;
        if (previousWaiting > 0) {
            return;
        }
        ValidateChangeOwner();
    }
    m_nValidate = 0;
    m_nValidateWaiting = 0;
}

void CCastle::StartTimer() {
    // Current L2Server 0x0059b94c.
    L2_GUARD_FUNCTION();
    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
    m_tPreviousSiegeEnd = 0;
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    if (state == RS_ANNOUNCING) {
        SetSiegeDate(0);
        if (nextSiege != 0) {
            m_tPreviousSiegeEnd = nextSiege + m_nSiegePeriod;
        }
    } else if (IsUnderSiege()) {
        m_bSiegeContinued = true;
        if (CDominionManager::Inst()->m_nState != DSST_DOMINION_SIEGE) {
            if (state == RS_MID_VICTORY) {
                m_bMidVictory = true;
                g_aHadMidVictory[static_cast<std::size_t>(m_data->m_nResidenceID)] = 1;
            }
            if (m_data != nullptr) {
                m_data->m_ResidenceState = RS_STANDBY;
            }
            PrepareSiege();
        }
    }
    AddTimer(10000, 0);
}

void CCastle::TimerExpired(const int timerId) {
    // Current L2Server 0x0059babc.
    L2_GUARD_FUNCTION();
    static_cast<void>(timerId);

    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    const std::int32_t now = runtime::crt::Time32Now();

    if (state == RS_ANNOUNCING) {
        AnnouncingOnTimerExpired(now);
    } else if (state == RS_REGISTRATION) {
        RegistrationOnTimerExpired(now);
    } else if (state == RS_STANDBY) {
        StandByOnTimerExpired(now);
    } else if (state == RS_SIEGE || state == RS_MID_VICTORY) {
        const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
        const std::int32_t siegePeriod = runtime::Int32FromBits(m_nSiegePeriod);
        const std::int32_t endTime =
            runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(nextSiege, siegePeriod), m_nLostTime);

        if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20) && m_bPausable) {
            PauseSiege();
        } else if (m_bPaused) {
            ResumeSiege();
        } else {
            if (m_pNewOwnerPledge != nullptr) {
                OnChangeOwner();
            }

            const std::int32_t remaining = runtime::SubtractInt32Wrapping(endTime, now);
            if (remaining < 11 && now < endTime) {
                m_HolyThingList[0]->SendCountDownMessage(0x168, remaining);
            } else if (static_cast<std::uint32_t>(remaining) - 59U < 2U && (m_nSiegeInform & 0x20) == 0) {
                m_HolyThingList[0]->SendCountDownMessage(0x167, 1);
                m_nSiegeInform = 0x3e;
            } else if (static_cast<std::uint32_t>(remaining) - 299U < 2U && (m_nSiegeInform & 0x10) == 0) {
                m_HolyThingList[0]->SendCountDownMessage(0x167, 5);
                m_nSiegeInform = 0x1e;
            } else if (static_cast<std::uint32_t>(remaining) - 599U < 2U && (m_nSiegeInform & 0x08) == 0) {
                m_HolyThingList[0]->SendCountDownMessage(0x167, 10);
                m_nSiegeInform = 0x0e;
            } else if (static_cast<std::uint32_t>(remaining) - 1799U < 2U && (m_nSiegeInform & 0x04) == 0) {
                m_HolyThingList[0]->SendCountDownMessage(0x167, 30);
                m_nSiegeInform = 0x06;
            } else if (static_cast<std::uint32_t>(remaining) - 3599U < 2U && (m_nSiegeInform & 0x02) == 0) {
                m_HolyThingList[0]->SendCountDownMessage(0x166, 1);
                m_nSiegeInform = 0x02;
            }

            if (endTime <= now) {
                EndSiege();
                SetSiegeDate(0);
                m_tPreviousSiegeEnd = endTime;

                int automaticTime = 0;
                const int castleId = m_data != nullptr ? m_data->m_nResidenceID : 0;
                if (GetSelectableSiegeTimes(endTime, &automaticTime, 1, castleId) == 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Cannot Set Siege Time castle[%d] previous[%d] "
                    //     L"autoTime[%d]",
                    //     castleId,
                    //     m_tPreviousSiegeEnd,
                    //     automaticTime)
                    // L2_LOG_ERROR(Log,
                    //     L"Cannot Set Siege Time castle[%d] previous[%d] "
                    //     L"autoTime[%d]",
                    //     castleId,
                    //     m_tPreviousSiegeEnd,
                    //     automaticTime)
                    L2_LOG_ERROR(
                        Log,
                        "Castle %d: no selectable next siege time after the siege ended (previous end %d, candidate %d; "
                        "Unix seconds); switching to ANNOUNCING.",
                        castleId, m_tPreviousSiegeEnd, automaticTime
                    );
                    ChangeResidenceState(RS_ANNOUNCING);
                } else {
                    db.RequestSetNextCastleSiege(nullptr, this, automaticTime);
                    SetSiegeDate(automaticTime);
                    ChangeResidenceState(RS_TYPE_CHANGE);
                    BroadcastSetSiegeTime();
                }
                g_npcserver.Send("cddd", 0x26, castleId, 9, 0);
                m_nSiegePeriod = static_cast<std::uint32_t>(BP::_CASTLE_IN_WAR_TIME);
            } else {
                if (m_nDbSaveTick == (m_nDbSaveTick / 10) * 10) {
                    m_nSiegeElapsed =
                        runtime::SubtractInt32Wrapping(runtime::SubtractInt32Wrapping(now, m_nLostTime), nextSiege);
                    if (m_nSiegeElapsed < 0) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Problem occurred in Lost Time[%d], Siege "
                        //     L"Elapsed Time[%d], i recommend you to "
                        //     L"relaunch... -_-..",
                        //     m_nLostTime,
                        //     m_nSiegeElapsed)
                        // L2_LOG_ERROR(Log,
                        //     L"Problem occurred in Lost Time[%d], Siege "
                        //     L"Elapsed Time[%d], i recommend you to "
                        //     L"relaunch... -_-..",
                        //     m_nLostTime,
                        //     m_nSiegeElapsed)
                        L2_LOG_ERROR(
                            Log,
                            "Castle %d: calculated siege elapsed time is negative (%d seconds; lost time %d, start %d, now "
                            "%d). Resetting elapsed time to 0 before requesting persistence.",
                            m_data != nullptr ? m_data->m_nResidenceID : 0, m_nSiegeElapsed, m_nLostTime, nextSiege, now
                        );
                        m_nSiegeElapsed = 0;
                    }
                    db.RequestSaveCastleSiegeElapsedTime(*this, m_nSiegeElapsed);
                    db.RequestSaveAllControlTowerStatus(this);
                }
                m_nDbSaveTick = runtime::AddInt32Wrapping(m_nDbSaveTick, 1);
            }

            const wchar_t* const musicName = g_CastleSiegeMusicPlayer[0].GetMusicName(
                runtime::SubtractInt32Wrapping(runtime::Int32FromBits(m_nSiegePeriod), remaining)
            );
            if (m_MusicName != musicName) {
                m_MusicName = musicName;
                if (musicName != nullptr) {
                    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                        if (user->IsInRealWorld() && IsInBattleField(user->GetPos(), true)) {
                            user->PlayMusic(WideToUtf16(m_MusicName).c_str());
                        }
                    });
                }
            }
        }
    } else if (state == RS_TYPE_CHANGE) {
        ChangeCastleTypeOnTimerExpired(now);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Unknown Castle[%d] State %d",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"Unknown Castle[%d] State %d",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log,
            "Castle %d has unsupported state %d; remaining timer work is skipped and the next check is scheduled in 60 "
            "seconds.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<int>(state)
        );
        AddTimer(60000, 0);
        return;
    }

    ValidateStatus();
    DoDecoCheck(static_cast<std::uint32_t>(now));
    DoDecoNpcCheck(now);
    AddTimer(1000, 0);
}

void CCastle::StartSiege() {
    // Current L2Server 0x0059aa70.
    L2_GUARD_FUNCTION();
    if (!m_bIsDominionSiege) {
        static_cast<void>(SSQSystem::inst().OnCastleSiegeStart(this));
    }

    int ownerPledgeId;
    int ownerWorldId;
    {
        L2_WRITE_GUARD(*this);
        m_bHappenDestructTowerEvent = false;
        m_pNewOwnerPledge = nullptr;
        m_eNewResidenceType = CastleType::CASTLE_NOTHING;

        if (!m_bIsDominionSiege) {
            m_nPrevDominionStatus = m_pDominion != nullptr ? m_pDominion->m_data->m_nDominionStatus : 0;
            if (!m_bSiegeContinued) {
                m_PreviousOwnerPledgeDbId = static_cast<int>(m_data->m_nResidenceOwnerID);
                m_nPrevOwnerPledgeWorldId = m_data->m_nResidenceOwnerWorldId;
                SharedCastleInfo* const sharedCastle = SharedCastleInfo::GetInfo(m_data->m_nResidenceID);
                sharedCastle->m_nPreviousOwnerId = m_PreviousOwnerPledgeDbId;
                sharedCastle->m_nPreviousOwnerWorldId = m_nPrevOwnerPledgeWorldId;
                db.RequestSetPreviousCastleOwnerID(*this);
                if (!m_bSiegeContinued) {
                    SetCastleSiegeRole(true);
                }
            }
            if (!m_bMidVictory) {
                BroadcastToAttacker(0x4a5);
            }
        } else {
            m_bPaused = false;
        }

        CResidenceData* const data = m_data;
        ownerPledgeId = static_cast<int>(data->m_nResidenceOwnerID);
        ownerWorldId = data->m_nResidenceOwnerWorldId;
        const bool continued = m_bSiegeContinued;

        if (!m_bIsDominionSiege) {
            g_npcserver.Send("cddd", 0x26, data != nullptr ? data->m_nResidenceID : 0, 1, continued ? 1 : 0);
            if (!continued) {
                BanNonDefendingPlayers(BAN_TYPE_0);
                SetBattleField(true);
            } else {
                SetFreezeField(false);
            }
            MakeNPCDisable(true);
        } else {
            int dominionId = 0;
            if (m_pDominion != nullptr && m_pDominion->m_data != nullptr) {
                dominionId = m_pDominion->m_data->m_nResidenceID;
            }
            g_npcserver.Send("cddd", 0x26, dominionId, 1, continued ? 1 : 0);
            if (m_pDominion != nullptr && m_pDominion->IsExtended()) {
                int extendedDominionId = 0;
                if (m_pDominion->m_data != nullptr) {
                    extendedDominionId = m_pDominion->m_data->m_nResidenceID;
                }
                g_npcserver.Send("cddd", 0x26, extendedDominionId, 6, continued ? 1 : 0);
            }
            if (!continued) {
                SetBattleField(true);
            } else {
                SetFreezeField(false);
            }
        }

        for (CDoor* const door : m_DoorList) {
            if (!continued) {
                door->SetHP(door->m_data->m_dMaxHP);
            }
            door->ValidateDamageGrade();
            door->ValidatePlaneCollision();
            door->Close(false, false);
        }
        MakeDoorsBreakable(true);
        for (CControlTower* const tower : m_ControlTowerList) {
            tower->OnSiegeStarted(continued);
        }
        ValidateLifeControlLevel(false);

        if (!m_bIsDominionSiege) {
            const std::int32_t elapsed = runtime::SubtractInt32Wrapping(
                runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), data->m_NextSiege), m_nSiegeElapsed
            );
            if (elapsed > 5) {
                m_nLostTime = runtime::AddInt32Wrapping(m_nLostTime, elapsed);
            }

            FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                if (!user->IsInRealWorld()) {
                    continue;
                }

                const std::uint32_t castleIndex = static_cast<std::uint32_t>(m_data->m_nResidenceID) - 1U;
                int castleSiegeId = -1;
                if (castleIndex < 10U) {
                    castleSiegeId = castleIndex < 9U ? user->m_nCastleSiegeId[castleIndex] : user->m_nCastleSiegeStatus[0];
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] castle_index is wrong!! [%d]",
                    //     L"c:\\work\\lineage2build\\server\\main\\"
                    //     L"l2server\\user.h",
                    //     0xd1d,
                    //     castleIndex)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] castle_index is wrong!! [%d]",
                    //     L"c:\\work\\lineage2build\\server\\main\\"
                    //     L"l2server\\user.h",
                    //     0xd1d,
                    //     castleIndex)
                    L2_LOG_ERROR(
                        Log,
                        "Castle %d: siege-start notification has invalid character siege index %u (expected 0..9); "
                        "reporting the call stack and continuing with siege ID -1.",
                        m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(castleIndex)
                    );
                    runtime::process::ReportNamedCallStack(L"Siege", LOG_ERROR, false);
                }

                CUserSocket* const socket = user->GetSocket();
                const short residenceId = static_cast<short>(m_data != nullptr ? m_data->m_nResidenceID : 0);
                if (castleSiegeId == (m_data != nullptr ? m_data->m_nResidenceID : 0)) {
                    socket->Send("chcch", 0x62, SystemMessageId::JOINED_NAMED_CASTLE_SIEGE_WITH_DURATION, 1, 5, residenceId);
                    user->OnDailyMissionCheckAndUpdate(E_DAILY_MISSION_TYPE_CASTLEWAR);
                } else {
                    socket->Send("chcch", 0x62, SystemMessageId::NAMED_CASTLE_SIEGE_STARTED, 1, 5, residenceId);
                }

                if (IsInBattleField(user->GetPos(), true)) {
                    const FVector position = user->GetPos();
                    user->PlaySound(
                        L"itemsound.siege_start", 1, static_cast<int>(user->m_nId),
                        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                        runtime::Win64CvttDoubleToInt32(position.z)
                    );
                }
                SendCastleSiegeDefenderList(user->GetSocket(), user);
            });
            m_nSiegeInform = 0;
        }

        if (m_data != nullptr && m_data->m_nResidenceOwnerID != 0 && !m_bIsDominionSiege) {
            if (continued) {
                db.RequestLoadMercenary(*this);
            } else {
                for (auto& entry : m_GuardNumMap) {
                    entry.second = 0;
                }
                for (const auto& entry : m_mapResidenceGuardData) {
                    ResidenceGuardData* const guardData = entry.second;
                    SummonMercenary(entry.first, guardData);
                    CItem* const item = ItemSmartIdManager()->GetObjectW(guardData->nItemServerId);
                    if (item != nullptr) {
                        CWorld::Inst().DoAsyncPickItem(item);
                    }
                    std::free(guardData);
                }
                m_mapResidenceGuardData.clear();
                m_mapResidenceGuardDataCheck.clear();
                db.RequestDeleteGuardData(*this);
            }
        }

        m_bSiegeContinued = false;
    }
    m_nValidate = 1;
    m_nValidateWaiting = 3;

    if (!m_bIsDominionSiege) {
        char16_t pledgeName[25]{};
        CSPointer<CPledge> owner = CPledge::FindPledge(ownerWorldId, ownerPledgeId);
        if (owner) {
            std::u16string_view(owner->GetPledgeName()).substr(0, 24).copy(pledgeName, 24);
            pledgeName[24] = u'\0';
        }
        Log.Add(
            LOG_AUDIT, u"%I64d,,,,,,,,%s,,,%I64d,%I64d,,,,,,,,,,,,,", static_cast<std::int64_t>(0x581), pledgeName,
            static_cast<std::int64_t>(ownerPledgeId),
            static_cast<std::int64_t>(m_data != nullptr ? m_data->m_nResidenceID : 0)
        );
        RelayClient::Inst()->SendSiegeStartPacket(m_data != nullptr ? m_data->m_nResidenceID : 0);
        InitKillScore();
    }
}

void CCastle::EndSiege() {
    // Current L2Server 0x005972c4.
    L2_GUARD_FUNCTION();
    if (!m_bIsDominionSiege) {
        static_cast<void>(SSQSystem::inst().OnCastleSiegeEnd(this));
    }

    std::optional<CSPointer<CPledge>> winnerStorage;
    std::optional<CSPointer<CPledge>> previousOwnerStorage;
    char16_t winnerName[25];
    {
        L2_WRITE_GUARD(*this);
        auto& winner = winnerStorage.emplace();
        auto& previousOwner =
            previousOwnerStorage.emplace(CPledge::FindPledge(m_nPrevOwnerPledgeWorldId, m_PreviousOwnerPledgeDbId));
        std::fill_n(winnerName, 25, wchar_t{});

        if (!m_bIsDominionSiege) {
            db.ReqOwnCastle(
                this, static_cast<int>(m_data->m_nResidenceOwnerID), m_PreviousOwnerPledgeDbId, m_data->m_ResidenceType,
                m_data->m_nResidenceOwnerWorldId, m_nPrevOwnerPledgeWorldId
            );

            if (m_data->m_nResidenceOwnerID != 0) {
                winner =
                    CPledge::FindPledge(m_data->m_nResidenceOwnerWorldId, static_cast<int>(m_data->m_nResidenceOwnerID));
            }
            if (winner) {
                winner->SetCastle(this);
                winner->Save(6);
                std::fill_n(winnerName, 25, u'\0');
                std::u16string_view(winner->GetPledgeName()).substr(0, 24).copy(winnerName, 24);

                PledgeMission::GoalType missionGoal;
                if (static_cast<int>(winner->GetDbId()) == m_PreviousOwnerPledgeDbId &&
                    static_cast<int>(winner->GetDbId()) == m_PreviousOwnerPledgeDbId &&
                    winner->GetWorldId() == m_nPrevOwnerPledgeWorldId) {
                    if (m_PreviousOwnerPledgeDbId != 0) {
                        OnDefenceSiegeSuccess(CPledge::FindPledge(m_nPrevOwnerPledgeWorldId, m_PreviousOwnerPledgeDbId));
                    }
                    missionGoal = winner->GetWorldId() == g_nWorldId ? PledgeMission::PMGT_CASTLEWAR_DEF_WIN
                                                                     : PledgeMission::PMGT_W_CASTLEWAR_DEF_WIN;
                } else {
                    if (m_PreviousOwnerPledgeDbId == 0) {
                        if (winner->GetFunction()->IsEnable(PledgeFunctionType::CASTLE_SIEGE)) {
                            if (m_pDominion != nullptr) {
                                g_npcserver.Send(
                                    "cddd", 0x26, m_pDominion->m_data != nullptr ? m_pDominion->m_data->m_nResidenceID : 0,
                                    8, 0
                                );
                            }
                            PledgeStuff::UpdateNameValueOf(winner.get(), NameValueForCastle::SUCCESS_FROM_NPC);
                        }
                    } else if (previousOwner && previousOwner->GetFunction()->IsEnable(PledgeFunctionType::CASTLE_SIEGE)) {
                        if (winner->GetFunction()->IsEnable(PledgeFunctionType::CASTLE_SIEGE)) {
                            PledgeStuff::UpdateNameValueOf(winner.get(), NameValueForCastle::SUCCESS_FROM_PC);
                        }
                        PledgeStuff::UpdateNameValueOf(previousOwner.get(), NameValueForCastle::FAILED_DEFENCE);
                    }
                    missionGoal = winner->GetWorldId() == g_nWorldId ? PledgeMission::PMGT_CASTLEWAR_ATK_WIN
                                                                     : PledgeMission::PMGT_W_CASTLEWAR_ATK_WIN;
                }

                winner->PledgeMissionCheckAndAddCountInCastleSiege(missionGoal, 0, 0, 1);
                GIP._Add(
                    L"CMD_siegeWarfareInfo(%d, %d, %d, %d", g_nWorldId, m_data != nullptr ? m_data->m_nResidenceID : 0,
                    static_cast<int>(winner->GetDbId()), static_cast<std::int64_t>(std::time(nullptr))
                );
            }
        }

        CBattleCampDb::inst().SetDisable(static_cast<std::uint32_t>(m_data->m_nResidenceID));
        db.RequestUninstallBattleCampBy(this);
        m_nLostTime = 0;
        m_nSiegeElapsed = 0;
        db.RequestSaveCastleSiegeElapsedTime(*this, 0);

        if (!m_bIsDominionSiege) {
            FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                if (user->IsInRealWorld()) {
                    const std::uint32_t castleIndex = static_cast<std::uint32_t>(m_data->m_nResidenceID) - 1U;
                    int castleSiegeId = -1;
                    if (castleIndex < 10U) {
                        castleSiegeId =
                            castleIndex < 9U ? user->m_nCastleSiegeId[castleIndex] : user->m_nCastleSiegeStatus[0];
                    } else {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] castle_index is wrong!! [%d]",
                        //     L"c:\\work\\lineage2build\\server\\main\\"
                        //     L"l2server\\user.h",
                        //     0xd1d,
                        //     castleIndex)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d] castle_index is wrong!! [%d]",
                        //     L"c:\\work\\lineage2build\\server\\main\\"
                        //     L"l2server\\user.h",
                        //     0xd1d,
                        //     castleIndex)
                        L2_LOG_ERROR(
                            Log,
                            "Castle %d: siege-end notification has invalid character siege index %u (expected 0..9); "
                            "reporting the call stack and continuing with siege ID -1.",
                            m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(castleIndex)
                        );
                        runtime::process::ReportNamedCallStack(L"Siege", LOG_ERROR, false);
                    }

                    CUserSocket* const socket = user->GetSocket();
                    const short residenceId = static_cast<short>(m_data != nullptr ? m_data->m_nResidenceID : 0);
                    if (castleSiegeId == (m_data != nullptr ? m_data->m_nResidenceID : 0)) {
                        socket->Send("chcch", 0x62, SystemMessageId::PARTICIPATING_CASTLE_SIEGE_ENDED, 1, 5, residenceId);
                        user->KillPVPPointChecker.ResetAll(user);
                    } else {
                        socket->Send("chcch", 0x62, SystemMessageId::NAMED_CASTLE_SIEGE_ENDED, 1, 5, residenceId);
                    }

                    if (winnerName[0] != u'\0') {
                        user->GetSocket()->Send(
                            "chccSch", 0x62, SystemMessageId::CLAN_WON_CASTLE_SIEGE, 2, 0,
                            std::u16string(winnerName).c_str(), 5, residenceId
                        );
                    }
                    if (IsInBattleField(user->GetPos(), true)) {
                        const FVector position = user->GetPos();
                        user->PlaySound(
                            L"itemsound.siege_end", 1, static_cast<int>(user->m_nId),
                            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                            runtime::Win64CvttDoubleToInt32(position.z)
                        );
                    }
                }

                user->GetSocket()->Send(
                    "chdd", 0xfe, 0x12d, m_data != nullptr ? m_data->m_nResidenceID : 0, m_data->m_ResidenceType
                );
            });
            if (winner) {
                winner->PlayMemberMusic();
            }
        }

        db.RequestDeleteMercenary(*this);
        if (!m_bIsDominionSiege) {
            BanNonDefendingPlayers(BAN_TYPE_0);
            MakeNPCDisable(false);
        }
        for (CDoor* const door : m_DoorList) {
            door->ResetDoorState(false, 1.0);
        }
        MakeDoorsBreakable(false);
        for (CControlTower* const tower : m_ControlTowerList) {
            tower->OnSiegeEnded();
        }
        ValidateLifeControlLevel(false);
        SetBattleField(false);

        if (!m_bIsDominionSiege) {
            std::vector<User*> defenders;
            std::vector<User*> attackers;
            SetDefendingCastle(false, defenders);
            for (User* const user : defenders) {
                if (user->m_data->m_nNoblessType == 1 || user->m_data->m_nNoblessType == 2) {
                    if (m_data->m_ResidenceState == RS_SIEGE) {
                        user->WriteNoblessAchievement(WIN_CASTLE_DEFENCE, m_data->m_nResidenceID);
                    } else if (m_data->m_ResidenceState == RS_MID_VICTORY) {
                        user->WriteNoblessAchievement(WIN_CASTLE_ATTACK, m_data->m_nResidenceID);
                    }
                }
                user->SetRelationChanged(false);
            }
            SetOffendingCastle(false, attackers);
            for (User* const user : attackers) {
                user->SetRelationChanged(false);
            }

            std::memset(
                m_data->m_stDefendingPledgeList.data(), 0,
                static_cast<std::size_t>(
                    static_cast<std::int64_t>(m_data->m_DefendingPledgeListSize) *
                    static_cast<std::int64_t>(sizeof(PledgeInfo))
                )
            );
            std::memset(
                m_data->m_stOffendingPledgeList.data(), 0,
                static_cast<std::size_t>(
                    static_cast<std::int64_t>(m_data->m_OffendingPledgeListSize) *
                    static_cast<std::int64_t>(sizeof(PledgeInfo))
                )
            );
            m_data->m_DefendingPledgeListSize = 0;
            m_data->m_OffendingPledgeListSize = 0;
            db.RequestResetCastleSiegePledge(this);
            FreeReserveCastleSiegeList();

            if (winner) {
                winner->ResetCastleSiege();
                db.RequestEndSiege(
                    *this, static_cast<int>(winner->GetDbId()), m_data->m_ResidenceType, winner->GetWorldId()
                );
                CancelAttackSiegeAtAllCastle(winner);
            }
        }

        bool sendEndEvent = !m_bIsDominionSiege;
        int eventResidenceId = 0;
        if (!m_bIsDominionSiege) {
            eventResidenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        } else if (!m_bSupportBroken) {
            sendEndEvent = true;
            if (m_pDominion != nullptr && m_pDominion->m_data != nullptr) {
                eventResidenceId = m_pDominion->m_data->m_nResidenceID;
            }
        }
        if (sendEndEvent) {
            g_npcserver.Send("cddd", 0x26, eventResidenceId, 2, 0);
        }

        m_bSupportBroken = false;
        FreeSiegeGolem();
        m_pNewOwnerPledge = nullptr;
        m_eNewResidenceType = CastleType::CASTLE_NOTHING;
        m_bMidVictory = false;
        g_aHadMidVictory[static_cast<std::size_t>(m_data->m_nResidenceID)] = 0;
    }

    if (!m_bIsDominionSiege) {
        CSPointer<CPledge> owner =
            CPledge::FindPledge(m_data->m_nResidenceOwnerWorldId, static_cast<int>(m_data->m_nResidenceOwnerID));
        if (owner && owner->m_pFortress != nullptr) {
            owner->m_pFortress->ResetOwnership();
        }
        const int ownerPledgeId = owner ? static_cast<int>(owner->GetDbId()) : 0;
        Log.Add(
            LOG_AUDIT, u"%I64d,,,,,,,,%s,,,%I64d,%I64d,%I64d,,,,,,,,,,,,", static_cast<std::int64_t>(0x583), winnerName,
            static_cast<std::int64_t>(ownerPledgeId),
            static_cast<std::int64_t>(m_data != nullptr ? m_data->m_nResidenceID : 0),
            static_cast<std::int64_t>(m_data->m_ResidenceType)
        );
        RelayClient::Inst()->SendSiegeEndPacket(m_data != nullptr ? m_data->m_nResidenceID : 0);
        InitKillScore();
    }
}

void CCastle::OnStartInBattleField(User* const user, FVector& position, bool& started) {
    // Current L2Server 0x00591d30.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    const std::int32_t inputX = runtime::Win64CvttDoubleToInt32(position.x);
    const std::int32_t inputY = runtime::Win64CvttDoubleToInt32(position.y);
    CGeoData* const inputGeo = CWorld::Inst().GetGeo();
    CGeoZone* const zone = inputGeo->GetZone(
        (runtime::AddInt32Wrapping(inputX, 0x50000) >> 15) + 10,
        (runtime::AddInt32Wrapping(inputY, 0x40000) >> 15) + 10
    );
    if (zone != nullptr) {
        CGeoSector* const sector = zone->GetSector((inputX >> 7) & 0xff, (inputY >> 7) & 0xff);
        if (sector != nullptr && !AreaBasedRule::StartIn(sector, position)) {
            return;
        }
    }

    const FVector currentPosition = user->m_data->GetPos();
    CGeoData* const currentGeo = CWorld::Inst().GetGeo();
    if (currentGeo->IsField(
            runtime::Win64CvttDoubleToInt32(currentPosition.x), runtime::Win64CvttDoubleToInt32(currentPosition.y),
            runtime::Win64CvttDoubleToInt32(currentPosition.z), GEO_FIELD_FREEZE
        )) {
        return;
    }

    const std::uint32_t firstRandom = static_cast<std::uint32_t>(::LegacyCrtRand());
    const int sealOwner = static_cast<int>(SSQSystem::inst().GetSealOwner(SSQSeal::STRIFE));
    if (sealOwner >= SSQPart::NONE && sealOwner < SSQPart::DAWN) {
        position = Singleton<CSetting>::inst().GetRestartPoint(
            user->m_data->m_Race, currentPosition, user->m_data->m_nKarma < 0, user->m_data->m_nPKCount
        );
    } else if (sealOwner == SSQPart::DAWN) {
        auto& restartPoints = !user->m_bEinhasadeTransformed && user->m_data->m_nKarma >= 0 ? m_OtherRestartVillageList
                                                                                            : m_ChaoRestartPointList;
        const std::size_t index =
            static_cast<std::size_t>(runtime::Win64UnsignedRemainder(firstRandom, restartPoints.size()));
        position = restartPoints[index];
    }
    started = true;

    const int pledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    const int userWorldId = user->m_data->m_nWorldId;
    CDominionManager* const dominionManager = CDominionManager::Inst();
    const int castleId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    if (dominionManager->m_nState == DSST_DOMINION_SIEGE) {
        int dominionId = dominionManager->GetPledgeDominionSiegeID(pledgeId);
        if (dominionId == 0) {
            dominionId = dominionManager->GetUserDominionSiegeID(static_cast<int>(user->GetServerId()));
        }
        if (dominionId != 0) {
            CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
            if (dominion != nullptr) {
                CPledge* const owner = dominion->GetResidenceOwnerPledge();
                if (owner != nullptr) {
                    CSPointer<CBattleCamp> battleCamp = owner->GetBattleCamp();
                    if (battleCamp && battleCamp->IsEnabled() && battleCamp->GetBaseResidenceId() == castleId) {
                        position = battleCamp->GetTeleportPos();
                        started = true;
                        return;
                    }
                }
            }
        }
    }

    if (pledgeId == 0) {
        return;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(userWorldId, pledgeId);
    if (!pledge) {
        return;
    }

    bool useOwnerRestart = false;
    if (pledge->IsDefender(this)) {
        useOwnerRestart = !m_bMidVictory || user->GetSSQPart() != SSQPart::TWILIGHT;
    } else if (pledge->IsAttacker(this)) {
        CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
        if (battleCamp && battleCamp->IsEnabled() && battleCamp->GetBaseResidenceId() == castleId) {
            position = battleCamp->GetTeleportPos();
            started = true;
        }
        return;
    } else if (dominionManager->m_nState == DSST_DOMINION_SIEGE && static_cast<int>(pledge->GetCastleDbId()) == castleId) {
        useOwnerRestart = true;
    }

    if (!useOwnerRestart) {
        return;
    }
    const std::uint32_t secondRandom = static_cast<std::uint32_t>(::LegacyCrtRand());
    const std::size_t ownerIndex =
        static_cast<std::size_t>(runtime::Win64UnsignedRemainder(secondRandom, m_OwnerRestartPointList.size()));
    position = m_OwnerRestartPointList[ownerIndex];
    started = true;
}

void CCastle::PrepareSiege() {
    // Current L2Server 0x0059b854.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        SetCastleSiegeRole(false);
        SetFreezeField(true);
        SetBattleField(true);
        db.RequestLoadBattleCamp(this, 0);
        db.RequestLoadPledgeSiegeKillDeathCount(this);
    }
}

void CCastle::UpdateDomainFortressContractStatus() {
    // Current L2Server 0x00591360.
    L2_GUARD_FUNCTION();
    const int fortressCount = GetNumChildDomainFortress();
    if (fortressCount > m_nTotalNumDomainFortress || fortressCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid num child domain fortress, "
        //     L"castle[%d] num[%d] total num[%d]",
        //     L"Castle.cpp",
        //     0xe00,
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     fortressCount,
        //     m_nTotalNumDomainFortress)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid num child domain fortress, "
        //     L"castle[%d] num[%d] total num[%d]",
        //     L"Castle.cpp",
        //     0xe00,
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     fortressCount,
        //     m_nTotalNumDomainFortress)
        L2_LOG_ERROR(
            Log,
            "Castle %d: child domain-fortress count %d is outside 0..%d; contract status is still calculated from this "
            "count.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, fortressCount, m_nTotalNumDomainFortress
        );
    }

    if (fortressCount == m_nTotalNumDomainFortress) {
        m_data->m_nDomainFortressContractStatus = 2;
    } else if (fortressCount < 1) {
        m_data->m_nDomainFortressContractStatus = 0;
    } else {
        m_data->m_nDomainFortressContractStatus = 1;
    }
}

void CCastle::AddCastleFortressStatusLog(const int fortressId) {
    // Current L2Server 0x00591480.
    L2_GUARD_FUNCTION();
    const char16_t* pledgeName = u"";
    int pledgeId = 0;
    User* master = nullptr;
    if (CPledge* const pledge = GetResidenceOwnerPledge()) {
        pledgeName = pledge->GetPledgeName();
        pledgeId = static_cast<int>(pledge->GetDbId());
        master = pledge->GetMaster();
    }

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x7d2, master, nullptr, pledgeName, nullptr, nullptr, pledgeId, m_data != nullptr ? m_data->m_nResidenceID : 0,
        fortressId, static_cast<std::int64_t>(m_sDomainFortress.size()),
        static_cast<std::int64_t>(m_sBoundaryFortress.size()), 0, 0, 0, 0, 0, 0
    );
}

void CCastle::RemoveChildFortress(CFortress* const fortress) {
    // Current L2Server 0x00592f3c.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr) {
        return;
    }

    {
        L2_WRITE_GUARD(*this);
        bool removed = false;
        if (fortress->m_eLocation == FLT_TERRITORY_DOMAIN) {
            const auto found = m_sDomainFortress.find(fortress);
            if (found != m_sDomainFortress.end()) {
                m_sDomainFortress.erase(found);
                UpdateDomainFortressContractStatus();
                removed = true;
            }
        } else {
            const auto found = m_sBoundaryFortress.find(fortress);
            if (found != m_sBoundaryFortress.end()) {
                m_sBoundaryFortress.erase(found);
                removed = true;
            }
        }
        if (removed) {
            AddCastleFortressStatusLog(fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0);
        }
    }
}

void CCastle::SetChildFortress(CFortress* const fortress) {
    // Current L2Server 0x00594b2c.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr || !IsRelatedFortress(fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0)) {
        return;
    }

    {
        L2_WRITE_GUARD(*this);
        bool inserted = false;
        if (fortress->m_eLocation == FLT_TERRITORY_DOMAIN) {
            inserted = m_sDomainFortress.insert(fortress).second;
            if (inserted) {
                UpdateDomainFortressContractStatus();
            }
        } else {
            inserted = m_sBoundaryFortress.insert(fortress).second;
        }
        if (inserted) {
            AddCastleFortressStatusLog(fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0);
        }
    }
}

bool CCastle::OnChangeOwner() {
    // Current L2Server 0x00597f6c.
    L2_GUARD_FUNCTION();
    const CastleType::Enum newResidenceType = m_eNewResidenceType;
    CPledge* const newOwner = m_pNewOwnerPledge;
    if (newOwner == nullptr) {
        return false;
    }

    std::optional<std::wstring> siegeResidencesStorage;
    std::optional<CSPointer<CBattleCamp>> newOwnerBattleCampStorage;
    std::optional<CResetAllResidenceDeco> resetDecoStorage;
    std::optional<CResetAllResidenceDecoNpc> resetNpcDecoStorage;
    std::optional<std::vector<PledgeInfo>> siegePledgesStorage;
    std::optional<std::vector<User*>> siegeUsersStorage;
    int residenceId;
    int newOwnerPledgeId;
    int newOwnerWorldId;
    {
        L2_WRITE_GUARD(*this);
        if (!IsOffendingPledge(static_cast<int>(newOwner->GetDbId()), newOwner->GetWorldId())) {
            m_pNewOwnerPledge = nullptr;
            m_eNewResidenceType = CastleType::CASTLE_NOTHING;

            auto& siegeResidences = siegeResidencesStorage.emplace();
            for (int index = 1; index < 10; ++index) {
                CCastle* const castle = newOwner->GetCastleUnderSiege(index);
                const int residenceId = castle != nullptr && castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
                if (residenceId != 0) {
                    siegeResidences += std::to_wstring(residenceId);
                    siegeResidences += L";";
                }
            }
            // Log.Add(
            //     LOG_ERROR,
            //     "Changing Owner to non-offending pledge! "
            //     "[%d][%s] pledge[%d]",
            //     m_data != nullptr ? m_data->m_nResidenceID : 0,
            //     siegeResidences.c_str(),
            //     static_cast<int>(newOwner->GetDbId()))
            // L2_LOG_ERROR(Log,
            //     "Changing Owner to non-offending pledge! "
            //     "[%d][%s] pledge[%d]",
            //     m_data != nullptr ? m_data->m_nResidenceID : 0,
            //     siegeResidences.c_str(),
            //     static_cast<int>(newOwner->GetDbId()))
            L2_LOG_WARN(
                Log,
                "Rejected ownership change for castle %d: clan %u is not an attacker (clan siege castles: %s); the pending "
                "change is cleared.",
                m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned>(newOwner->GetDbId()),
                WideToUtf8(siegeResidences).c_str()
            );
            return false;
        }

        const std::int32_t now = runtime::crt::Time32Now();
        if (runtime::SubtractInt32Wrapping(now, m_tPossessed) < 300) {
            m_pNewOwnerPledge = nullptr;
            m_eNewResidenceType = CastleType::CASTLE_NOTHING;
            // Log.Add(
            //     LOG_ERROR,
            //     "Holything was possessed just now")
            // L2_LOG_ERROR(Log,
            //     "Holything was possessed just now")
            L2_LOG_WARN(
                Log,
                "Castle %d: ownership change rejected by the 300-second capture cooldown (now %d, previous capture %d; Unix "
                "seconds); the pending change is cleared.",
                m_data != nullptr ? m_data->m_nResidenceID : 0, now, m_tPossessed
            );
            return false;
        }
        m_tPossessed = now;

        const int previousOwnerPledgeId = static_cast<int>(m_data->m_nResidenceOwnerID);
        if (previousOwnerPledgeId == 0) {
            ResetVault(1);
        } else {
            CSPointer<CPledge> previousOwner = CPledge::FindPledge(m_data->m_nResidenceOwnerWorldId, previousOwnerPledgeId);
            if (previousOwner) {
                User* const master = previousOwner->GetMaster();
                if (master != nullptr) {
                    if (master->IsRidingWyvern()) {
                        master->TryDismounting(true, true);
                    }
                    if (master->m_inv.HaveItemByClassId(0x1ab9, 1, false, false)) {
                        static_cast<void>(master->DeleteItemInInventory(0x1ab9, 1, nullptr, false, false));
                    }
                }
                previousOwner->SetCastle(nullptr);
                previousOwner->UnequipMemberCastleItem();
                previousOwner->Save(6);
                previousOwner->UpdateInfoWindowPacket();
                CSPointer<CBattleCamp> battleCamp = previousOwner->GetBattleCamp();
                if (battleCamp && battleCamp->m_data->m_dHP > 0.0) {
                    battleCamp->m_data->m_bIsActive = true;
                    battleCamp->Appear();
                }
            }
        }

        if (newOwner->m_pFortress != nullptr) {
            newOwner->m_pFortress->ResetOwnership();
        }
        m_data->m_ResidenceType = static_cast<int>(newResidenceType);
        SetPledge(newOwner);
        newOwner->SetCastle(this);
        if (m_pDominion != nullptr) {
            m_pDominion->ChangeDominionStatus(1);
        }
        newOwner->Save(6);
        newOwner->UpdateInfoWindowPacket();
        auto& newOwnerBattleCamp = newOwnerBattleCampStorage.emplace(newOwner->GetBattleCamp());
        if (newOwnerBattleCamp) {
            newOwnerBattleCamp->Disappear();
        }

        int messageId = 0;
        if (m_data->m_ResidenceType == CastleType::CASTLE_GOOD) {
            messageId = 0x4a5a;
        } else if (m_data->m_ResidenceType == CastleType::CASTLE_BAD) {
            messageId = 0x4a5b;
        }

        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            if (user->IsInRealWorld() && IsInBattleField(user->GetPos(), true) && messageId != 0) {
                user->GetSocket()->Send(
                    "chccScdh", 0x62, SystemMessageId::CLAN_SUCCEEDED_IN_NAMED_ACTION, 2, 0, newOwner->GetPledgeName(), 4,
                    messageId, 1
                );
            }
            user->GetSocket()->Send(
                "chdd", 0xfe, 0x12d, m_data != nullptr ? m_data->m_nResidenceID : 0, m_data->m_ResidenceType
            );
        });

        db.ReqOwnCastle(
            this, static_cast<int>(m_data->m_nResidenceOwnerID), m_PreviousOwnerPledgeDbId, m_data->m_ResidenceType,
            m_data->m_nResidenceOwnerWorldId, m_nPrevOwnerPledgeWorldId
        );
        db.RequestSetCastleCampaign(this, -1);
        ChangeResidenceState(RS_MID_VICTORY);
        if (!m_bMidVictory) {
            BroadcastToAttacker(0x4a6);
        }
        m_bMidVictory = true;
        residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        g_aHadMidVictory[static_cast<std::size_t>(residenceId)] = 1;

        auto& resetDeco = resetDecoStorage.emplace();
        resetDeco.nNpcIndex = -1;
        resetDeco.nUserIndex = -1;
        resetDeco.nAgitId = residenceId;
        db.RequestTPacketProcess(g_nWorldId, 3, static_cast<int>(sizeof(resetDeco)), resetDeco);
        auto& resetNpcDeco = resetNpcDecoStorage.emplace(previousOwnerPledgeId, residenceId);
        db.RequestTPacketProcess(g_nWorldId, 0x3d, static_cast<int>(sizeof(resetNpcDeco)), resetNpcDeco);
        db.RequestDeleteMercenary(*this);

        auto& siegePledges = siegePledgesStorage.emplace();
        for (int index = 0; index < m_data->m_DefendingPledgeListSize; ++index) {
            siegePledges.push_back(m_data->m_stDefendingPledgeList[static_cast<std::size_t>(index)]);
        }
        m_data->m_DefendingPledgeListSize = 0;
        m_data->m_stDefendingPledgeList[static_cast<std::size_t>(m_data->m_DefendingPledgeListSize)] =
            PledgeInfo{static_cast<int>(newOwner->GetDbId()), newOwner->GetWorldId()};
        ++m_data->m_DefendingPledgeListSize;

        newOwnerPledgeId = static_cast<int>(newOwner->GetDbId());
        newOwnerWorldId = newOwner->GetWorldId();
        for (int index = 0; index < m_data->m_OffendingPledgeListSize; ++index) {
            const PledgeInfo& pledge = m_data->m_stOffendingPledgeList[static_cast<std::size_t>(index)];
            if (pledge.m_nPledgeID != newOwnerPledgeId || pledge.m_nWorldID != newOwnerWorldId) {
                siegePledges.push_back(pledge);
            }
        }
        m_data->m_OffendingPledgeListSize = 0;
        for (const PledgeInfo& pledge : siegePledges) {
            m_data->m_stOffendingPledgeList[static_cast<std::size_t>(m_data->m_OffendingPledgeListSize)] = pledge;
            ++m_data->m_OffendingPledgeListSize;
        }

        db.RequestResetCastleSiegePledge(this);
        newOwner->ResetCastleSiege();
        auto& siegeUsers = siegeUsersStorage.emplace();
        SetDefendingCastle(true, siegeUsers);
        SetOffendingCastle(true, siegeUsers);
        for (User* const user : siegeUsers) {
            user->SetRelationChanged(false);
        }

        BanNonDefendingPlayers(BAN_TYPE_1);
        for (CDoor* const door : m_DoorList) {
            door->ResetDoorState(true, 0.5);
        }
        MakeDoorsBreakable(true);
        for (CControlTower* const tower : m_ControlTowerList) {
            tower->OnMidVictory();
        }
        ValidateLifeControlLevel(false);
        g_npcserver.Send("cddd", 0x26, residenceId, 1, 0);
        FreeSiegeGolem();
    }

    m_nValidate = 2;
    m_nValidateWaiting = 3;
    // Log.Add(
    //     LOG_ERROR,
    //     L"no error mid victory : %s, %d",
    //     newOwner->GetPledgeName(),
    //     static_cast<int>(newResidenceType))
    // L2_LOG_ERROR(Log,
    //     L"no error mid victory : %s, %d",
    //     newOwner->GetPledgeName(),
    //     static_cast<int>(newResidenceType))
    L2_LOG_INFO(
        Log,
        "Castle %d: clan '%s' (ID %d, world %d) captured the castle; mid-victory state applied with residence type %d and "
        "persistence requested.",
        residenceId, Utf16ToUtf8(newOwner->GetPledgeName()).c_str(), newOwnerPledgeId, newOwnerWorldId,
        static_cast<int>(newResidenceType)
    );
    m_eNewResidenceType = CastleType::CASTLE_NOTHING;
    m_pNewOwnerPledge = nullptr;
    InitKillScore();
    return true;
}

int CCastle::GetSelectableSiegeTimes(
    const std::int32_t baseTime, int* const siegeTimes, const int count, const int residenceId
) {
    // Current L2Server 0x00598c28. The inclusive concurrency comparison
    // and the two seemingly unrelated day constants are target behavior.
    L2_GUARD_FUNCTION();
    const std::int32_t adjusted = baseTime - BP::_CASTLE_ANNOUNCE_TIME;
    const std::tm* const local = runtime::crt::LocalTime32(adjusted);
    if (local == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "localtime_l2 is null in CCastle::GetSelectableSiegeTimes")
        // L2_LOG_ERROR(Log,
        //     "localtime_l2 is null in CCastle::GetSelectableSiegeTimes")
        L2_LOG_ERROR(
            Log,
            "Cannot calculate selectable siege times for castle %d: local-time conversion failed for Unix seconds %d (base "
            "time %d); returning no choices.",
            residenceId, adjusted, baseTime
        );
        return 0;
    }

    const int weekday = local->tm_wday < 1 ? 6 : local->tm_wday - 1;
    const std::int32_t weekStart = adjusted - ((local->tm_hour + weekday * 24) * 60 + local->tm_min) * 60 - local->tm_sec;

    std::vector<std::int32_t> candidates;
    for (std::size_t index = 0; index < s_lstWday.size(); ++index) {
        std::int32_t candidate = weekStart + (runtime::CheckedVectorAt(s_lstWday, index) == 6 ? 0x190c80 : 0x1a5e00);
        candidate += runtime::CheckedVectorAt(s_lstHour, index) * 0xe10;
        candidates.push_back(candidate);
    }

    int selected;
    {
        L2_WRITE_GUARD(s_lock);
        for (auto position = s_mmapReservedSiegeTime.begin(); position != s_mmapReservedSiegeTime.end(); ++position) {
            if (position->second == residenceId) {
                s_mmapReservedSiegeTime.erase(position);
                break;
            }
        }

        selected = 0;
        for (const std::int32_t candidate : candidates) {
            if (selected >= count) {
                break;
            }
            const auto range = s_mmapReservedSiegeTime.equal_range(candidate);
            int concurrent = 0;
            for (auto position = range.first; position != range.second; ++position) {
                ++concurrent;
            }
            if (concurrent <= s_nMaxConcurrentSiege) {
                siegeTimes[selected++] = candidate;
            }
        }
        if (residenceId != 0 && selected > 0) {
            s_mmapReservedSiegeTime.emplace(siegeTimes[0], residenceId);
        }
    }
    return selected;
}

void CCastle::AdjustSiegeTime() {
    // Current L2Server 0x0059a4f0.
    L2_GUARD_FUNCTION();
    s_lstCastle.clear();
    for (const auto& entry : g_ResidenceDB.m_ResidenceMap) {
        CResidence* const residence = entry.second;
        if (residence->IsCastle()) {
            s_lstCastle.push_back(residence->CastCastle());
        }
    }

    {
        L2_WRITE_GUARD(s_lock);
        s_mmapReservedSiegeTime.clear();
        for (CCastle* const castle : s_lstCastle) {
            const int nextSiege = castle->m_data != nullptr ? castle->m_data->m_NextSiege : 0;
            const int castleId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
            s_mmapReservedSiegeTime.emplace(nextSiege, castleId);
        }
    }
}

void CCastle::ValidateChangeOwner() {
    // Current L2Server 0x0059a6e8.
    L2_GUARD_FUNCTION();
    BanNonDefendingPlayers(BAN_TYPE_1);
}

void CCastle::SetCastleType(const CastleType::Enum castleType) {
    // Current L2Server 0x005925f0.
    L2_GUARD_FUNCTION();
    if (m_data->m_ResidenceState != RS_TYPE_CHANGE) {
        return;
    }

    const int previousType = m_data->m_ResidenceType;
    m_data->m_ResidenceType = static_cast<int>(castleType);
    db.RequestSetCastleType(*this, m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<int>(castleType));
    BroadcastCastleState();

    CPledge* const pledge = GetResidenceOwnerPledge();
    const int pledgeId = pledge != nullptr ? static_cast<int>(pledge->GetDbId()) : 0;
    const std::u16string pledgeName = std::u16string(pledge != nullptr ? pledge->GetPledgeName() : u"");
    CLogFormatter formatter;
    formatter.WriteLog(
        0x7f7, pledgeId, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), nullptr, nullptr,
        m_data != nullptr ? m_data->m_nResidenceID : 0, previousType, static_cast<int>(castleType), 0, 0, 0, 0, 0, 0, 0,
        nullptr, nullptr, nullptr, nullptr, 0
    );

    if (pledge != nullptr) {
        RemoveResidenceSkill(*pledge);
        AddResidenceSkill(*pledge, castleType);
    }
}

void CCastle::ResetCastleOwner(const int userSid) {
    // Current L2Server 0x005928b8.
    L2_GUARD_FUNCTION();
    const int ownerWorldId = m_data != nullptr ? m_data->m_nResidenceOwnerWorldId : 0;
    const int ownerPledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
    if (ownerPledgeId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CCastle::ResetCastleOwner() :Cannot Reset Castle[%d] "
        //     L"Invalid Owner",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // Noise: Resetting a castle with no player-clan owner is an idempotent no-op. No ownership change occurs.
        // L2_LOG_ERROR(Log,
        //     L"CCastle::ResetCastleOwner() :Cannot Reset Castle[%d] "
        //     L"Invalid Owner",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        return;
    }

    CSPointer<CPledge> owner = CPledge::FindPledge(ownerWorldId, ownerPledgeId);
    if (owner) {
        if (owner->GetFortress() != nullptr) {
            owner->GetFortress()->ResetOwnership();
        }
        owner->SetCastle(nullptr);
        owner->Save(6);
        owner->UpdatePledgeInfoPacket();
        owner->UnequipMemberCastleItem();
    }

    SetPledge(nullptr);
    if (m_pDominion != nullptr) {
        m_pDominion->m_data->m_nResidenceOwnerID = 0;
        CSPointer<CPledge> emptyPledge;
        m_pDominion->DisableDominionExtends(emptyPledge);
        m_pDominion->ChangeDominionStatus(0);
    }
    SetAllNPCPledge(0);
    db.ReqOwnCastle(this, 0, m_PreviousOwnerPledgeDbId, m_data->m_ResidenceType, 0, m_nPrevOwnerPledgeWorldId);
    db.RequestSetCastleCampaign(this, -1);
    BroadcastCastleState();

    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user != nullptr) {
        CPledge* const actorPledge = user->GetPledge();
        CLogFormatter formatter;
        formatter.WriteLog4(
            0x7f8, user, nullptr, actorPledge != nullptr ? actorPledge->GetPledgeName() : nullptr, nullptr, nullptr,
            m_data != nullptr ? m_data->m_nResidenceID : 0, m_PreviousOwnerPledgeDbId, user->m_data->m_nPledgeID, 0, 0, 0, 0,
            0, 0, 0, 0
        );
    }
}
