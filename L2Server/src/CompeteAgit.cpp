#include "CompeteAgit.h"
#include "AccountJobFinishSignal.h"
#include "CConfig.h"
#include "DB.h"
#include "InstantAgit.h"
#include "NpcServer.h"
#include "Packet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <cstdint>
#include <ctime>
#include <cwchar>
#include <string>

namespace {


const char16_t* ResidenceName(const CResidenceData* const data) noexcept {
    return data != nullptr ? data->m_sResidenceName.data() : u"";
}

std::uint32_t ResidenceId(const CResidenceData* const data) noexcept {
    return data != nullptr ? static_cast<std::uint32_t>(data->m_nResidenceID) : 0U;
}

} // namespace

// Current L2Server .data 0x015cd008 is initialized to 0x258. The current
// builder command is the only writer, and TotalChecker's MAP places the
// original physical definition in CompeteAgit.obj.
int BP::_COMPETE_AGIT_PREPARE_TIME = 600;

CCompeteAgitStatus::state CCompeteAgitStatus::CalcState() const {
    // Current L2Server 0x005c51d0.
    L2_GUARD_FUNCTION();
    if (m_msecElapsed == 0) {
        return NOTHING;
    }

    const std::uint32_t prepareMilliseconds = static_cast<std::uint32_t>(BP::_COMPETE_AGIT_PREPARE_TIME) * 1000U;
    if (m_msecElapsed < prepareMilliseconds) {
        return PREPARE;
    }

    const std::uint32_t processEnd = prepareMilliseconds + static_cast<std::uint32_t>(m_SecProgress);
    if (m_msecElapsed < processEnd) {
        return m_State == BOSSKILL ? BOSSKILL : PROCESS;
    }
    return CLEAN_UP;
}

bool CCompeteAgitStatus::UpdateState() {
    // Current L2Server 0x005c5294.
    L2_GUARD_FUNCTION();
    if (!m_bPause) {
        if (m_msecLastUpdate == 0) {
            m_msecLastUpdate = runtime::TickCount32();
        } else {
            const std::uint32_t now = runtime::TickCount32();
            m_msecElapsed += now - m_msecLastUpdate;
            m_msecLastUpdate = now;
        }
    }

    const state next = CalcState();
    if (next == m_State) {
        return false;
    }
    m_State = next;
    return true;
}

