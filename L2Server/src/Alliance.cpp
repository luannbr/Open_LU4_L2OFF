#include "Alliance.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include "DB.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "Raid.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Config.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

constexpr JobFinishType kAllianceJobFinishType = JFT_LOAD_ALLIANCE;

void CopyAllianceName(char16_t* destination, const wchar_t* source) {
    if (!source) {
        destination[0] = u'\0';
        runtime::process::HandleInvalidParameter(L"CAlliance name must not be null");
        return;
    }

    const std::u16string converted = ::WideToUtf16(std::wstring_view(source));
    if (converted.size() >= 24) {
        destination[0] = u'\0';
        runtime::process::HandleInvalidParameter(L"CAlliance name exceeds wchar_t[24]");
        return;
    }

    std::memcpy(destination, converted.data(), converted.size() * sizeof(char16_t));
    destination[converted.size()] = u'\0';
}

CPledge* ResolvePledge(std::uint32_t smartId) noexcept { return CPledge::GetObjectW(smartId); }

void RejectAllianceCreation(CNPC* npc, User* user, std::int32_t messageId) noexcept {
    g_npcserver.Send("cdddd", 0x35, user->GetServerId(), user->m_data->index, npc->m_data->index, 0);
    user->GetSocket()->SendSystemMessage(static_cast<std::uint32_t>(messageId));
}

} // namespace

CAllianceDbIdMap g_AllianceDbIdMap;
CAllianceNameMap g_AllianceNameMap;
CAllianceSmartIdManager g_AllianceSmartIdManager;

CAllianceSmartIdManager* AllianceSmartIdManager() noexcept { return &g_AllianceSmartIdManager; }

CAlliance::CAlliance(
    std::uint32_t databaseId,
    const wchar_t* name,
    std::uint32_t masterPledgeDatabaseId,
    std::int32_t worldId,
    std::uint32_t oustTime,
    std::uint32_t crestId
) :
    m_nMasterPledgeServerId(0), m_nWorldId(worldId) {
    L2_GUARD_FUNCTION();

    m_data = CAllianceFactory::Inst().Add(this);
    const std::int32_t sharedIndex = m_data->index;

    m_nId = g_AllianceSmartIdManager.RegisterObject(
        // 15,
        SCLS_ALLIANCE, static_cast<std::uint32_t>(sharedIndex), this
    );
    CAllianceFactory::Inst().PublishIndexId(sharedIndex, m_nId);

    m_data->m_nDbId = databaseId;
    CopyAllianceName(m_data->m_sAllianceName, name);
    m_data->m_nMasterPledgeDbId = masterPledgeDatabaseId;
    m_data->m_nOustTime = oustTime;
    m_data->m_nCrestId = crestId;
    m_data->m_nOnlineMemberCount = 0;
    std::memset(m_data->m_OnlineMember, 0, sizeof(m_data->m_OnlineMember));
    std::memset(m_data->m_EnemyWarPair, 0, sizeof(m_data->m_EnemyWarPair));
}

CAlliance::~CAlliance() {
    L2_GUARD_FUNCTION();
    CAllianceFactory::Inst().Remove(this);
    m_data = nullptr;
    m_lock = nullptr;
}

int CAlliance::GetIndex() const { return m_data->index; }

CObject* CAlliance::Copy() { return nullptr; }

FVector CAlliance::GetPos() const { return {}; }

void CAlliance::InitAlliance() {
    L2_GUARD_FUNCTION();
    if (g_JobFinishSignal.m_nMustLoadAlliance < 1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Alliance]: count: 0")
        // L2_LOG_INFO(Log,
        //     L"Load Success[Alliance]: count: 0")
        L2_LOG_INFO(Log, "No alliances are required; marking the alliance startup dependency ready.");
        g_JobFinishSignal.NotifyFinish(kAllianceJobFinishType);
        CPledge::SetFlagReservePledge(PRLT_ALLIANCE);
        return;
    }

    db.RequestLoadAllAlliance();
    if (!g_JobFinishSignal.WaitFinish(kAllianceJobFinishType, 30)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Problem occurred in Loading Alliance")
        // L2_LOG_ERROR(Log,
        //     L"Problem occurred in Loading Alliance")
        L2_LOG_ERROR(
            Log,
            "Alliance loading did not signal completion within 30 seconds; alliance initialization returns without "
            "confirming completion."
        );
        return;
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Load Success[Alliance]: count: %d",
    //     g_JobFinishSignal.m_nMustLoadAlliance)
    // L2_LOG_INFO(Log,
    //     L"Load Success[Alliance]: count: %d",
    //     g_JobFinishSignal.m_nMustLoadAlliance)
    L2_LOG_INFO(
        Log, "Alliance load completion signal received; expected alliance count %d.", g_JobFinishSignal.m_nMustLoadAlliance
    );
}

