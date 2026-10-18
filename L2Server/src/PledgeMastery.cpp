#include "PledgeMastery.h"
#include "DB.h"
#include "L2Packet.h"
#include "Pledge.h"
#include "PledgeMasteryInfo.h"
#include "PledgeMemberService.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/proto/Stream.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>

namespace {


template <std::size_t Size, typename... Args>
std::u16string FormatPledgeMasteryBuilderMessage(const std::u16string_view format, const Args&... args) {
    std::array<char16_t, Size> message{};
    FormatUtf16To(message.data(), message.size(), format, args...);
    return std::u16string(message.data());
}

} // namespace

// static_assert(L2ServerMemory::kPledgeMasteryClassId == 232)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PledgeMastery) == 232);

L2_DEFINE_OBJECT_CLASS_ID(PledgeMastery)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeMastery, m_pClassName_PledgeMastery)

L2_DEFINE_OBJECT_NEW(PledgeMastery)

L2_DEFINE_OBJECT_ALLOCATOR(PledgeMastery, __GetClassName())

L2_DEFINE_OBJECT_ADD_REF(PledgeMastery)

L2_DEFINE_OBJECT_RELEASE(PledgeMastery)

PledgeMastery::PledgeMastery(std::uint32_t pledgeSid, int worldId, int pledgeDatabaseId) :
    m_TransactionLock(0x1e),
    m_sidPledge(pledgeSid),
    m_idWorld(worldId),
    m_dbidPledge(pledgeDatabaseId),
    m_anPoints{},
    m_bLoadedFromDB(false) {}

PledgeMastery::~PledgeMastery() = default;

void PledgeMastery::LoadFromDB() {
    // Current L2Server 0x009ce730.
    L2_GUARD_FUNCTION();
    m_bLoadedFromDB = false;
    const std::uint32_t pledgeSid = m_sidPledge;
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([pledgeSid](AtomicLambdaJobDx* const callback) {
        // Current L2Server 0x009ce1f4.
        static constexpr wchar_t functionName[] = L"PledgeMastery.cpp(133)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        const auto* const packet = static_cast<CDX_PLEDGE_MASTERY_LOAD*>(callback->m_pDx);
        if (!packet->m_bSuccess) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMastery.cpp",
            //     0x8b)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMastery.cpp",
            //     0x8b)
            L2_LOG_ERROR(
                Log,
                "Clan object %u: the database mastery-load operation failed; this callback publishes no points or "
                "completion flag.",
                static_cast<unsigned>(pledgeSid)
            );
            return;
        }

        CPledge* const pledge = CPledge::GetObjectW(pledgeSid);
        if (pledge == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMastery.cpp",
            //     0x93)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMastery.cpp",
            //     0x93)
            L2_LOG_WARN(
                Log, "Cannot publish loaded mastery points: clan object %u is no longer available.",
                static_cast<unsigned>(pledgeSid)
            );
            return;
        }

        PledgeMastery* const mastery = pledge->m_pMastery;
        std::copy(std::begin(packet->m_anPoints), std::end(packet->m_anPoints), mastery->m_anPoints.begin());
        mastery->m_bLoadedFromDB = true;
        if (pledge->HasLoadedAllData()) {
            pledge->LoadingCompleted();
        }
    });

    CDX_PLEDGE_MASTERY_LOAD packet(m_dbidPledge, job);
    db.RequestTPacketProcess(m_idWorld, static_cast<short>(94), static_cast<int>(sizeof(packet)), packet);
}

void PledgeMastery::OnSetPoint(PledgeMasteryID::Type masteryId, int point) {
    L2_GUARD_FUNCTION();
    if (!PledgeStuff::IsValidMasteryID(masteryId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x132)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x132)
        L2_LOG_ERROR(
            Log, "Clan %u rejected mastery point update: invalid mastery ID %d, requested points %d.",
            static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId), point
        );
        return;
    }
    m_anPoints[static_cast<std::size_t>(masteryId)] = point;
}

void PledgeMastery::OnResetPoint() {
    L2_GUARD_FUNCTION();
    m_anPoints.fill(0);
}

int PledgeMastery::GetPoint(PledgeMasteryID::Type masteryId) const {
    if (!PledgeStuff::IsValidMasteryID(masteryId)) {
        return 0;
    }
    return m_anPoints[static_cast<std::size_t>(masteryId)];
}