void CCompeteAgitStatus::SetBosKill() {
    // Current L2Server 0x005c5358.
    L2_GUARD_FUNCTION();
    if (m_State == PROCESS) {
        m_State = BOSSKILL;
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CCompeteAgit)

L2_DEFINE_OBJECT_CLASS_NAME(CCompeteAgit, m_pClassName_CCompeteAgit)

L2_DEFINE_OBJECT_ALLOCATOR(CCompeteAgit, __GetClassName())

L2_DEFINE_OBJECT_NEW(CCompeteAgit)

CCompeteAgit::CCompeteAgit() {
    // Current L2Server 0x005c53f0 writes the status carrier literally.
    m_StateHelper.m_State = CCompeteAgitStatus::NOTHING;
    m_StateHelper.m_SecProgress = 0;
    m_StateHelper.m_msecElapsed = 0;
    m_StateHelper.m_msecLastUpdate = 0;
    m_StateHelper.m_bPause = false;
    m_nPreviousOwnerDBID = 0;
    m_nWinnerPledgeDbId = 0;
    m_bAcceptOwner = false;
    L2_GUARD_FUNCTION();
}

CCompeteAgit::~CCompeteAgit() {
    // Current L2Server 0x005c54b4.
    L2_GUARD_FUNCTION();
}

// Current L2Server 0x005c6188.
L2_DEFINE_OBJECT_TYPED_RELEASE(CCompeteAgit)

bool CCompeteAgit::IsCompetitionAgit() const { return true; }

bool CCompeteAgit::IsBattleTypeAgit() const { return true; }

CCompeteAgit* CCompeteAgit::CastCompeteAgit() const { return const_cast<CCompeteAgit*>(this); }

void CCompeteAgit::LoadFromDB(const int elapsedMilliseconds) {
    // Current L2Server 0x005c5550.
    L2_GUARD_FUNCTION();
    m_StateHelper.m_msecElapsed = static_cast<std::uint32_t>(elapsedMilliseconds);
}

bool CCompeteAgit::OnSetWinner(CPledge* const pledge, const bool accepted) {
    // Current L2Server 0x005c55f0.
    L2_GUARD_FUNCTION();
    if (m_StateHelper.m_State != CCompeteAgitStatus::PROCESS) {
        return false;
    }

    bool suppressFailureLog = false;
    if (pledge == nullptr) {
        suppressFailureLog = m_nWinnerPledgeDbId == 0;
    } else if (m_nWinnerPledgeDbId == 0) {
        m_nWinnerPledgeDbId = pledge->GetDbId();
        m_bAcceptOwner = accepted;
        m_StateHelper.SetBosKill();
        pledge->SetCompeteAgit(this);
        db.RequestCompeteAgitWinner(*this, pledge->GetDbId(), m_bAcceptOwner);

        const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x80d, pledge->GetDbId(), 0, 0, 0, 0, 0, 0, pledgeName.c_str(), nullptr, nullptr, residenceId,
            m_bAcceptOwner ? 1 : 0, 0, 0, 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
        );
        return true;
    }

    if (!suppressFailureLog) {
        // The current call supplies no values for the two
        // formatter placeholders.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CompeteAgit set owner fail")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CompeteAgit set owner fail")
        L2_LOG_WARN(
            Log, "Competition clan hall %u: winner selection refused because clan %u is already recorded as winner.",
            ResidenceId(m_data), m_nWinnerPledgeDbId
        );
    }
    return false;
}

std::int32_t CCompeteAgit::GetNextSiegeTime() {
    // Current L2Server 0x005c5834.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);

    int dayDelta = 0;
    const int targetDay = m_data->m_nCompeteAgitSiegeDay;
    if (local->tm_wday < targetDay || (local->tm_wday == targetDay && local->tm_hour < m_data->m_nCompeteAgitSiegehour) ||
        (local->tm_hour == m_data->m_nCompeteAgitSiegehour && local->tm_min < m_data->m_nCompeteAgitSiegeMin)) {
        dayDelta = targetDay - local->tm_wday;
    } else {
        dayDelta = targetDay - local->tm_wday + 7;
    }

    std::uint32_t result = static_cast<std::uint32_t>(m_data->m_nCompeteAgitSiegehour);
    result = result * 60U + static_cast<std::uint32_t>(m_data->m_nCompeteAgitSiegeMin);
    result = result * 60U;
    result += static_cast<std::uint32_t>(dayDelta) * 86400U;
    result += static_cast<std::uint32_t>(now);
    result -= (static_cast<std::uint32_t>(local->tm_hour) * 60U + static_cast<std::uint32_t>(local->tm_min)) * 60U +
        static_cast<std::uint32_t>(local->tm_sec);
    return runtime::Int32FromBits(result);
}

void CCompeteAgit::StartAgitSiege(const AgitSiegeStatus::status status) {
    // Current L2Server 0x005c5948.
    L2_GUARD_FUNCTION();
    g_npcserver.Send("cdSddd", 0x31, ResidenceId(m_data), ResidenceName(m_data), 0, 1, static_cast<int>(status));
    MakeNPCDisable(true);
}

void CCompeteAgit::ClearGame() {
    // Current L2Server 0x005c5a40.
    L2_GUARD_FUNCTION();
    m_StateHelper.m_State = CCompeteAgitStatus::NOTHING;
    m_StateHelper.m_msecElapsed = 0;
    m_StateHelper.m_msecLastUpdate = 0;
    m_StateHelper.m_bPause = false;
    ChangeResidenceState(RS_STANDBY);
    if (m_data != nullptr) {
        m_data->m_NextSiege = runtime::Int32FromBits(static_cast<std::uint32_t>(m_data->m_NextSiege) + 0x127500U);
    }
    db.RequestSetNextAgitBattleTime(this);
}