void CAlliance::CreateAllianceByNpc(CNPC* npc, User* user, const char16_t* name) {
    L2_GUARD_FUNCTION();
    if (npc->m_data == nullptr) {
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->IsPledgeMaster()) {
        RejectAllianceCreation(npc, user, 0x1f8);
        return;
    }
    if (pledge->IsWaitingToDismiss()) {
        RejectAllianceCreation(npc, user, 0x226);
        return;
    }
    if (pledge->HaveCastleUnderSiege() || pledge->GetFortressUnderDefenceSiege() != nullptr ||
        pledge->GetFortressUnderOffenceSiege() != nullptr) {
        RejectAllianceCreation(npc, user, 0x2d1);
        return;
    }
    if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::ALLIANCE)) {
        RejectAllianceCreation(npc, user, 0x225);
        return;
    }
    if (pledge->GetAllianceDbId() != 0) {
        RejectAllianceCreation(npc, user, 0x1f6);
        return;
    }
    if (!pledge->CanCreateAlliance()) {
        RejectAllianceCreation(npc, user, 0x1f9);
        return;
    }

    if (::g_nCountryCode == 8 && !User::CheckCharacterNameDual(name, 8, false)) {
        // Exact current asymmetry: this path does not emit the NPC reject
        // packet used by the ordinary-name failure immediately below.
        user->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_ALLIANCE_NAME);
        return;
    }
    if (!name || !User::CheckCharacterName(name, ::g_nCountryCode, false)) {
        RejectAllianceCreation(npc, user, 0x1fa);
        return;
    }

    const std::uint32_t length = static_cast<std::uint32_t>(std::char_traits<char16_t>::length(name));
    if (length - 1U < 16U) {
        db.RequestCreateAllianceByNpc(npc, user, pledge, name);
        return;
    }
    RejectAllianceCreation(npc, user, 0x1fb);
}

bool CAlliance::CanAcceptNewMemberPledge() {
    L2_GUARD_FUNCTION();
    const std::uint32_t oustTime = m_data->m_nOustTime;
    if (oustTime == 0) {
        return true;
    }
    const std::uint32_t currentTime = static_cast<std::uint32_t>(runtime::crt::Time32Now());
    const std::uint32_t elapsed = currentTime - oustTime;
    return runtime::Int32FromBits(elapsed) >= BP::_ALLIANCE_OUST_PENALTY_TIMEOUT;
}

void CAlliance::Save(std::uint32_t selector) {
    L2_GUARD_FUNCTION();
    std::uint32_t value = 0;
    if (selector == 0) {
        value = m_data->m_nOustTime;
    } else if (selector == 1) {
        value = m_data->m_nCrestId;
    } else {
        return;
    }
    db.RequestSetAllianceInfo(*this, selector, value);
}

CPledge* CAlliance::GetMasterPledge() {
    L2_GUARD_FUNCTION();
    if (m_nMasterPledgeServerId == 0) {
        return nullptr;
    }
    CPledge* const pledge = ResolvePledge(m_nMasterPledgeServerId);
    if (!pledge) {
        m_nMasterPledgeServerId = 0;
    }
    return pledge;
}

bool CAlliance::IsRaidAlliance() {
    L2_GUARD_FUNCTION();
    return m_nWorldId != g_nWorldId;
}

void CAlliance::SendDB(const char* format, ...) const {
    L2_GUARD_FUNCTION();
    const std::int32_t worldId = ::CThreadManager::GetThreadID() < 16 ? m_nWorldId : g_nWorldId;
    runtime::process::SetCurrentWorldId(worldId);
    CDB::CWorldSetter worldSetter;

    va_list arguments;
    va_start(arguments, format);
    if (worldId < 1) {
        SystemMail::sendNotSetThreadLocalWorldID(mailServer);
    } else {
        db.m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

CAlliance* CAlliance::GetObjectW(std::uint32_t smartId) {
    L2_GUARD_FUNCTION();
    return g_AllianceSmartIdManager.GetObjectW(smartId);
}

void CAlliance::GetAllMemberPledge(std::vector<CPledge*>& pledges) {
    L2_GUARD_FUNCTION();
    pledges.clear();
    {
        L2_GUARD(*m_lock);
        for (const auto& entry : m_mapPledge) {
            if (CPledge* const pledge = ResolvePledge(entry.second)) {
                pledges.push_back(pledge);
            }
        }
    }
}

void CAlliance::Say(User* user, const char16_t* message, SayPacketType type, bool unknownFlag, const char16_t* targetName) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->m_PledgeGameUser.IsPlayer()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CHAT_IN_CEREMONY_OF_CHAOS);
        return;
    }

    std::vector<CPledge*> pledges;
    GetAllMemberPledge(pledges);
    for (CPledge* const pledge : pledges) {
        pledge->Say(user, message, type, unknownFlag, targetName);
    }
}

void CAlliance::SayByMobile(
    int firstValue, int secondValue, const char16_t* senderName, const char16_t* message, SayPacketType type
) {
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    GetAllMemberPledge(pledges);
    for (CPledge* const pledge : pledges) {
        pledge->SayByMobile(firstValue, secondValue, senderName, message, type);
    }
}