int PledgeMastery::GetTotalPoint() const noexcept {
    int total = 0;
    for (const int point : m_anPoints) {
        total += point;
    }
    return total;
}

namespace PledgeMasteryStuff {

bool HasRequirement(const PledgeMastery* const mastery, const PledgeMasteryRequirementInfo* const requirement) {
    L2_GUARD_FUNCTION();
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x206,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x206,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-mastery requirements: mastery state is unavailable; requirements are treated as unmet."
        );
        return false;
    }
    if (requirement == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x207,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x207,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot check clan-mastery requirements: the requirement definition is unavailable; requirements are treated as "
            "unmet."
        );
        return false;
    }
    int total = 0;
    for (const PledgeMasteryID::Type id : requirement->MasteryID) {
        total = runtime::AddInt32Wrapping(total, mastery->GetPoint(id));
    }
    return requirement->min_point <= total;
}

bool HasRequirement(const PledgeMastery* const mastery, const PledgeMasteryInfo* const info) {
    L2_GUARD_FUNCTION();
    for (const PledgeMasteryRequirementInfo& requirement : info->Require) {
        if (!HasRequirement(mastery, &requirement)) {
            return false;
        }
    }
    return true;
}

bool HasRequirement(const PledgeMastery* const mastery, const PledgeMasteryID::Type masteryId) {
    L2_GUARD_FUNCTION();
    const PledgeMasteryInfo* const info = PledgeMasteryDB::inst().FindInfo(masteryId);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x1ea)
        // Noise: FindInfo already identifies the missing mastery definition. Requirements are treated as unmet.
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x1ea)
        return false;
    }
    return HasRequirement(mastery, info);
}

PledgeActiveStatus::Value CheckMasteryStatus(const PledgeMastery* const mastery, const PledgeMasteryID::Type masteryId) {
    L2_GUARD_FUNCTION();
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x266,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x266,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot determine clan mastery %d status: mastery state is unavailable; returning locked status.",
            static_cast<int>(masteryId)
        );
        return PledgeActiveStatus::locked;
    }
    if (mastery->GetPoint(masteryId) > 0) {
        return PledgeActiveStatus::activated;
    }
    return HasRequirement(mastery, masteryId) ? PledgeActiveStatus::inactivated : PledgeActiveStatus::locked;
}

} // namespace PledgeMasteryStuff

namespace {

void Assemble_S_EX_PLEDGE_MASTERY_INFO(CPledge* const pledge, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    protocol::S_EX_PLEDGE_MASTERY_INFO info{};
    PledgeMastery* const mastery = pledge->GetMastery();
    info.nTotalPoint = mastery->GetTotalPoint();
    info.nMaxPoint = std::max(static_cast<int>(pledge->GetPledgeLevel()) - 4, 0);
    for (int id = PledgeMasteryID::PLEDGE_LV_1; id < PledgeMasteryID::MAX_COUNT; ++id) {
        const auto masteryId = static_cast<PledgeMasteryID::Type>(id);
        protocol::PkPledgeMasteryInfo row{};
        row.nID = id;
        row.nPoint = mastery->GetPoint(masteryId);
        row.nStatus = PledgeMasteryStuff::CheckMasteryStatus(mastery, masteryId);
        info.vMasteries.push_back(row);
    }

    const int bufferSize = packet.GetBufferSize();
    char* const buffer = packet.GetPacket();
    network::Stream stream(reinterpret_cast<unsigned char*>(buffer), bufferSize);
    const std::uint8_t opcode = 0xfe;
    const std::uint16_t extendedOpcode = 0x1e2;
    if (stream.Encode(&opcode) && stream.Encode(&extendedOpcode) && info.Encode(&stream)) {
        packet.SetLength(static_cast<int>(stream.GetCursor() - stream.GetBuffer()));
    }
}

void Send_S_EX_PLEDGE_MASTERY_SET(User* const user, const int result) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x1e,
        //     L"(pUser != NULL)")
        // Noise: The set-point callback can outlive its requester. No reply can be sent. Operation failures are separately
        // reported with captured IDs. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x1e,
        //     L"(pUser != NULL)")
        return;
    }
    protocol::S_EX_PLEDGE_MASTERY_SET packet{};
    packet.nResult = static_cast<char>(result);
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_MASTERY_SET, packet);
}