void CCompeteAgit::StartTimer() {
    // Current L2Server 0x005c5b28.
    L2_GUARD_FUNCTION();
    const int nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
    if (nextSiege == 0) {
        const std::int32_t next = GetNextSiegeTime();
        if (m_data != nullptr) {
            m_data->m_NextSiege = next;
        }
        db.RequestSetNextAgitBattleTime(this);
    }

    if (!IsUnderSiege()) {
        const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : static_cast<ResidenceState>(8);
        if (state != RS_STANDBY) {
            ChangeResidenceState(RS_STANDBY);
        }
    } else {
        m_bSiegeContinued = true;
        if (m_data != nullptr) {
            m_data->m_ResidenceState = RS_STANDBY;
        }
    }

    m_StateHelper.m_SecProgress =
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_data->m_nCompeteAgitSiegeProgress) * 60000U);
    CWorldAgit::StartTimer();
}

CCompeteAgit* CCompeteAgit::GetCompeteAgit(const int residenceId) {
    // Current L2Server 0x005c5c5c.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->IsWorldAgit()) {
        return nullptr;
    }
    CWorldAgit* const agit = residence->CastWorldAgit();
    if (agit == nullptr || !agit->IsCompetitionAgit()) {
        return nullptr;
    }
    return agit->CastCompeteAgit();
}

void CCompeteAgit::ShowInfoByBuilderCmd(CUserSocket* const socket) {
    // Current L2Server 0x005c5d40.
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }

    char16_t status[56];
    switch (m_StateHelper.m_State) {
        case CCompeteAgitStatus::NOTHING:
            FormatUtf16To(status, 56, u"Status:[%ls]", u"NOTHING");
            break;
        case CCompeteAgitStatus::PREPARE:
            FormatUtf16To(status, 56, u"Status:[%ls]", u"PREPARE");
            break;
        case CCompeteAgitStatus::PROCESS:
            FormatUtf16To(status, 56, u"Status:[%ls]", u"PROCESS");
            break;
        case CCompeteAgitStatus::BOSSKILL:
            FormatUtf16To(status, 56, u"Status:[%ls, %d]", u"BOSSKILL", m_nWinnerPledgeDbId);
            break;
        case CCompeteAgitStatus::CLEAN_UP:
            FormatUtf16To(status, 56, u"Status:[%ls]", u"CLEAN_UP");
            break;
        default:
            break;
    }

    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
    const std::tm* const local = runtime::crt::LocalTime32(nextSiege);
    char16_t message[512];
    FormatUtf16To(
        message, 512, u"[%d] %ls CompeteStartTime:%d-%02d-%02d %02d:%02d:%02d", ResidenceId(m_data), status,
        local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec
    );
    socket->SendSystemMessage(u"SYS", message);
}

bool CCompeteAgitDBHandler::ReplyResetCompeteAgit(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x005c5f08.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);
    int errorCode = 0;
    int residenceId = 0;
    ::Disassemble(packet, "dd", &errorCode, &residenceId);

    CCompeteAgit* const agit = CCompeteAgit::GetCompeteAgit(residenceId);
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CCompeteAgit(%d) is null.",
        //     L"CompeteAgit.cpp",
        //     0x269,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CCompeteAgit(%d) is null.",
        //     L"CompeteAgit.cpp",
        //     0x269,
        //     residenceId)
        L2_LOG_ERROR(
            Log,
            "Competition clan hall %d: cannot apply the database reset reply (result %d) because the hall was not found.",
            residenceId, errorCode
        );
    } else {
        if (errorCode != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CCompeteAgit(%d) DB process fail("
            //     L"RP_RESET_COMPETE_AGIT_EX) error_code(%d).",
            //     L"CompeteAgit.cpp",
            //     0x270,
            //     residenceId,
            //     errorCode)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CCompeteAgit(%d) DB process fail("
            //     L"RP_RESET_COMPETE_AGIT_EX) error_code(%d).",
            //     L"CompeteAgit.cpp",
            //     0x270,
            //     residenceId,
            //     errorCode)
            L2_LOG_ERROR(
                Log, "Competition clan hall %d: database reset failed with result %d; local game cleanup still proceeds.",
                residenceId, errorCode
            );
        }
        agit->ClearGame();
    }
    return false;
}