void CAlliance::SetAllianceCrestToMember(std::uint32_t crestId) {
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    GetAllMemberPledge(pledges);
    for (CPledge* const pledge : pledges) {
        pledge->SetMemberAllianceCrest(crestId);
    }
}

void CAlliance::AddMemberPledge(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    std::uint32_t pledgeDatabaseId;
    bool duplicate;
    bool overflow;
    {
        L2_GUARD(*m_lock);

        pledgeDatabaseId = pledge->GetDbId();
        const std::uint32_t pledgeSid = pledge->GetSmartID().Sid();
        const std::uint32_t pledgeIndex = static_cast<std::uint32_t>(pledge->GetIndex());

        if (pledgeDatabaseId == m_data->m_nMasterPledgeDbId) {
            m_nMasterPledgeServerId = pledgeSid;
        }
        m_mapPledge[pledgeDatabaseId] = pledgeSid;

        duplicate = false;
        for (const IdIndexPair& member : m_data->m_OnlineMember) {
            if (member.id == pledgeSid) {
                duplicate = true;
            }
        }

        overflow = true;
        if (!duplicate) {
            for (IdIndexPair& member : m_data->m_OnlineMember) {
                if (member.id == 0) {
                    member.id = pledgeSid;
                    member.index = pledgeIndex;
                    ++m_data->m_nOnlineMemberCount;
                    overflow = false;
                    break;
                }
            }
        }
    }

    if (overflow && !duplicate) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CAlliance[%d]::AddMemberPledge[%s]-> Overflow",
        //     m_data->m_nDbId,
        //     pledge->GetPledgeName())
        // L2_LOG_ERROR(Log,
        //     L"CAlliance[%d]::AddMemberPledge[%s]-> Overflow",
        //     m_data->m_nDbId,
        //     pledge->GetPledgeName())
        L2_LOG_ERROR(
            Log,
            "Alliance %u has no free online-clan slot for '%s' (clan %u); the clan mapping was already stored and raid "
            "synchronization still follows where applicable.",
            static_cast<unsigned>(m_data->m_nDbId), Utf16ToUtf8(pledge->GetPledgeName()).c_str(),
            static_cast<unsigned>(pledgeDatabaseId)
        );
    }
    if (!IsRaidAlliance()) {
        RaidAllianceService_AddPledgeMember service(pledgeDatabaseId);
        RaidClientManager::inst().RequestAllianceService(*this, service);
    }
}

void CAlliance::DeleteMemberPledge(std::uint32_t pledgeDatabaseId) {
    L2_GUARD_FUNCTION();
    bool memberFound;
    const bool mappingFound = [&] {
        L2_GUARD(*m_lock);
        const auto found = m_mapPledge.find(pledgeDatabaseId);
        if (found == m_mapPledge.end()) {
            return false;
        }
        const std::uint32_t pledgeSid = found->second;
        if (pledgeDatabaseId == m_data->m_nMasterPledgeDbId) {
            m_nMasterPledgeServerId = 0;
        }
        m_mapPledge.erase(found);

        memberFound = false;
        for (IdIndexPair& member : m_data->m_OnlineMember) {
            if (member.id == pledgeSid) {
                member.id = 0;
                member.index = static_cast<std::uint32_t>(-1);
                --m_data->m_nOnlineMemberCount;
                memberFound = true;
                break;
            }
        }
        return true;
    }();
    if (!mappingFound) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CAlliance[%d]::DeleteMemberPledge[%d]-> Cannot find",
        //     m_data->m_nDbId,
        //     pledgeDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"CAlliance[%d]::DeleteMemberPledge[%d]-> Cannot find",
        //     m_data->m_nDbId,
        //     pledgeDatabaseId)
        L2_LOG_ERROR(
            Log, "Cannot remove clan %u from alliance %u: its clan mapping is missing; reporting the stack and returning.",
            static_cast<unsigned>(pledgeDatabaseId), static_cast<unsigned>(m_data->m_nDbId)
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    if (!memberFound) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CAlliance[%d]::DeleteMember[%d]-> Cannot find",
        //     m_data->m_nDbId,
        //     pledgeDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"CAlliance[%d]::DeleteMember[%d]-> Cannot find",
        //     m_data->m_nDbId,
        //     pledgeDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Clan %u had no online-member slot in alliance %u; its clan mapping was already removed. Reporting the stack; "
            "raid synchronization still follows where applicable.",
            static_cast<unsigned>(pledgeDatabaseId), static_cast<unsigned>(m_data->m_nDbId)
        );
        runtime::process::ReportCallStack(2, false);
    }
    if (!IsRaidAlliance()) {
        RaidAllianceService_DelPledgeMember service(pledgeDatabaseId);
        RaidClientManager::inst().RequestAllianceService(*this, service);
    }
}