void Send_S_EX_PLEDGE_MASTERY_RESET(User* const user, const int result) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x2d,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x2d,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send clan-mastery reset result %d: the requester is unavailable.", result);
        return;
    }
    protocol::S_EX_PLEDGE_MASTERY_RESET packet{};
    packet.nResult = static_cast<char>(result);
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_MASTERY_RESET, packet);
}

void EndMasteryJob(CPledge* const pledge, const int line) {
    if (pledge != nullptr && !pledge->GetMastery()->GetTransactionLock().EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeMastery.cpp",
        //     line)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeMastery.cpp",
        //     line)
        L2_LOG_ERROR(
            Log, "Clan %u: mastery %s reached cleanup with no active transaction.", static_cast<unsigned>(pledge->GetDbId()),
            line == 0x145       ? "reset rejection"
                : line == 0x181 ? "reset callback"
                : line == 0x110 ? "set-point callback"
                                : "operation"
        );
    }
}

} // namespace

namespace PledgeMasteryStuff {

int GetMaxMasteryPoint(const CPledge* const pledge) {
    if (pledge == nullptr) {
        return 0;
    }
    return std::max(static_cast<int>(const_cast<CPledge*>(pledge)->GetPledgeLevel()) - 4, 0);
}

int GetRemainMasteryPoint(const CPledge* const pledge) {
    if (pledge == nullptr || pledge->GetMastery() == nullptr) {
        return 0;
    }
    return GetMaxMasteryPoint(pledge) - pledge->GetMastery()->GetTotalPoint();
}

namespace Builder {

void SendPledgeMasteryInfoTo(CUserSocket* const socket, const CPledge* const pledge) {
    // Current L2Server.exe 0x009cf24c.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x27a,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x27a,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot send the builder mastery list: the clan is unavailable.");
        return;
    }

    const PledgeMastery* const mastery = pledge->GetMastery();
    PledgeStuff::SendSystemMessage(
        socket,
        FormatPledgeMasteryBuilderMessage<128>(u"Mastery point: %d/%d", mastery->GetTotalPoint(), GetMaxMasteryPoint(pledge))
    );
    for (int rawId = PledgeMasteryID::PLEDGE_LV_1; rawId < PledgeMasteryID::MAX_COUNT; ++rawId) {
        const auto masteryId = static_cast<PledgeMasteryID::Type>(rawId);
        PledgeStuff::SendSystemMessage(
            socket,
            FormatPledgeMasteryBuilderMessage<128>(
                u"%d: (%d/%d)", rawId, static_cast<int>(CheckMasteryStatus(mastery, masteryId)), mastery->GetPoint(masteryId)
            )
        );
    }
}

} // namespace Builder

void SendPledgeMasteryInfoTo(User* const user) {
    // Current L2Server 0x009cfefc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x219,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x219,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send clan-mastery information: the requesting user is unavailable.");
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x21f)
        // Noise: A user without a current clan has no mastery information to receive. The request returns without sending a
        // reply. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x21f)
        return;
    }
    CPacketHelperEx<20480> packet;
    Assemble_S_EX_PLEDGE_MASTERY_INFO(pledge, packet);
    PledgeStuff::SendPacket(user, packet);
}

void SendPledgeMasteryInfoToMembers(CPledge* const pledge) {
    // Current L2Server 0x009d0070.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x22f,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x22f,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot broadcast clan-mastery information: the clan is unavailable.");
        return;
    }

    CPacketHelperEx<20480> packet;
    Assemble_S_EX_PLEDGE_MASTERY_INFO(pledge, packet);
    GenericService service(&packet);
    pledge->ServeForAllMember(service, true);
}

void RequestResetPoint(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x251,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x251,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request a clan-mastery reset: the requesting user is unavailable.");
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x257)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x257)
        L2_LOG_WARN(
            Log, "Character %u cannot request a mastery reset without a current clan.",
            static_cast<unsigned>(user->GetDBID())
        );
        return;
    }
    pledge->GetMastery()->RequestResetPoint(user, pledge);
}

void RequestSetPoint(User* const user, const PledgeMasteryID::Type masteryId) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x23e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x23e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot request a point for clan mastery %d: the requesting user is unavailable.",
            static_cast<int>(masteryId)
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x244)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMastery.cpp",
        //     0x244)
        L2_LOG_WARN(
            Log, "Character %u cannot request a point for mastery %d without a current clan.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(masteryId)
        );
        return;
    }
    pledge->GetMastery()->RequestSetPoint(user, pledge, masteryId);
}

} // namespace PledgeMasteryStuff