bool CCompeteAgitDBHandler::ReplyLoadCompeteAgit(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x005c6044.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);
    int residenceId = 0;
    int elapsed = 0;
    int winnerPledgeId = 0;
    int previousOwnerPledgeId = 0;
    int accepted = 0;
    ::Disassemble(packet, "ddddd", &residenceId, &elapsed, &winnerPledgeId, &previousOwnerPledgeId, &accepted);

    CCompeteAgit* const agit = CCompeteAgit::GetCompeteAgit(residenceId);
    if (agit != nullptr) {
        agit->LoadFromDB(elapsed);
        if (winnerPledgeId != 0) {
            agit->m_nWinnerPledgeDbId = static_cast<std::uint32_t>(winnerPledgeId);
            agit->m_bAcceptOwner = accepted != 0;
        }
        if (previousOwnerPledgeId != 0) {
            agit->m_nPreviousOwnerDBID = static_cast<std::uint32_t>(previousOwnerPledgeId);
        }
        agit->StartTimer();
    }
    if (g_JobFinishSignal.SetAgitLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_AGIT);
    }
    return false;
}

CompeteAgitResult::Reason CCompeteAgit::CanBeOwner(CPledge* const pledge) {
    // Current L2Server 0x005c62f8.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return CompeteAgitResult::NO_PLEDGE;
    }
    if (pledge->GetWorldAgit() == nullptr && pledge->GetCompeteAgit() == nullptr &&
        CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(pledge->GetDbId())) == nullptr) {
        return CompeteAgitResult::SUCCESS;
    }
    return CompeteAgitResult::DUPLICATED_AGIT;
}

void CCompeteAgit::PrepareSiege() {
    // Current L2Server 0x005c63e4.
    L2_GUARD_FUNCTION();
    m_nPreviousOwnerDBID = m_data != nullptr ? m_data->m_nResidenceOwnerID : 0;
    db.RequestSetPreviouseAgitOwnerID(*this, m_nPreviousOwnerDBID);
    if (m_nPreviousOwnerDBID != 0) {
        CPledge* const previous = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_nPreviousOwnerDBID)).get();
        if (previous != nullptr) {
            previous->SystemMessage(SystemMessageId::CLAN_HALL_OWNERSHIP_RETURNED_WITH_BATTLE_ENTRY_TIME);
        }
    }
    ChangeOwner(nullptr, 0, true);
}