bool PledgeMastery::RequestResetPoint(User* const user, CPledge* const pledge) {
    // Current L2Server 0x009ce80c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x140,
        //     user == nullptr ? L"(pUser != NULL)" : L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0x140,
        //     user == nullptr ? L"(pUser != NULL)" : L"(pPledge != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot reset clan mastery points: %s is unavailable.", user == nullptr ? "the requesting user" : "the clan"
        );
        return false;
    }

    JobTransactionLong jobType = 2;
    if (!m_TransactionLock.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0x149,
        //     m_dbidPledge,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0x149,
        //     m_dbidPledge,
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Clan %u rejected the mastery reset: transaction %d is already active.",
            static_cast<unsigned>(m_dbidPledge), static_cast<int>(jobType)
        );
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        return false;
    }
    if (!user->HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        EndMasteryJob(pledge, 0x145);
        return false;
    }
    if (GetTotalPoint() == 0) {
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]GetTotalPoint is 0: %d",
        //     L"PledgeMastery.cpp",
        //     0x15a,
        //     m_dbidPledge)
        // Noise: Resetting zero allocated mastery points is a normal rejected request. The requester already receives
        // feedback and the transaction is ended. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]GetTotalPoint is 0: %d",
        //     L"PledgeMastery.cpp",
        //     0x15a,
        //     m_dbidPledge)
        EndMasteryJob(pledge, 0x145);
        return false;
    }

    PledgeMasteryDB& database = PledgeMasteryDB::inst();
    if (!pledge->CanResetMastery(database.Reset.penalty_day)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_FUNCTION_CONSECUTIVELY);
        EndMasteryJob(pledge, 0x145);
        return false;
    }

    PledgeStuff::PledgeCostHelper helper(user, pledge);
    helper.SetCost(database.Reset.Cost);
    helper.AppendEtcMani(
        new CETC_MANI_PLEDGE_MASTERY_RESET(pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()), user->Id())
    );
    PledgeStuff::Write_LOG_PLEDGE_MASTERY_RESET(user, pledge, PledgeStuff::ActionStep::Try);

    const UserCapture userCapture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
    const PledgeCapture pledgeCapture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
    helper.Do(MakeLambdaJob([userCapture, pledgeCapture, jobType](const int result) {
        // Current L2Server 0x009d0418.
        static constexpr wchar_t functionName[] = L"PledgeMastery.cpp(371)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        CSPointer<User> requester(userCapture.GetUser(), ObjectRefType::FunctionChain);
        Send_S_EX_PLEDGE_MASTERY_RESET(requester.get(), result);
        CPledge* const owner = pledgeCapture.GetPledge();
        if (owner == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]No pledge: %d",
            //     L"PledgeMastery.cpp",
            //     0x17d,
            //     static_cast<int>(jobType))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]No pledge: %d",
            //     L"PledgeMastery.cpp",
            //     0x17d,
            //     static_cast<int>(jobType))
            L2_LOG_ERROR(
                Log,
                "Clan %u is unavailable in the mastery-reset callback (character %u, job %d, result %d); this callback "
                "stops before transaction cleanup.",
                static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<unsigned>(userCapture.m_dbid),
                static_cast<int>(jobType), result
            );
            return;
        }
        if (result == 0) {
            owner->SetMasteryResetTime(static_cast<unsigned int>(runtime::crt::Time32Now()));
            owner->Save(0x15);
            PledgeStuff::RemovePrivilegeByMastery(owner);
            PledgeMasteryStuff::SendPledgeMasteryInfoToMembers(owner);
            PMS_SyncPledgeMastery sync(PledgeMasteryID::NONE, 0);
            owner->ServeForAllMember(sync, true);
            ::SystemMessage<NoCheck, User> message(SystemMessageId::ALL_CLAN_SPECIALIZATIONS_RESET);
            owner->ServeForAllMember(message, false);
            PledgeStuff::Write_LOG_PLEDGE_MASTERY_RESET(requester.get(), owner, PledgeStuff::ActionStep::Success);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to reset mastery point: %d, %d",
            //     L"PledgeMastery.cpp",
            //     0x185,
            //     static_cast<int>(jobType),
            //     result)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to reset mastery point: %d, %d",
            //     L"PledgeMastery.cpp",
            //     0x185,
            //     static_cast<int>(jobType),
            //     result)
            L2_LOG_ERROR(
                Log, "Clan %u, character %u: mastery reset returned %d for job %d; %s.",
                static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<unsigned>(userCapture.m_dbid), result,
                static_cast<int>(jobType), result == -4 ? "insufficient reset funds" : "the reset operation failed"
            );
            requester->SendSystemMessage(
                result == -4 ? SystemMessageId::NOT_ENOUGH_FUNDS_FOR_RESET : SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR
            );
        }
        EndMasteryJob(owner, 0x181);
    }));
    return true;
}