void CCompeteAgit::EndSiege() {
    // Current L2Server 0x005c6518.
    L2_GUARD_FUNCTION();
    CPledge* owner = nullptr;
    bool updateNameValue = true;
    if (!m_bAcceptOwner) {
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
    } else {
        owner = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_nWinnerPledgeDbId)).get();
        if (owner == nullptr) {
            updateNameValue = false;
        } else {
            ChangeOwner(owner, 0, true);
            BroadcastSystemMessageW(owner->GetPledgeName(), SystemMessageId::CLAN_WON_NAMED_CLAN_HALL_SIEGE);
        }
    }
    if (updateNameValue) {
        UpdatePledgeNameValue(0, owner);
    }

    if (m_nWinnerPledgeDbId != 0) {
        CPledge* const winner = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_nWinnerPledgeDbId)).get();
        if (winner == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CompeteAgit m_nWinnerPledgeDbId[%d] "
            //     L"FindPledge failed",
            //     L"CompeteAgit.cpp",
            //     0x186,
            //     m_nWinnerPledgeDbId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CompeteAgit m_nWinnerPledgeDbId[%d] "
            //     L"FindPledge failed",
            //     L"CompeteAgit.cpp",
            //     0x186,
            //     m_nWinnerPledgeDbId)
            L2_LOG_ERROR(
                Log,
                "Competition clan hall %u: winner clan %u was not found at siege end; its competition link cannot be "
                "cleared, but hall cleanup continues.",
                ResidenceId(m_data), m_nWinnerPledgeDbId
            );
        } else {
            winner->SetCompeteAgit(nullptr);
        }
    }

    m_nPreviousOwnerDBID = 0;
    m_nWinnerPledgeDbId = 0;
    db.RequestResetCompeteAgitStatus(*this);
    g_npcserver.Send("cdSddd", 0x31, ResidenceId(m_data), ResidenceName(m_data), 0, 0, 0);
    MakeNPCDisable(false);
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED);
}

void CCompeteAgit::HandleState() {
    // Current L2Server 0x005c6764.
    L2_GUARD_FUNCTION();
    if (m_StateHelper.m_bPause) {
        m_StateHelper.m_bPause = false;
        m_StateHelper.m_msecLastUpdate = runtime::TickCount32();
        static_cast<void>(m_StateHelper.UpdateState());
        if (m_StateHelper.m_State == CCompeteAgitStatus::PROCESS) {
            if (!m_bSiegeContinued) {
                g_npcserver.Send(
                    "cdSddd", 0x31, ResidenceId(m_data), ResidenceName(m_data), 0, 1,
                    static_cast<int>(AgitSiegeStatus::RESTART)
                );
                MakeNPCDisable(true);
            } else {
                if (m_nWinnerPledgeDbId != 0) {
                    m_StateHelper.SetBosKill();
                }
                StartAgitSiege(AgitSiegeStatus::RESTART);
                ChangeResidenceState(RS_SIEGE);
                m_bSiegeContinued = false;
            }
        }
    }

    if (m_StateHelper.m_State == CCompeteAgitStatus::NOTHING) {
        m_StateHelper.m_msecElapsed = 1;
        m_StateHelper.m_msecLastUpdate = runtime::TickCount32();
        if (m_nWinnerPledgeDbId != 0) {
            CPledge* const winner = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_nWinnerPledgeDbId)).get();
            if (winner != nullptr) {
                winner->SetCompeteAgit(nullptr);
            }
        }
        m_nWinnerPledgeDbId = 0;
    }

    if (m_StateHelper.UpdateState()) {
        switch (m_StateHelper.m_State) {
            case CCompeteAgitStatus::PREPARE:
                PrepareSiege();
                break;
            case CCompeteAgitStatus::PROCESS:
                StartAgitSiege(AgitSiegeStatus::START);
                ChangeResidenceState(RS_SIEGE);
                break;
            case CCompeteAgitStatus::CLEAN_UP:
                EndSiege();
                ChangeResidenceState(RS_STANDBY);
                break;
            default:
                break;
        }
    }
    db.RequestSetCompeteAgitStatus(*this, static_cast<int>(m_StateHelper.m_msecElapsed));
}

void CCompeteAgit::TimerExpired(const int id) {
    // Current L2Server 0x005c69c0.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : static_cast<ResidenceState>(8);

    bool handle = state == RS_SIEGE;
    if (state == RS_STANDBY || state == RS_REGISTRATION) {
        const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
        const std::int32_t prepareStart = runtime::Int32FromBits(
            static_cast<std::uint32_t>(nextSiege) - static_cast<std::uint32_t>(BP::_COMPETE_AGIT_PREPARE_TIME)
        );
        handle = now >= prepareStart;
    }

    if (handle) {
        if (g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
            HandleState();
        } else {
            PauseSiege();
        }
    }
    CWorldAgit::TimerExpired(id);
}