bool PledgeMastery::RequestSetPoint(User* const user, CPledge* const pledge, const PledgeMasteryID::Type masteryId) {
    // Current L2Server 0x009cf3fc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0xb0,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0xb0,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot add a point to clan mastery %d: the requesting user is unavailable.", static_cast<int>(masteryId)
        );
        return false;
    }
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0xb1,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMastery.cpp",
        //     0xb1,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot add a point to clan mastery %d: the clan is unavailable.", static_cast<int>(masteryId));
        return false;
    }

    JobTransactionLong jobType = 1;
    if (!m_TransactionLock.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xb9,
        //     m_dbidPledge,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xb9,
        //     m_dbidPledge,
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Clan %u rejected adding a point to mastery %d: transaction %d is already active.",
            static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId), static_cast<int>(jobType)
        );
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        return false;
    }
    if (!user->HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        if (!m_TransactionLock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            L2_LOG_ERROR(
                Log,
                "Clan %u: rejection of mastery %d for insufficient authority reached cleanup with no active transaction.",
                static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId)
            );
        }
        return false;
    }
    if (PledgeMasteryStuff::GetRemainMasteryPoint(pledge) < 1) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_FUNDS_TO_UNLOCK_SPECIALIZATION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]GetRemainMasteryPoint failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xca,
        //     m_dbidPledge,
        //     PledgeMasteryStuff::GetRemainMasteryPoint(pledge))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]GetRemainMasteryPoint failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xca,
        //     m_dbidPledge,
        //     PledgeMasteryStuff::GetRemainMasteryPoint(pledge))
        L2_LOG_WARN(
            Log, "Clan %u rejected adding a point to mastery %d: fewer than one point remains (currently %d).",
            static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId),
            PledgeMasteryStuff::GetRemainMasteryPoint(pledge)
        );
        if (!m_TransactionLock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            L2_LOG_ERROR(
                Log,
                "Clan %u: rejection of mastery %d for insufficient remaining points reached cleanup with no active "
                "transaction.",
                static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId)
            );
        }
        return false;
    }

    const PledgeMasteryInfo* const info = PledgeMasteryDB::inst().FindInfo(masteryId);
    if (info == nullptr) {
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]FindInfo failed: %d",
        //     L"PledgeMastery.cpp",
        //     0xd4,
        //     static_cast<int>(masteryId))
        // Noise: FindInfo already identifies the missing mastery definition. The requester receives an error and the
        // transaction is ended. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]FindInfo failed: %d",
        //     L"PledgeMastery.cpp",
        //     0xd4,
        //     static_cast<int>(masteryId))
        if (!m_TransactionLock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            L2_LOG_ERROR(
                Log, "Clan %u: rejection of mastery %d for missing definition reached cleanup with no active transaction.",
                static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId)
            );
        }
        return false;
    }
    if (!PledgeMasteryStuff::HasRequirement(pledge->GetMastery(), info)) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_FUNDS_TO_UNLOCK_SPECIALIZATION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]HasRequirement failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xde,
        //     m_dbidPledge,
        //     static_cast<int>(masteryId))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]HasRequirement failed: %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xde,
        //     m_dbidPledge,
        //     static_cast<int>(masteryId))
        L2_LOG_WARN(
            Log, "Clan %u rejected adding a point to mastery %d: mastery requirements are unmet.",
            static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId)
        );
        if (!m_TransactionLock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            L2_LOG_ERROR(
                Log, "Clan %u: rejection of mastery %d for unmet requirements reached cleanup with no active transaction.",
                static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId)
            );
        }
        return false;
    }

    const int nextPoint = runtime::AddInt32Wrapping(pledge->GetMastery()->GetPoint(masteryId), 1);
    if (!PledgeMasteryDB::inst().HasPointStep(masteryId, nextPoint)) {
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        PledgeStuff::SendSystemMessage(user, std::u16string(u"No more mastery point step"));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]HasPointStep failed: %d, %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xe9,
        //     m_dbidPledge,
        //     static_cast<int>(masteryId),
        //     nextPoint)
        // Noise: HasPointStep already reports a missing definition or invalid point step. The requester receives feedback
        // and the transaction is ended. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]HasPointStep failed: %d, %d, %d",
        //     L"PledgeMastery.cpp",
        //     0xe9,
        //     m_dbidPledge,
        //     static_cast<int>(masteryId),
        //     nextPoint)
        if (!m_TransactionLock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeMastery.cpp",
            //     0xb5)
            L2_LOG_ERROR(
                Log,
                "Clan %u: rejection of mastery %d for unavailable point step reached cleanup with no active transaction.",
                static_cast<unsigned>(m_dbidPledge), static_cast<int>(masteryId)
            );
        }
        return false;
    }

    PledgeStuff::PledgeCostHelper helper(user, pledge);
    helper.SetCost(PledgeMasteryDB::inst().GetPointCost(masteryId, nextPoint));
    helper.AppendEtcMani(new CETC_MANI_PLEDGE_MASTERY_SET(
        pledge->GetWorldId(), static_cast<int>(pledge->GetDbId()), masteryId, nextPoint, user->Id()
    ));
    PledgeStuff::Write_LOG_PLEDGE_MASTERY_SET(user, pledge, masteryId, nextPoint, PledgeStuff::ActionStep::Try);

    const UserCapture userCapture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
    const PledgeCapture pledgeCapture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
    helper.Do(MakeLambdaJob([userCapture, pledgeCapture, masteryId, nextPoint, jobType](const int result) {
        // Current L2Server 0x009d01d4.
        static constexpr wchar_t functionName[] = L"PledgeMastery.cpp(258)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        CPledge* const owner = pledgeCapture.GetPledge();
        if (owner == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMastery.cpp",
            //     0x108)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMastery.cpp",
            //     0x108)
            L2_LOG_ERROR(
                Log,
                "Clan %u is unavailable in the mastery set-point callback (mastery %d, points %d, character %u, job %d, "
                "result %d); this callback stops before transaction cleanup.",
                static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<int>(masteryId), nextPoint,
                static_cast<unsigned>(userCapture.m_dbid), static_cast<int>(jobType), result
            );
            return;
        }
        User* const requester = userCapture.GetUser();
        Send_S_EX_PLEDGE_MASTERY_SET(requester, result);
        if (result == 0) {
            PledgeStuff::AddPrivilegeByLevelAndMastery(owner, true);
            owner->UpdateInfoWindowPacket();
            PledgeMasteryStuff::SendPledgeMasteryInfoToMembers(owner);
            PledgeStuff::SendSystemMessage_MasteryID(owner, SystemMessageId::NAMED_CLAN_SPECIALIZATION_UNLOCKED, masteryId);
            PMS_SyncPledgeMastery sync(masteryId, nextPoint);
            owner->ServeForAllMember(sync, true);
            PledgeStuff::Write_LOG_PLEDGE_MASTERY_SET(
                requester, owner, masteryId, nextPoint, PledgeStuff::ActionStep::Success
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]SetMastery failed: %d, %d, %d, %d",
            //     L"PledgeMastery.cpp",
            //     0x114,
            //     static_cast<int>(jobType),
            //     static_cast<int>(masteryId),
            //     nextPoint,
            //     result)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]SetMastery failed: %d, %d, %d, %d",
            //     L"PledgeMastery.cpp",
            //     0x114,
            //     static_cast<int>(jobType),
            //     static_cast<int>(masteryId),
            //     nextPoint,
            //     result)
            L2_LOG_ERROR(
                Log, "Clan %u, character %u: setting mastery %d to %d points failed with result %d (job %d).",
                static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<unsigned>(userCapture.m_dbid),
                static_cast<int>(masteryId), nextPoint, result, static_cast<int>(jobType)
            );
        }
        EndMasteryJob(owner, 0x110);
    }));
    return true;
}
