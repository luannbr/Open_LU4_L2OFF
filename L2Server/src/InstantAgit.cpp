#include "InstantAgit.h"
#include "AgitDeco.h"
#include "AgitDecoDb.h"
#include "CConfig.h"
#include "DB.h"
#include "DecoNpcData.h"
#include "IOCPMessageServer.h"
#include "InstantAgitParser.h"
#include "InstantZone.h"
#include "L2Packet.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <vector>

namespace InstantAgitConst {

// Current L2Server .data 0x015d4688. The builder interval command is the
// only writer. Both manager timer sites are the current readers.
std::int32_t _TIMER_PERIOD = 60000;

} // namespace InstantAgitConst

namespace {


void _WriteDecoNpcChangeOwnerLog(int oldPledgeId, int instantAgitId, const SharedInstantAgitDeco* deco) {
    L2_GUARD_FUNCTION();
    if (deco == nullptr) {
        return;
    }

    const FVector position{};
    for (int slot = 0; slot < 5; ++slot) {
        const auto& npc = deco->aDecoNpc[slot];
        if (npc.nCID != 0) {
            ResidenceUtil::WriteDecoNpcSetLog(
                0x125, 0, 0, position, oldPledgeId, instantAgitId, slot, npc.nCID, npc.tExpire,
                ResidenceUtil::DecoNpcSetReason::ChangeOwner
            );
        }
    }
}

void _GetNeedToDeleteDecoNpcs(std::vector<int>& npcSids, const SharedInstantAgitDeco* deco) {
    L2_GUARD_FUNCTION();
    if (deco == nullptr) {
        return;
    }
    for (const auto& npc : deco->aDecoNpc) {
        if (npc.nCID != 0 && npc.nNpcSID != 0) {
            npcSids.push_back(npc.nNpcSID);
        }
    }
}

void DeleteDecoNpcs(const std::vector<int>& npcSids) {
    L2_GUARD_FUNCTION();
    for (const int npcSid : npcSids) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
        if (npc != nullptr && !npc->IsDeletingObject() && npc->m_Deleted == 0) {
            npc->Despawn(false, false);
        }
    }
}

template <typename Owner>
std::int32_t ReleaseInstantAgitObject(
    Owner* owner,
    volatile std::int32_t* referenceCount,
    int classId,
    const char* className,
    const char* reportFile,
    int reportLine,
    const char* callerFile,
    int callerLine
) {
    const std::int32_t remaining = runtime::raw_atomic::AddI32(referenceCount, -1);
    if (remaining == 0) {
        if (!owner->IsInTimer()) {
            void* const allocation = owner;
            owner->~Owner();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer(className, callerFile, callerLine);
            owner->ReportOrt(reportFile, reportLine);
        }
        runtime::process::DecrementMemoryClassReference(classId);
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, className, owner->MemoryObjectID(), callerFile, callerLine);
        owner->ReportOrt(reportFile, reportLine);
        runtime::process::ReportCallStack(LOG_ERROR, true);
    }
    return remaining;
}

} // namespace

L2_DEFINE_OBJECT_CLASS_ID(CInstantAgitData)

L2_DEFINE_OBJECT_CLASS_NAME(CInstantAgitData, m_pClassName_CInstantAgitData)

L2_DEFINE_OBJECT_ALLOCATOR(CInstantAgitData, __GetClassName())

L2_DEFINE_OBJECT_NEW(CInstantAgitData)

CInstantAgitData::CInstantAgitData(int id) :
    m_Id(id),
    m_padding1c{},
    m_RentalFee(0),
    m_Commission(0.0),
    m_RetalPeriod(0),
    m_ApplyPeriod(0),
    m_MinPledgeLevel(0),
    m_InstantZoneTypeId(0),
    m_MaxChannel(0),
    m_padding44{},
    m_nResidenceGrade(0),
    m_nDecoNpcSlotCount(0),
    m_aDecoNpcSlotPositions{},
    m_aDecoNpcSlotYaws{},
    m_padding104{},
    m_NextLotteryDate(0),
    m_padding12c{},
    m_pFirstInstantAgit(nullptr),
    m_SharedInstantAgitApplyData(nullptr) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_ApplicantSet.clear();
    m_SharedInstantAgitApplyData = SharedInstantAgitApplyData::GetInfo(m_Id);
}

CInstantAgitData::~CInstantAgitData() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_ApplicantSet.clear();
}

std::int32_t CInstantAgitData::Release(const char* callerFile, int callerLine, ObjectRefType, bool) {
    return ReleaseInstantAgitObject(this, &m_nRefCount, __GetClassID(), "CInstantAgitData", L2_SOURCE_FILE, __LINE__, callerFile, callerLine);
}

void CInstantAgitData::Init(int maxChannel, FVector& position) {
    L2_GUARD_FUNCTION();
    m_MaxChannel = maxChannel;
    m_InstantAgitPos = position;
}

void CInstantAgitData::Delete() {
    L2_GUARD_FUNCTION();
    CInstantAgit* agit = m_pFirstInstantAgit;
    while (agit != nullptr) {
        CInstantAgit* const next = agit->m_pNextInstantAgit;
        L2_RELEASE(agit, ObjectRefType::Anonymous);
        agit = next;
    }
    L2_RELEASE(this, ObjectRefType::Anonymous);
}

void CInstantAgitData::SetNextLotteryDate(std::int32_t lotteryDate) {
    L2_GUARD_FUNCTION();
    m_NextLotteryDate = lotteryDate;
    if (m_SharedInstantAgitApplyData != nullptr) {
        m_SharedInstantAgitApplyData->SetLotteryDate(
            lotteryDate - m_ApplyPeriod, lotteryDate - InstantAgitJobType::RENTAL_PERIOD_BUFFER
        );
    }
}

bool CInstantAgitData::CanUse() {
    L2_GUARD_FUNCTION();
    return runtime::crt::Time32Now() < SharedInstantAgitApplyData::MakeLastApplyDate(m_NextLotteryDate);
}

void CInstantAgitData::ShowAgitStatus(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }

    char16_t message[512]{};
    if (const std::tm* const next = runtime::crt::LocalTime32(m_NextLotteryDate)) {
        FormatUtf16To(
            message, std::size(message), u"[%d] NextBidDate:%d-%02d-%02d %02d:%02d:%02d", m_Id, next->tm_year + 1900,
            next->tm_mon + 1, next->tm_mday, next->tm_hour, next->tm_min, next->tm_sec
        );
        socket->Send("cS", 0x9a, message);
    }

    const std::int32_t nextApply = m_NextLotteryDate - m_ApplyPeriod;
    if (const std::tm* const next = runtime::crt::LocalTime32(nextApply)) {
        FormatUtf16To(
            message, std::size(message), u"[%d] NextApplyDate:%d-%02d-%02d %02d:%02d:%02d", m_Id, next->tm_year + 1900,
            next->tm_mon + 1, next->tm_mday, next->tm_hour, next->tm_min, next->tm_sec
        );
        socket->Send("cS", 0x9a, message);
    }
}

bool CInstantAgitData::CanChooseOwner(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    return m_NextLotteryDate <= currentTime;
}

void CInstantAgitData::AddDecoNpcSlot(FVector& position, int yaw) {
    if (m_nDecoNpcSlotCount < 5) {
        m_aDecoNpcSlotPositions[m_nDecoNpcSlotCount] = position;
        m_aDecoNpcSlotYaws[m_nDecoNpcSlotCount] = yaw;
        ++m_nDecoNpcSlotCount;
    }
}

void CInstantAgitData::PostLoad() {
    L2_GUARD_FUNCTION();
    CInstantAgit* previous = nullptr;
    for (int channel = 0; channel < m_MaxChannel; ++channel) {
        CInstantAgit* const agit = new CInstantAgit(this);
        if (previous == nullptr) {
            m_pFirstInstantAgit = agit;
        } else {
            previous->m_pNextInstantAgit = agit;
        }
        previous = agit;
    }
    if (m_SharedInstantAgitApplyData != nullptr) {
        m_SharedInstantAgitApplyData->Init(m_Id, m_MaxChannel, static_cast<int>(m_RentalFee));
    }
}

void CInstantAgitData::InitNPC() {
    L2_GUARD_FUNCTION();
    CInstantAgit* agit = m_pFirstInstantAgit;
    while (agit != nullptr) {
        CInstantAgit* const next = agit->m_pNextInstantAgit;
        if (CInstantZone* const zone = agit->GetMyInstantZone()) {
            zone->InitNPC();
        }
        agit = next;
    }
}

void CInstantAgitData::OnApply(int pledgeId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_ApplicantSet.insert(pledgeId);
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR][InstantAgitApply] CInstantAgitData Insert to Set! "
    //     "PledgeDBID[%d] InstantAgitId[%d]",
    //     pledgeId,
    //     m_Id)
    // Noise: Routine applicant-set insertion duplicates the application flow. It is neither a failure nor an ownership
    // result. L2_LOG_ERROR(Log,
    //     "[NO_ERROR][InstantAgitApply] CInstantAgitData Insert to Set! "
    //     "PledgeDBID[%d] InstantAgitId[%d]",
    //     pledgeId,
    //     m_Id)
    if (m_SharedInstantAgitApplyData != nullptr) {
        ++m_SharedInstantAgitApplyData->applicantCount;
    }
}

InstantAgitApplyResponse::Code CInstantAgitData::CanApply(User& user, bool builder) {
    L2_GUARD_FUNCTION();

    const std::int32_t currentTime = runtime::crt::Time32Now();
    const std::int32_t firstApplyDate = SharedInstantAgitApplyData::MakeFirstApplyDate(m_NextLotteryDate, m_ApplyPeriod);
    const std::int32_t lastApplyDate = SharedInstantAgitApplyData::MakeLastApplyDate(m_NextLotteryDate);
    if ((currentTime < firstApplyDate || lastApplyDate < currentTime) && !builder) {
        return InstantAgitApplyResponse::NOT_APPLY_PERIOD;
    }

    CPledge* const pledge = user.GetPledge();
    if (pledge == nullptr) {
        return InstantAgitApplyResponse::NO_PLEDGE;
    }
    if (pledge->GetWorldAgit() != nullptr || CInstantAgitManager::inst().GetInstantAgit(user) != nullptr) {
        return InstantAgitApplyResponse::DUPLICATED_AGIT;
    }
    if (pledge->GetPledgeLevel() < static_cast<std::uint32_t>(m_MinPledgeLevel)) {
        return InstantAgitApplyResponse::INVALID_PLEDGE_LEVEL;
    }
    if (!user.HavePledgePower(PP_AGIT_AUCTION)) {
        return InstantAgitApplyResponse::NO_PLEDGE_POWER;
    }
    return CResidence::IsSiegeRegistered(pledge) ? InstantAgitApplyResponse::DUPLICATED_AGIT_SIEGE
                                                 : InstantAgitApplyResponse::SUCCESS;
}

bool CInstantAgitData::TryApply(int userSid, User& user, bool builder) {
    L2_GUARD_FUNCTION();
    const InstantAgitApplyResponse::Code response = CanApply(user, builder);
    if (response == InstantAgitApplyResponse::SUCCESS) {
        CApplyInstantAgit request{};
        request.npcIndex = userSid;
        request.userIndex = user.GetIndex();
        request.instantAgitId = m_Id;
        request.pledgeDBId = static_cast<int>(user.m_data->m_nPledgeID);
        request.rentalFee = static_cast<int>(m_RentalFee);
        request.inzoneTypeId = m_InstantZoneTypeId;
        request.applyTime = runtime::crt::Time32Now();
        request.nReturn = 0;
        request.nReason = 0;
        db.RequestTPacketProcess(g_nWorldId, 0x35, static_cast<int>(sizeof(request)), request);
        return true;
    }

    CInstantAgitManager::SendApplyResponse(userSid, user.GetIndex(), response, m_InstantZoneTypeId, m_Id, m_RentalFee);
    if (builder) {
        char16_t message[256]{};
        FormatUtf16To(
            message, std::size(message),
            u"[%d]Apply Fail Reason : %d(1:not_apply_period "
            u"2:duplicated_agit 3:no_pledge 4:invalid_pledge_level "
            u"5:no_pledge_power 6:no_adena "
            u"7:DUPLICATED_AGIT_SIEGE)",
            m_Id, static_cast<int>(response)
        );
        if (CUserSocket* const socket = user.GetSocket()) {
            socket->SendSystemMessage(u"SYS", message);
        }
    }
    return false;
}

void CInstantAgitData::SetChooseOwner(std::int32_t currentTime, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    std::tm* const nextLottery = runtime::crt::LocalTime32(m_NextLotteryDate);
    std::int32_t nextLotteryDate = -1;
    do {
        nextLotteryDate = runtime::crt::Mktime32(nextLottery);
        if (currentTime < nextLotteryDate) {
            break;
        }
        nextLottery->tm_mday += m_RetalPeriod;
    } while (true);

    SetNextLotteryDate(nextLotteryDate);
    db.RequestTryChooseInstantAgitOwner(
        m_Id, m_NextLotteryDate,
        static_cast<std::int64_t>(((100.0 - m_Commission) * static_cast<double>(m_RentalFee)) / 100.0), packet
    );
}

void CInstantAgitData::_TryChooseOwner(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();

    std::set<int> applicants;
    for (const int applicant : m_ApplicantSet) {
        applicants.insert(applicant);
    }
    CPacketHelperEx<20480> packet;
    const int applicantCount = static_cast<int>(applicants.size());
    int winnerCount = 0;
    while (!applicants.empty() && winnerCount < m_MaxChannel) {
        const int selected =
            static_cast<int>(::genrand64_real2() * static_cast<double>(static_cast<int>(applicants.size())) + 0.0);
        auto winner = applicants.begin();
        std::advance(winner, selected);
        packet.Assemble("d", *winner);
        applicants.erase(winner);
        ++winnerCount;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] inzone agit [%d][applicant:%d][winner:%d][max channel:%d]",
    //     kInstantAgitSourceWide,
    //     0x19e,
    //     m_Id,
    //     applicantCount,
    //     winnerCount,
    //     m_MaxChannel)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] inzone agit [%d][applicant:%d][winner:%d][max channel:%d]",
    //     kInstantAgitSourceWide,
    //     0x19e,
    //     m_Id,
    //     applicantCount,
    //     winnerCount,
    //     m_MaxChannel)
    // Noise: an empty lottery produces no useful operator summary. Winner selection and the database request still run.
    if (applicantCount != 0 || winnerCount != 0) {
        L2_LOG_INFO(
            Log, "Provisional clan hall %d: prepared %d winners from %d applicants (capacity %d) for the database request.",
            m_Id, winnerCount, applicantCount, m_MaxChannel
        );
    }
    SetChooseOwner(currentTime, packet);
}

void CInstantAgitData::ResetAgitAll() {
    L2_GUARD_FUNCTION();

    for (CInstantAgit* agit = m_pFirstInstantAgit; agit != nullptr; agit = agit->m_pNextInstantAgit) {
        const int pledgeId = agit->m_OwnerPledgeId;
        if (pledgeId < 1) {
            break;
        }

        if (agit->ResetOwner()) {
            CInstantAgitManager::inst().ResetInstantAgit(pledgeId);
            CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
            if (pledge) {
                pledge->UpdateInfoWindowPacket();
                const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
                CLogFormatter formatter;
                formatter.WriteLog(
                    0x7f6, pledgeId, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), u"", u"", pledge->GetPledgeLevel(),
                    pledge->GetDbMemberCount(), m_Id, m_InstantZoneTypeId, 0, 0, 0, 0, 0, 0, u"", u"", u"", u"", 0
                );
            }
        }

        CReturnInstantAgit request{};
        request.npcIndex = -1;
        request.userIndex = -1;
        request.pledgeDBId = pledgeId;
        request.nReturn = 0;
        db.RequestTPacketProcess(g_nWorldId, 0x37, static_cast<int>(sizeof(request)), request);
    }
}

void CInstantAgitData::OnChooseOwner(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    CInstantAgit* available = m_pFirstInstantAgit;
    while (available != nullptr && available->m_OwnerPledgeId > 0) {
        available = available->m_pNextInstantAgit;
    }

    int winnerCount = 0;
    packet = ::Disassemble(packet, "d", &winnerCount);
    for (int winnerIndex = 0; winnerIndex < winnerCount; ++winnerIndex) {
        int pledgeId = 0;
        packet = ::Disassemble(packet, "d", &pledgeId);
        if (available == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid instant agit count[now:%d][winner:%d]"
            //     L"[max:%d] unlucky pledge[%d]",
            //     kInstantAgitSourceWide,
            //     0x207,
            //     winnerIndex,
            //     winnerCount,
            //     m_MaxChannel,
            //     pledgeId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid instant agit count[now:%d][winner:%d]"
            //     L"[max:%d] unlucky pledge[%d]",
            //     kInstantAgitSourceWide,
            //     0x207,
            //     winnerIndex,
            //     winnerCount,
            //     m_MaxChannel,
            //     pledgeId)
            L2_LOG_ERROR(
                Log,
                "Provisional clan hall %d: no free instance for winner %d of %d, clan %d (capacity %d); this winner is "
                "skipped.",
                m_Id, winnerIndex + 1, winnerCount, pledgeId, m_MaxChannel
            );
            continue;
        }

        CInstantAgitManager& manager = CInstantAgitManager::inst();
        if (!manager._AddPledgeInstantAgit(pledgeId, available)) {
            continue;
        }

        available->SetOwner(pledgeId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
        if (pledge) {
            pledge->UpdateInfoWindowPacket();
            pledge->SystemMessage(SystemMessageId::PROVISIONAL_CLAN_HALL_BID_WON);
        }
        g_npcserver.Send("ccddd", 0xaf, 6, manager.m_AgitManagerIndex, pledgeId, 1);

        CLogFormatter formatter;
        formatter.WriteLog(
            0x7f4, pledgeId, 0, 0, 0, 0, 0, 0, u"", u"", u"", 0, 0, m_Id, m_InstantZoneTypeId, m_RentalFee, 0, 0, 0, 0, 0,
            u"", u"", u"", u"", 0
        );
        available = available->m_pNextInstantAgit;
    }

    L2_GUARD(m_Lock);
    for (const int pledgeId : m_ApplicantSet) {
        CInstantAgitManager& manager = CInstantAgitManager::inst();
        if (manager.GetInstantAgit(pledgeId) == nullptr) {
            CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
            if (pledge) {
                if (User* const master = pledge->GetMaster()) {
                    master->SendSystemMessage(SystemMessageId::PROVISIONAL_CLAN_HALL_BID_LOST);
                }
                const std::int64_t refund =
                    static_cast<std::int64_t>(((100.0 - m_Commission) * static_cast<double>(m_RentalFee)) / 100.0);
                const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
                CLogFormatter formatter;
                formatter.WriteLog(
                    0x7f5, pledgeId, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), u"", u"", pledge->GetPledgeLevel(),
                    pledge->GetDbMemberCount(), m_Id, m_InstantZoneTypeId, m_RentalFee - refund, refund, 0, 0, 0, 0, u"",
                    u"", u"", u"", 0
                );
            }
        }
        manager._DeleteApplicant(pledgeId);
    }
    m_ApplicantSet.clear();
}

void CInstantAgitData::TryChooseOwner(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    if (!CanChooseOwner(currentTime)) {
        return;
    }

    L2_GUARD(m_Lock);
    ResetAgitAll();
    _TryChooseOwner(currentTime);
}

void CInstantAgitData::OnTick(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    TryChooseOwner(currentTime);
}

L2_DEFINE_OBJECT_CLASS_ID(CInstantAgit)

L2_DEFINE_OBJECT_CLASS_NAME(CInstantAgit, m_pClassName_CInstantAgit)

L2_DEFINE_OBJECT_ALLOCATOR(CInstantAgit, __GetClassName())

L2_DEFINE_OBJECT_NEW(CInstantAgit)

CInstantAgit::CInstantAgit(CInstantAgitData* data) :
    m_pNextInstantAgit(nullptr),
    m_pData(data),
    m_InstantZoneSId(-1),
    m_OwnerPledgeId(0),
    m_pDecoList(SharedInstantAgitDecoData::Create()),
    m_pResidenceDecoVisible(nullptr) {
    L2_GUARD_FUNCTION();

    m_pDecoList->Init(0);
    m_pResidenceDecoVisible = new CResidenceDecoVisible(static_cast<std::uint32_t>(data->m_Id), data->m_InstantAgitPos);

    CreateFailReason reason = CFR_INTERNAL_ERROR;
    CInstantZone* const zone = g_InstantZoneMgr.CreateInstantZone(data->m_InstantZoneTypeId, reason);
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CreateInstantZone() is Fail in CInstantAgit()")
        // L2_LOG_ERROR(Log,
        //     L"CreateInstantZone() is Fail in CInstantAgit()")
        L2_LOG_ERROR(
            Log,
            "Cannot create zone type %d for provisional clan hall %d: %s (reason %d); the hall object remains without a "
            "zone, SID -1.",
            data->m_InstantZoneTypeId, data->m_Id,
            reason == CFR_NPC_SERVER_DOWN        ? "NPC loading is not complete"
                : reason == CFR_LIMIT_ZONE_COUNT ? "zone count or container capacity is exhausted"
                : reason == CFR_INTERNAL_ERROR   ? "internal creation failure"
                                                 : "unrecognized creation failure",
            static_cast<int>(reason)
        );
        return;
    }

    m_InstantZoneSId = static_cast<int>(zone->Id());
    zone->EnterZone(*m_pResidenceDecoVisible);
}

CInstantAgit::~CInstantAgit() {
    L2_GUARD_FUNCTION();
    g_InstantZoneMgr.SetDeleteInzoneAgit(m_InstantZoneSId);
    m_InstantZoneSId = 0;
    m_pData = nullptr;
    m_pNextInstantAgit = nullptr;
    m_OwnerPledgeId = -1;
    m_pResidenceDecoVisible = nullptr;
    m_pDecoList->Init(0);
}

std::int32_t CInstantAgit::Release(const char* callerFile, int callerLine, ObjectRefType, bool) {
    return ReleaseInstantAgitObject(this, &m_nRefCount, __GetClassID(), "CInstantAgit", L2_SOURCE_FILE, __LINE__, callerFile, callerLine);
}

double CInstantAgit::GetHpBonus() {
    L2_GUARD_FUNCTION();
    return m_pDecoList->decoEffect.dHpBonus;
}

double CInstantAgit::GetMpBonus() {
    L2_GUARD_FUNCTION();
    return m_pDecoList->decoEffect.dMpBonus;
}

double CInstantAgit::GetExpRestore() {
    L2_GUARD_FUNCTION();
    return m_pDecoList->decoEffect.dExpRestore;
}

bool CInstantAgit::IsUsingNow(User& user) {
    L2_GUARD_FUNCTION();
    return user.GetInstantZoneIndex() == m_InstantZoneSId;
}

bool CInstantAgit::CanUse(User& user) {
    L2_GUARD_FUNCTION();
    return user.m_data->m_nPledgeID == static_cast<std::uint32_t>(m_OwnerPledgeId) && m_pData->CanUse();
}

CInstantZone* CInstantAgit::GetMyInstantZone() {
    L2_GUARD_FUNCTION();
    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(m_InstantZoneSId));
    CInstantZone* zone = nullptr;
    if (object != nullptr && object->IsInZone()) {
        zone = object->CastInZone();
    }
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Invalid InstantZone[%d] of InstantAgit[%d]",
        //     kInstantAgitSource,
        //     0x45b,
        //     m_InstantZoneSId,
        //     m_pData->m_Id)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] Invalid InstantZone[%d] of InstantAgit[%d]",
        //     kInstantAgitSource,
        //     0x45b,
        //     m_InstantZoneSId,
        //     m_pData->m_Id)
        L2_LOG_ERROR(
            Log,
            "Provisional clan hall %d, owner clan %d: zone SID %u is unavailable or is not an instance zone; no zone is "
            "returned.",
            m_pData != nullptr ? m_pData->m_Id : -1, m_OwnerPledgeId, static_cast<unsigned>(m_InstantZoneSId)
        );
    }
    return zone;
}

int CInstantAgit::GetInstantZoneTypeId() {
    L2_GUARD_FUNCTION();
    CInstantZone* const zone = GetMyInstantZone();
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Invalid InstantZone[%d] of InstantAgit[%d]",
        //     kInstantAgitSource,
        //     0x46b,
        //     m_InstantZoneSId,
        //     m_pData->m_Id)
        // Noise: GetMyInstantZone already reports this lookup failure. This wrapper only returns type ID 0.
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] Invalid InstantZone[%d] of InstantAgit[%d]",
        //     kInstantAgitSource,
        //     0x46b,
        //     m_InstantZoneSId,
        //     m_pData->m_Id)
        return 0;
    }
    return zone->GetInstantZoneTypeID();
}

bool CInstantAgit::TryEnter(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanUse(user)) {
        return false;
    }
    CInstantZone* const zone = GetMyInstantZone();
    if (zone == nullptr) {
        return false;
    }
    g_InstantZoneMgr.EnterInstantZone(
        &user, zone, IZET_INDIVIDUAL, 0, false, m_OwnerPledgeId, TeleportSourceType::InstantAgit
    );
    return true;
}

void CInstantAgit::TryLeave(User& user) {
    L2_GUARD_FUNCTION();
    if (CInstantZone* const zone = GetMyInstantZone()) {
        g_InstantZoneMgr.LeaveInstantZone(&user, zone, 0, 0, 0);
    }
}

bool CInstantAgit::OnSetAgitDecoNpcFromNPCServer(int slot, int npcSid) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return false;
    }

    L2_GUARD(m_Lock);
    if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Set Deco Npc is failed at %s : %d, %d",
        //     L"CInstantAgit::OnSetAgitDecoNpcFromNPCServer",
        //     m_OwnerPledgeId,
        //     slot)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Set Deco Npc is failed at %s : %d, %d",
        //     L"CInstantAgit::OnSetAgitDecoNpcFromNPCServer",
        //     m_OwnerPledgeId,
        //     slot)
        L2_LOG_ERROR(
            Log,
            "Cannot register decoration NPC SID %u for provisional clan hall %d, clan %d: slot %d is outside the configured "
            "range (slot count %d).",
            static_cast<unsigned>(npcSid), m_pData->m_Id, m_OwnerPledgeId, slot, m_pData->m_nDecoNpcSlotCount
        );
        return false;
    }

    auto& npc = m_pDecoList->aDecoNpc[slot];
    if (npc.nNpcSID != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Deco Npc is already created at %s : %d, %d",
        //     L"CInstantAgit::OnSetAgitDecoNpcFromNPCServer",
        //     m_OwnerPledgeId,
        //     slot)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Deco Npc is already created at %s : %d, %d",
        //     L"CInstantAgit::OnSetAgitDecoNpcFromNPCServer",
        //     m_OwnerPledgeId,
        //     slot)
        L2_LOG_ERROR(
            Log,
            "Provisional clan hall %d, clan %d, slot %d: cannot register decoration NPC SID %u; existing SID %u is "
            "retained.",
            m_pData->m_Id, m_OwnerPledgeId, slot, static_cast<unsigned>(npcSid), static_cast<unsigned>(npc.nNpcSID)
        );
        return false;
    }

    npc.nNpcSID = npcSid;
    return true;
}

void CInstantAgit::ResetDecoNpc(int slot) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }

    L2_GUARD(m_Lock);
    if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Deco slot is invalid at %s : %d, %d",
        //     L"CInstantAgit::ResetDecoNpc",
        //     m_OwnerPledgeId,
        //     slot)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Deco slot is invalid at %s : %d, %d",
        //     L"CInstantAgit::ResetDecoNpc",
        //     m_OwnerPledgeId,
        //     slot)
        L2_LOG_ERROR(
            Log,
            "Cannot reset decoration NPC SID for provisional clan hall %d, clan %d: slot %d is outside the configured range "
            "(slot count %d).",
            m_pData->m_Id, m_OwnerPledgeId, slot, m_pData->m_nDecoNpcSlotCount
        );
        return;
    }
    m_pDecoList->aDecoNpc[slot].nNpcSID = 0;
}

void CInstantAgit::SetDecoNpcExpireTime(int slot, int expireTime) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }

    L2_GUARD(m_Lock);
    if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
        return;
    }
    m_pDecoList->aDecoNpc[slot].tExpire = runtime::crt::Time32Now() + expireTime;
}

bool CInstantAgit::OnResetAgitDecoNpc(CSPointer<User> user, int slot, int& npcClassId, std::int32_t& expireTime) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return false;
    }

    int npcSid = 0;
    {
        L2_GUARD(m_Lock);
        if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
            //     L"CInstantAgit::OnResetAgitDecoNpc",
            //     m_pData->m_Id,
            //     slot,
            //     m_pData->m_nDecoNpcSlotCount)
            // L2_LOG_ERROR(Log,
            //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
            //     L"CInstantAgit::OnResetAgitDecoNpc",
            //     m_pData->m_Id,
            //     slot,
            //     m_pData->m_nDecoNpcSlotCount)
            L2_LOG_ERROR(
                Log,
                "Cannot remove decoration NPC for provisional clan hall %d, clan %d: slot %d is outside the configured "
                "range (slot count %d).",
                m_pData->m_Id, m_OwnerPledgeId, slot, m_pData->m_nDecoNpcSlotCount
            );
            return false;
        }

        auto& npc = m_pDecoList->aDecoNpc[slot];
        npcClassId = npc.nCID;
        npcSid = npc.nNpcSID;
        expireTime = npc.tExpire;
        npc.nCID = 0;
        npc.tExpire = 0;
        npc.nNpcSID = 0;

        if (user) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::EXPERT_REMOVAL_SUCCEEDED);
        }
        ResidenceUtil::SendDecoNpcSetForReset(user, 0, m_pData->m_Id, slot);
    }

    if (npcSid != 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
        if (npc != nullptr) {
            npc->Delete();
        }
    }
    return true;
}

void CInstantAgit::SendDecoNpcInfoAsMsg(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    char16_t message[1024]{};
    L2_GUARD(m_Lock);
    const std::int32_t currentTime = runtime::crt::Time32Now();
    for (int slot = 0; slot < m_pData->m_nDecoNpcSlotCount; ++slot) {
        const auto& npc = m_pDecoList->aDecoNpc[slot];
        const std::int32_t remaining = npc.nCID == 0 ? 0 : npc.tExpire - currentTime;
        FormatUtf16To(message, std::size(message), u"Deco(%d) : %d, %d, %d", slot, npc.nCID, remaining, npc.nNpcSID);
        socket->SendSystemMessage(u"SYS", message);
    }
}

void CInstantAgit::OnExpireAgitDecoNpc(const int* slots, const int* results, int count) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr || slots == nullptr || results == nullptr) {
        return;
    }

    std::vector<int> npcSids;
    {
        L2_GUARD(m_Lock);
        for (int index = 0; index < count; ++index) {
            const int slot = slots[index];
            if (results[index] != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[DECONPC]Expire Deco Npc is failed at %s : %d, %d",
                //     L"CInstantAgit::OnExpireAgitDecoNpc",
                //     m_OwnerPledgeId,
                //     slot)
                // L2_LOG_ERROR(Log,
                //     L"[DECONPC]Expire Deco Npc is failed at %s : %d, %d",
                //     L"CInstantAgit::OnExpireAgitDecoNpc",
                //     m_OwnerPledgeId,
                //     slot)
                L2_LOG_ERROR(
                    Log,
                    "Provisional clan hall %d, clan %d: decoration NPC expiration reply for slot %d failed with result %d; "
                    "this slot is retained and other replies are processed.",
                    m_pData->m_Id, m_OwnerPledgeId, slot, results[index]
                );
                continue;
            }
            if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
                //     L"CInstantAgit::OnExpireAgitDecoNpc",
                //     m_pData->m_Id,
                //     slot,
                //     m_pData->m_nDecoNpcSlotCount)
                // L2_LOG_ERROR(Log,
                //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
                //     L"CInstantAgit::OnExpireAgitDecoNpc",
                //     m_pData->m_Id,
                //     slot,
                //     m_pData->m_nDecoNpcSlotCount)
                L2_LOG_ERROR(
                    Log,
                    "Provisional clan hall %d, clan %d: expiration reply has invalid slot %d (slot count %d); processing "
                    "stops before deleting collected NPCs and earlier slot resets remain.",
                    m_pData->m_Id, m_OwnerPledgeId, slot, m_pData->m_nDecoNpcSlotCount
                );
                return;
            }

            auto& npc = m_pDecoList->aDecoNpc[slot];
            if (npc.nNpcSID != 0) {
                npcSids.push_back(npc.nNpcSID);
            }
            const FVector position{};
            ResidenceUtil::WriteDecoNpcSetLog(
                0x125, 0, 0, position, m_OwnerPledgeId, m_pData->m_Id, slot, npc.nCID, npc.tExpire,
                ResidenceUtil::DecoNpcSetReason::Expire
            );
            npc.nCID = 0;
            npc.tExpire = 0;
            npc.nNpcSID = 0;
        }
    }
    DeleteDecoNpcs(npcSids);
}

void CInstantAgit::SendDecoNpcInfo(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    L2_GUARD(m_Lock);
    protocol::S_EX_DECO_NPC_INFO packet{};
    packet.nAgitCID = static_cast<int>(user->m_data->m_nPledgeID);
    const std::int32_t currentTime = runtime::crt::Time32Now();
    for (int slot = 0; slot < m_pData->m_nDecoNpcSlotCount; ++slot) {
        const auto& npc = m_pDecoList->aDecoNpc[slot];
        protocol::DecoNpcInfo info{};
        info.cSlot = static_cast<char>(slot);
        info.nDecoCID = npc.nCID;
        info.nExpire = npc.nCID == 0 ? 0 : npc.tExpire - currentTime;
        packet.vDecoNpcs.push_back(info);
    }
    packet.nResidenceGrade = static_cast<char>(m_pData->m_nResidenceGrade);
    packet.vResidenceDomains.push_back(0);
    socket->Send(S_EX, S_EX_DECO_NPC_INFO, packet);
}

void CInstantAgit::ExpelNotPledgeMember(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanUse(user) || !user.IsPledgeMaster()) {
        return;
    }

    std::vector<User*> users;
    if (CInstantZone* const zone = GetMyInstantZone()) {
        zone->GetAllActiveUser(users);
    }
    for (User* const member : users) {
        if (member != nullptr && !CanUse(*member)) {
            TryLeave(*member);
        }
    }
}

void CInstantAgit::ExpelInstantAgit(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanUse(user) || !user.IsPledgeMaster()) {
        return;
    }

    std::vector<User*> users;
    if (CInstantZone* const zone = GetMyInstantZone()) {
        zone->GetAllActiveUser(users);
    }
    for (User* const member : users) {
        if (member != nullptr) {
            TryLeave(*member);
        }
    }
}

void CInstantAgit::SetOwner(int pledgeId) {
    L2_GUARD_FUNCTION();
    if (CInstantZone* const zone = GetMyInstantZone()) {
        zone->ExpelAllUser(true);
    }

    const int oldPledgeId = m_OwnerPledgeId;
    m_OwnerPledgeId = pledgeId;
    std::vector<int> npcSids;
    {
        L2_GUARD(m_Lock);
        if (m_pData != nullptr) {
            _WriteDecoNpcChangeOwnerLog(oldPledgeId, m_pData->m_Id, m_pDecoList);
        }
        _GetNeedToDeleteDecoNpcs(npcSids, m_pDecoList);
    }
    DeleteDecoNpcs(npcSids);
    m_pDecoList->Init(pledgeId);
}

bool CInstantAgit::ResetOwner(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanUse(user) || !user.IsPledgeMaster()) {
        return false;
    }

    if (CInstantZone* const zone = GetMyInstantZone()) {
        zone->ExpelAllUser(true);
    }
    const int oldPledgeId = m_OwnerPledgeId;
    m_OwnerPledgeId = -1;
    std::vector<int> npcSids;
    {
        L2_GUARD(m_Lock);
        if (m_pData != nullptr) {
            _WriteDecoNpcChangeOwnerLog(oldPledgeId, m_pData->m_Id, m_pDecoList);
        }
        _GetNeedToDeleteDecoNpcs(npcSids, m_pDecoList);
    }
    DeleteDecoNpcs(npcSids);
    m_pDecoList->Init(m_OwnerPledgeId);
    return true;
}

bool CInstantAgit::ResetOwner() {
    L2_GUARD_FUNCTION();
    if (CInstantZone* const zone = GetMyInstantZone()) {
        zone->ExpelAllUser(true);
    }
    const int oldPledgeId = m_OwnerPledgeId;
    m_OwnerPledgeId = -1;
    std::vector<int> npcSids;
    {
        L2_GUARD(m_Lock);
        if (m_pData != nullptr) {
            _WriteDecoNpcChangeOwnerLog(oldPledgeId, m_pData->m_Id, m_pDecoList);
        }
        _GetNeedToDeleteDecoNpcs(npcSids, m_pDecoList);
    }
    DeleteDecoNpcs(npcSids);
    m_pDecoList->Init(m_OwnerPledgeId);
    return true;
}

void CInstantAgit::DoDecoNpcCheck(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }

    {
        L2_GUARD(m_Lock);
        for (int slot = 0; slot < 5; ++slot) {
            auto& npc = m_pDecoList->aDecoNpc[slot];
            if (npc.nCID == 0 || (npc.tExpire != 0 && currentTime >= npc.tExpire) || npc.nNpcSID != 0) {
                continue;
            }

            const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(npc.nCID));
            if (data == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[DECONPC]Deco Npc Id is invalid at %s : %d, %d",
                //     L"CInstantAgit::DoDecoNpcCheck",
                //     m_OwnerPledgeId,
                //     npc.nCID)
                // L2_LOG_ERROR(Log,
                //     L"[DECONPC]Deco Npc Id is invalid at %s : %d, %d",
                //     L"CInstantAgit::DoDecoNpcCheck",
                //     m_OwnerPledgeId,
                //     npc.nCID)
                L2_LOG_ERROR(
                    Log,
                    "Provisional clan hall %d, clan %d, slot %d: decoration NPC definition %d was not found; clearing its "
                    "installed ID and expiration, then checking the next slot.",
                    m_pData->m_Id, m_OwnerPledgeId, slot, npc.nCID
                );
                npc.nCID = 0;
                npc.tExpire = 0;
                continue;
            }

            const char16_t* const npcName = g_ObjectDB.GetName(data->m_nNpcCID);
            if (npcName == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[DECONPC]No Npc Class name at %s: %d, %d",
                //     L"CInstantAgit::DoDecoNpcCheck",
                //     npc.nCID,
                //     data->m_nNpcCID)
                // L2_LOG_ERROR(Log,
                //     L"[DECONPC]No Npc Class name at %s: %d, %d",
                //     L"CInstantAgit::DoDecoNpcCheck",
                //     npc.nCID,
                //     data->m_nNpcCID)
                L2_LOG_ERROR(
                    Log,
                    "Provisional clan hall %d, clan %d, slot %d: decoration %d references NPC class %d without a name; the "
                    "remaining decoration NPC check is skipped.",
                    m_pData->m_Id, m_OwnerPledgeId, slot, npc.nCID, data->m_nNpcCID
                );
                return;
            }
            NpcServerUtil::SendRequestDecoNpcSpawn(
                npcName, m_pData->m_aDecoNpcSlotPositions[slot], m_InstantZoneSId, m_pData->m_aDecoNpcSlotYaws[slot], 2,
                m_OwnerPledgeId, slot
            );
        }
    }

    int slots[5]{};
    int count = 0;
    {
        L2_GUARD(m_Lock);
        for (int slot = 0; slot < 5; ++slot) {
            const auto& npc = m_pDecoList->aDecoNpc[slot];
            if (npc.nCID != 0 && npc.tExpire != 0 && npc.tExpire <= currentTime) {
                slots[count++] = slot;
            }
        }
        if (count > 0) {
            CExpireInstantAgitDecoNpc request(m_pData->m_Id, m_OwnerPledgeId, count, slots);
            db.RequestTPacketProcess(g_nWorldId, 0x41, static_cast<int>(sizeof(request)), request);
        }
    }
}

void CInstantAgit::_SetAgitDecoVisible(int depth, int visible) {
    L2_GUARD_FUNCTION();
    if (m_pResidenceDecoVisible != nullptr) {
        m_pResidenceDecoVisible->SetDecoDepth(depth, visible);
    }
}

void CInstantAgit::_ValidateAgitDecoEffect(CResidenceDeco& deco) {
    L2_GUARD_FUNCTION();
    if (m_pDecoList == nullptr) {
        return;
    }
    switch (deco.m_nFunction) {
        case 1:
            m_pDecoList->decoEffect.dHpBonus = deco.m_dArgument;
            break;
        case 2:
            m_pDecoList->decoEffect.dMpBonus = deco.m_dArgument;
            break;
        case 6:
            m_pDecoList->decoEffect.dExpRestore = deco.m_dArgument;
            break;
        default:
            break;
    }
}

bool CInstantAgit::_SetAgitDeco(int decoId) {
    L2_GUARD_FUNCTION();
    CResidenceDeco* const deco = CResidenceDecoDb::Inst().FindResidenceDeco(static_cast<std::uint32_t>(decoId));
    if (deco == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid agit deco id[%d] (instant agit id[%d] pledge[%d]",
        //     kInstantAgitSourceWide,
        //     0x3eb,
        //     decoId,
        //     m_pData->m_Id,
        //     m_OwnerPledgeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid agit deco id[%d] (instant agit id[%d] pledge[%d]",
        //     kInstantAgitSourceWide,
        //     0x3eb,
        //     decoId,
        //     m_pData->m_Id,
        //     m_OwnerPledgeId)
        L2_LOG_ERROR(
            Log, "Cannot apply decoration %d to provisional clan hall %d, clan %d: the decoration definition was not found.",
            decoId, m_pData != nullptr ? m_pData->m_Id : -1, m_OwnerPledgeId
        );
        return false;
    }

    auto& slot = m_pDecoList->decoInfo[deco->m_nType - 1];
    slot.agitDecoId = decoId;
    slot.agitDecoLevel = deco->m_nLevel;
    slot.agitDecoPower = deco->m_dArgument;
    _SetAgitDecoVisible(deco->m_nType - 1, deco->m_nDepth);
    _ValidateAgitDecoEffect(*deco);
    return true;
}

void CInstantAgit::TryAddAgitDeco(int npcIndex, int userIndex, int decoId) {
    L2_GUARD_FUNCTION();
    CResidenceDeco* const deco = CResidenceDecoDb::Inst().FindResidenceDeco(static_cast<std::uint32_t>(decoId));
    if (deco == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid agit deco id[%d] (instant agit id[%d] pledge[%d]",
        //     kInstantAgitSourceWide,
        //     0x412,
        //     decoId,
        //     m_pData->m_Id,
        //     m_OwnerPledgeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid agit deco id[%d] (instant agit id[%d] pledge[%d]",
        //     kInstantAgitSourceWide,
        //     0x412,
        //     decoId,
        //     m_pData->m_Id,
        //     m_OwnerPledgeId)
        L2_LOG_ERROR(
            Log,
            "Cannot request decoration %d for provisional clan hall %d, clan %d: the decoration definition was not found; "
            "no installation request is sent.",
            decoId, m_pData != nullptr ? m_pData->m_Id : -1, m_OwnerPledgeId
        );
        return;
    }

    L2_GUARD(m_Lock);
    const int currentDecoId = m_pDecoList->decoInfo[deco->m_nType - 1].agitDecoId;
    CAddInstantAgitDeco request{};
    request.npcIndex = npcIndex;
    request.userIndex = userIndex;
    request.instantAgitId = m_pData->m_Id;
    request.pledgeDBId = m_OwnerPledgeId;
    request.agitDecoId = decoId;
    request.removeAgitDecoId = currentDecoId > 0 ? currentDecoId : -1;
    request.adenaAmount = static_cast<int>(deco->GetCost());
    request.nReturn = 0;
    db.RequestTPacketProcess(g_nWorldId, 0x36, static_cast<int>(sizeof(request)), request);
}

void CInstantAgit::OnAddAgitDeco(int decoId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    _SetAgitDeco(decoId);
}

bool CInstantAgit::_HasDecoNpc(int subType, int factionType) const {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return false;
    }
    for (int slot = 0; slot < m_pData->m_nDecoNpcSlotCount; ++slot) {
        const int cid = m_pDecoList->aDecoNpc[slot].nCID;
        if (cid == 0) {
            continue;
        }
        const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(cid));
        if (data != nullptr && data->m_nFactionType == factionType && data->m_nSubType == subType) {
            return true;
        }
    }
    return false;
}

void CInstantAgit::TryAddAgitDecoNpc(CSPointer<User> user, int npcClassId, int slot) {
    L2_GUARD_FUNCTION();
    if (!user || m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }
    if (!user->HavePledgePower(PP_CONTROL_AGIT_FUNC)) {
        socket->SendSystemMessage(u"SYS", u"no right");
        return;
    }

    L2_GUARD(m_Lock);
    if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
        socket->SendSystemMessage(u"SYS", u"invalid agit slot");
        return;
    }
    if (m_pDecoList->aDecoNpc[slot].nCID != 0) {
        socket->SendSystemMessage(SystemMessageId::REMOVE_EXISTING_EXPERT_BEFORE_PLACEMENT);
        return;
    }

    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(npcClassId));
    if (data == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid deco id");
        return;
    }
    if (data->m_nPeriod < 1) {
        socket->SendSystemMessage(u"SYS", u"invalid period");
        return;
    }
    if (!data->HasResidenceGrade(m_pData->m_nResidenceGrade)) {
        socket->SendSystemMessage(u"SYS", u"invalid grade");
        return;
    }
    if (!data->HasResidenceDomain(0)) {
        socket->SendSystemMessage(u"SYS", u"invalid domain");
        return;
    }
    if (!DecoNpcUtil::IsRightSlot(data, slot)) {
        socket->SendSystemMessage(u"SYS", u"invalid slot for a faction delegation");
        return;
    }
    if (_HasDecoNpc(data->m_nSubType, data->m_nFactionType)) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_PLACE_SAME_CLASS_EXPERTS_OF_SAME_FACTION_TOGETHER);
        return;
    }

    const std::int32_t currentTime = runtime::crt::Time32Now();
    std::tm* const localTime = runtime::crt::LocalTime32(currentTime);
    if (localTime == nullptr) {
        return;
    }
    const std::int32_t expireTime =
        currentTime + (((data->m_nPeriod * 1440 - localTime->tm_min) + 60) * 60 - localTime->tm_sec);
    const FVector position = user->GetPos();
    CSetInstantAgitDecoNpc request(
        static_cast<int>(user->m_nId), static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), position,
        static_cast<int>(user->m_data->m_nPledgeID), m_pData->m_Id, slot, npcClassId, expireTime,
        static_cast<int>(data->m_nCostAdena), data->GetFirstCostItemCID(),
        static_cast<std::int64_t>(data->GetFirstCostItemCount()), 0
    );
    db.RequestTPacketProcess(g_nWorldId, 0x3f, static_cast<int>(sizeof(request)), request);
}

bool CInstantAgit::OnAddAgitDecoNpc(CSPointer<User> user, int npcClassId, int slot, std::int32_t expireTime) {
    L2_GUARD_FUNCTION();
    if (m_pData == nullptr || m_pDecoList == nullptr) {
        return false;
    }

    L2_GUARD(m_Lock);
    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(npcClassId));
    if (data == nullptr) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Deco Npc Data is not exist at %s : %d",
        //     L"CInstantAgit::OnAddAgitDecoNpc",
        //     npcClassId)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Deco Npc Data is not exist at %s : %d",
        //     L"CInstantAgit::OnAddAgitDecoNpc",
        //     npcClassId)
        L2_LOG_ERROR(
            Log,
            "Cannot add decoration NPC %d to provisional clan hall %d, clan %d, slot %d: the decoration definition was not "
            "found.",
            npcClassId, m_pData->m_Id, m_OwnerPledgeId, slot
        );
        return false;
    }

    const char16_t* const npcName = g_ObjectDB.GetName(data->m_nNpcCID);
    if (npcName == nullptr) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]No Npc Class name at %s : %d, %d",
        //     L"CInstantAgit::OnAddAgitDecoNpc",
        //     npcClassId,
        //     data->m_nNpcCID)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]No Npc Class name at %s : %d, %d",
        //     L"CInstantAgit::OnAddAgitDecoNpc",
        //     npcClassId,
        //     data->m_nNpcCID)
        L2_LOG_ERROR(
            Log,
            "Cannot add decoration NPC %d to provisional clan hall %d, clan %d, slot %d: NPC class %d has no name; "
            "installation is skipped.",
            npcClassId, m_pData->m_Id, m_OwnerPledgeId, slot, data->m_nNpcCID
        );
        return false;
    }

    if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
        //     L"CInstantAgit::OnAddAgitDecoNpc",
        //     m_pData->m_Id,
        //     slot,
        //     m_pData->m_nDecoNpcSlotCount)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
        //     L"CInstantAgit::OnAddAgitDecoNpc",
        //     m_pData->m_Id,
        //     slot,
        //     m_pData->m_nDecoNpcSlotCount)
        L2_LOG_ERROR(
            Log,
            "Cannot add decoration NPC %d to provisional clan hall %d, clan %d: slot %d is outside the configured range "
            "(slot count %d).",
            npcClassId, m_pData->m_Id, m_OwnerPledgeId, slot, m_pData->m_nDecoNpcSlotCount
        );
        return false;
    }

    auto& npc = m_pDecoList->aDecoNpc[slot];
    npc.nCID = npcClassId;
    npc.tExpire = expireTime;
    npc.nNpcSID = 0;
    NpcServerUtil::SendRequestDecoNpcSpawn(
        npcName, m_pData->m_aDecoNpcSlotPositions[slot], m_InstantZoneSId, m_pData->m_aDecoNpcSlotYaws[slot], 2,
        m_OwnerPledgeId, slot
    );
    if (user && user->GetSocket() != nullptr) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::EXPERT_PLACEMENT_SUCCEEDED);
    }
    ResidenceUtil::SendDecoNpcSet(user, 0, m_pData->m_Id, slot, npcClassId, expireTime);
    return true;
}

void CInstantAgit::TryResetAgitDecoNpc(CSPointer<User> user, int slot) {
    L2_GUARD_FUNCTION();
    if (!user || m_pData == nullptr || m_pDecoList == nullptr) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }
    if (!user->HavePledgePower(PP_CONTROL_AGIT_FUNC)) {
        socket->SendSystemMessage(u"SYS", u"no right");
        return;
    }

    L2_GUARD(m_Lock);
    if (slot < 0 || slot >= m_pData->m_nDecoNpcSlotCount) {
        socket->SendSystemMessage(u"SYS", u"invalid agit slot");
        return;
    }
    const int npcClassId = m_pDecoList->aDecoNpc[slot].nCID;
    if (npcClassId == 0) {
        socket->SendSystemMessage(u"SYS", u"no deco at agit slot");
        return;
    }
    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(npcClassId));
    if (data == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid deco id of placed deco npc");
        return;
    }
    if (!data->m_isChangeable) {
        socket->SendSystemMessage(u"SYS", u"cannot remove an unchangagle deco npc");
        return;
    }

    const FVector position = user->GetPos();
    CResetInstantAgitDecoNpc request(
        static_cast<int>(user->m_nId), static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), position,
        m_pData->m_Id, m_OwnerPledgeId, slot
    );
    db.RequestTPacketProcess(g_nWorldId, 0x40, static_cast<int>(sizeof(request)), request);
}

L2_DEFINE_OBJECT_CLASS_ID(CInstantAgitManager)

L2_DEFINE_OBJECT_CLASS_NAME(CInstantAgitManager, m_pClassName_CInstantAgitManager)

L2_DEFINE_OBJECT_ALLOCATOR(CInstantAgitManager, __GetClassName())

L2_DEFINE_OBJECT_NEW(CInstantAgitManager)

CInstantAgitManager::CInstantAgitManager() :
    m_LotteryCountToLoad(-1),
    m_LotteryCountLoaded(0),
    m_AgitCountToLoad(-1),
    m_AgitCountLoaded(0),
    m_ApplicantCountToLoad(-1),
    m_ApplicantCountLoaded(0),
    m_AgitManagerIndex(0),
    m_paddingac{} {
    L2_GUARD_FUNCTION();
    m_InstantAgitDataMap.clear();
    m_PledgeInstantAgitMap.clear();
    m_ApplicantSet.clear();
}

CInstantAgitManager::~CInstantAgitManager() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_InstantAgitDataMap) {
        if (entry.second != nullptr) {
            entry.second->Delete();
        }
    }
    m_InstantAgitDataMap.clear();
    m_PledgeInstantAgitMap.clear();
    m_ApplicantSet.clear();
}

std::int32_t CInstantAgitManager::Release(const char* callerFile, int callerLine, ObjectRefType, bool) {
    return ReleaseInstantAgitObject(this, &m_nRefCount, __GetClassID(), "CInstantAgitManager", L2_SOURCE_FILE, __LINE__, callerFile, callerLine);
}

void CInstantAgitManager::Load() {
    L2_GUARD_FUNCTION();
    db.RequestLoadInstantAgit();
    AddTimer(5000, 2);
}

void CInstantAgitManager::OnLoadSummary(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int lotteryCount = 0;
    int agitCount = 0;
    int applicantCount = 0;
    ::Disassemble(packet, "ddd", &lotteryCount, &agitCount, &applicantCount);

    runtime::raw_atomic::ExchangeI32(&m_LotteryCountToLoad, lotteryCount);
    runtime::raw_atomic::ExchangeI32(&m_AgitCountToLoad, agitCount);
    runtime::raw_atomic::ExchangeI32(&m_ApplicantCountToLoad, applicantCount);
}

void CInstantAgitManager::EnterAgitManager(int pledgeId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_AgitManagerIndex = pledgeId;
}

void CInstantAgitManager::CheckLoadingCompleted() {
    L2_GUARD_FUNCTION();
    if (m_LotteryCountToLoad == m_LotteryCountLoaded && m_AgitCountLoaded == m_AgitCountToLoad &&
        m_ApplicantCountLoaded == m_ApplicantCountToLoad && m_AgitCountToLoad >= 0) {
        db.RequestLoadInstantAgitDeco();
        db.RequestLoadInstantAgitDecoNpc();
        AddTimer(InstantAgitConst::_TIMER_PERIOD, 1);
    } else {
        AddTimer(5000, 2);
    }
}

void CInstantAgitManager::PostLoad() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const auto& entry : m_InstantAgitDataMap) {
        if (entry.second != nullptr) {
            entry.second->PostLoad();
        }
    }
}

void CInstantAgitManager::SendApplyResponse(
    int userSid,
    int userIndex,
    InstantAgitApplyResponse::Code response,
    int instantZoneTypeId,
    int agitId,
    std::int64_t rentalFee
) {
    L2_GUARD_FUNCTION();

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        User* const user = creature->CastUser();
        const int pledgeId = static_cast<int>(user->m_data->m_nPledgeID);
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
        const char16_t* pledgeName = u"";
        if (pledge) {
            pledgeName = pledge->GetPledgeName();
        }

        int logId = 0x804;
        std::int64_t logValue = static_cast<int>(response);
        if (response == InstantAgitApplyResponse::SUCCESS) {
            if (CUserSocket* const socket = user->GetSocket()) {
                socket->Send(
                    "chcch", 0x62, SystemMessageId::BID_PLACED_ON_NAMED_AUCTION, 1, 10,
                    static_cast<int>(static_cast<std::int16_t>(instantZoneTypeId))
                );
            }
            logId = 0x7f3;
            logValue = rentalFee;
        } else {
            static constexpr std::array<unsigned int, 7> kFailureMessages{
                0xd8e,
                0xd8d,
                0xd8f,
                0xd8b,
                0xd90,
                0x3ed,
                0xda9,
            };
            const int failureIndex = static_cast<int>(response) - 1;
            if (failureIndex >= 0 && failureIndex < static_cast<int>(kFailureMessages.size())) {
                if (CUserSocket* const socket = user->GetSocket()) {
                    socket->SendSystemMessage(kFailureMessages[failureIndex]);
                }
            }
        }

        CLogFormatter formatter;
        formatter.WriteLog4(
            logId, user, nullptr, pledgeName, u"", u"", pledgeId, agitId, instantZoneTypeId, logValue, 0, 0, 0, 0, 0, 0, 0
        );
    }

    g_npcserver.Send("ccddd", 0xaf, 0, userSid, userIndex, static_cast<int>(response));
}

void CInstantAgitManager::MakerFinished() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_InstantAgitDataMap) {
        if (entry.second != nullptr) {
            entry.second->InitNPC();
        }
    }
}

CInstantAgitData* CInstantAgitManager::GetInstantAgitData(int agitId) {
    L2_GUARD_FUNCTION();
    const auto found = m_InstantAgitDataMap.find(agitId);
    return found != m_InstantAgitDataMap.end() ? found->second : nullptr;
}

CInstantAgit* CInstantAgitManager::GetInstantAgit(int pledgeId) {
    L2_GUARD_FUNCTION();
    const auto found = m_PledgeInstantAgitMap.find(pledgeId);
    return found != m_PledgeInstantAgitMap.end() ? found->second : nullptr;
}

CInstantAgit* CInstantAgitManager::GetInstantAgit(User& user) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user.GetPledge();
    return pledge != nullptr ? GetInstantAgit(static_cast<int>(pledge->GetDbId())) : nullptr;
}

bool CInstantAgitManager::_IsAlreadyAppliedPledge(int pledgeId) {
    L2_GUARD_FUNCTION();
    return m_ApplicantSet.find(pledgeId) != m_ApplicantSet.end();
}

bool CInstantAgitManager::IsAlreadyAppliedPledge(int pledgeId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return _IsAlreadyAppliedPledge(pledgeId);
}

void CInstantAgitManager::OnLoadLotteryDate(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        int agitId = 0;
        std::int32_t lotteryDate = 0;
        packet = ::Disassemble(packet, "dd", &agitId, &lotteryDate);
        if (CInstantAgitData* const data = GetInstantAgitData(agitId)) {
            data->SetNextLotteryDate(lotteryDate);
        }
    }
    runtime::raw_atomic::AddI32(&m_LotteryCountLoaded, 1);
}

bool CInstantAgitManager::SetRentalPeriod(int agitId, int rentalPeriod) {
    L2_GUARD_FUNCTION();
    CInstantAgitData* const data = GetInstantAgitData(agitId);
    if (data != nullptr) {
        data->m_RetalPeriod = rentalPeriod;
    }
    return data != nullptr;
}

bool CInstantAgitManager::SetApplyPeriod(int agitId, int applyPeriod) {
    L2_GUARD_FUNCTION();
    CInstantAgitData* const data = GetInstantAgitData(agitId);
    if (data != nullptr) {
        data->m_ApplyPeriod = applyPeriod;
    }
    return data != nullptr;
}

bool CInstantAgitManager::OnSetDecoNpcFromNPCServer(int pledgeId, int slot, int npcSid) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(pledgeId);
    return agit != nullptr && agit->OnSetAgitDecoNpcFromNPCServer(slot, npcSid);
}

void CInstantAgitManager::ResetDecoNpc(int pledgeId, int slot) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(pledgeId)) {
        agit->ResetDecoNpc(slot);
    }
}

bool CInstantAgitManager::OnResetInstantAgitDecoNpc(
    CSPointer<User> user, int pledgeId, int slot, int& npcClassId, std::int32_t& expireTime
) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(pledgeId);
    return agit != nullptr && agit->OnResetAgitDecoNpc(user, slot, npcClassId, expireTime);
}

void CInstantAgitManager::SetDecoNpcExpireTime(int pledgeId, int slot, int expireTime) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(pledgeId)) {
        agit->SetDecoNpcExpireTime(slot, expireTime);
    }
}

void CInstantAgitManager::OnExpireInstantAgitDecoNpc(int pledgeId, const int* slots, const int* results, int count) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(pledgeId)) {
        agit->OnExpireAgitDecoNpc(slots, results, count);
    }
}

void CInstantAgitManager::OnLoadApplicant(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int agitId = 0;
    packet = ::Disassemble(packet, "d", &agitId);
    L2_GUARD(m_Lock);
    CInstantAgitData* const data = GetInstantAgitData(agitId);
    if (data != nullptr) {
        int count = 0;
        packet = ::Disassemble(packet, "d", &count);
        for (int index = 0; index < count; ++index) {
            int pledgeId = 0;
            packet = ::Disassemble(packet, "d", &pledgeId);
            _AddApplicant(*data, pledgeId);
        }
    }
    runtime::raw_atomic::AddI32(&m_ApplicantCountLoaded, 1);
}

void CInstantAgitManager::OnLoadDeco(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int pledgeId = 0;
    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &pledgeId, &count);
    CInstantAgit* const agit = GetInstantAgit(pledgeId);
    if (agit == nullptr || count <= 0) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        int decoId = 0;
        cursor = ::Disassemble(cursor, "d", &decoId);
        agit->OnAddAgitDeco(decoId);
    }
}

void CInstantAgitManager::OnLoadDecoNpc(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int pledgeId = 0;
    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &pledgeId, &count);
    CInstantAgit* const agit = GetInstantAgit(pledgeId);
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Instant Agit is not exist at %s : %d",
        //     L"CInstantAgitManager::OnLoadDecoNpc",
        //     pledgeId)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Instant Agit is not exist at %s : %d",
        //     L"CInstantAgitManager::OnLoadDecoNpc",
        //     pledgeId)
        L2_LOG_ERROR(
            Log,
            "Cannot restore decoration NPCs for clan %d: no provisional clan hall is registered; the reply body is not "
            "processed (declared entries %d).",
            pledgeId, count
        );
        return;
    }
    for (int index = 0; index < count; ++index) {
        int slot = 0;
        int npcClassId = 0;
        std::int32_t expireTime = 0;
        cursor = ::Disassemble(cursor, "ddd", &slot, &npcClassId, &expireTime);
        CSPointer<User> noUser;
        agit->OnAddAgitDecoNpc(noUser, npcClassId, slot, expireTime);
    }
}

void CInstantAgitManager::OnLoadInstantAgit(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int agitId = 0;
    packet = ::Disassemble(packet, "d", &agitId);
    L2_GUARD(m_Lock);
    if (CInstantAgitData* const data = GetInstantAgitData(agitId)) {
        data->OnChooseOwner(packet);
    }
    runtime::raw_atomic::AddI32(&m_AgitCountLoaded, 1);
}

void CInstantAgitManager::OnChooseOwner(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int agitId = 0;
    packet = ::Disassemble(packet, "d", &agitId);
    if (CInstantAgitData* const data = GetInstantAgitData(agitId)) {
        L2_GUARD(m_Lock);
        data->OnChooseOwner(packet);
    }
}

bool CInstantAgitManager::AddInstantAgit(CInstantAgitData* data) {
    L2_GUARD_FUNCTION();
    if (data == nullptr) {
        return false;
    }

    const auto inserted = m_InstantAgitDataMap.emplace(data->m_Id, data);
    if (!inserted.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated instant agit id[%d]",
        //     kInstantAgitSourceWide,
        //     0x868,
        //     data->m_Id)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] duplicated instant agit id[%d]",
        //     kInstantAgitSourceWide,
        //     0x868,
        //     data->m_Id)
        L2_LOG_ERROR(
            Log, "Cannot register duplicate provisional clan hall ID %d; the existing definition is retained.", data->m_Id
        );
        return false;
    }

    CInstantZone* const zone = g_InstantZoneMgr.GetInstantZoneTemplate(data->m_InstantZoneTypeId);
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid instant zone type id[%d] while loading InstantAgit[%d]",
        //     kInstantAgitSourceWide,
        //     0x862,
        //     data->m_InstantZoneTypeId,
        //     data->m_Id)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid instant zone type id[%d] while loading InstantAgit[%d]",
        //     kInstantAgitSourceWide,
        //     0x862,
        //     data->m_InstantZoneTypeId,
        //     data->m_Id)
        L2_LOG_ERROR(
            Log,
            "Provisional clan hall %d references missing zone template %d; the inserted definition remains registered, but "
            "instance initialization is skipped.",
            data->m_Id, data->m_InstantZoneTypeId
        );
        return false;
    }

    FVector start = zone->m_const->GetStartPos(0);
    data->Init(zone->MAX_ZONE_COUNT, start);
    return true;
}

void CInstantAgitManager::_DeleteApplicant(int pledgeId) {
    L2_GUARD_FUNCTION();
    m_ApplicantSet.erase(pledgeId);
}

void CInstantAgitManager::_AddApplicant(CInstantAgitData& data, int pledgeId) {
    L2_GUARD_FUNCTION();
    const auto inserted = m_ApplicantSet.insert(pledgeId);
    if (inserted.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[NO_ERROR][InstantAgitApply] CInstantAgitManager Insert to Set! "
        //     "PledgeDBID[%d] InstantAgitId[%d]",
        //     pledgeId,
        //     data.m_Id)
        // Noise: Routine manager-set insertion is followed by the data-owner insertion. Neither warrants an operator record.
        // L2_LOG_ERROR(Log,
        //     "[NO_ERROR][InstantAgitApply] CInstantAgitManager Insert to Set! "
        //     "PledgeDBID[%d] InstantAgitId[%d]",
        //     pledgeId,
        //     data.m_Id)
        data.OnApply(pledgeId);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] duplicated applicant[%d] to instant agit[%d]",
    //     kInstantAgitSourceWide,
    //     0x7de,
    //     pledgeId,
    //     data.m_Id)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] duplicated applicant[%d] to instant agit[%d]",
    //     kInstantAgitSourceWide,
    //     0x7de,
    //     pledgeId,
    //     data.m_Id)
    L2_LOG_WARN(
        Log, "Clan %d already has a provisional clan-hall application; duplicate application for hall %d is ignored.",
        pledgeId, data.m_Id
    );
}

void CInstantAgitManager::AddApplicant(CInstantAgitData& data, int pledgeId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    _AddApplicant(data, pledgeId);
}

void CInstantAgitManager::OnApplyInstantAgit(int agitId, int pledgeId) {
    L2_GUARD_FUNCTION();
    if (CInstantAgitData* const data = GetInstantAgitData(agitId)) {
        AddApplicant(*data, pledgeId);
    }
}

bool CInstantAgitManager::TryEnterInstantAgit(User& user) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(user);
    return agit != nullptr && agit->TryEnter(user);
}

bool CInstantAgitManager::TryLeaveInstantAgit(User& user) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(user);
    if (agit != nullptr) {
        agit->TryLeave(user);
    }
    return agit != nullptr;
}

bool CInstantAgitManager::_AddPledgeInstantAgit(int pledgeId, CInstantAgit* agit) {
    L2_GUARD_FUNCTION();
    const auto inserted = m_PledgeInstantAgitMap.emplace(pledgeId, agit);
    if (inserted.second) {
        return true;
    }
    const int ownedAgitId = inserted.first->second != nullptr && inserted.first->second->m_pData != nullptr
        ? inserted.first->second->m_pData->m_Id
        : -1;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] pledge[%d] try to acquire a duplicated instant "
    //     L"agit[%d]. owned instant agit[%d]",
    //     kInstantAgitSourceWide,
    //     0x911,
    //     pledgeId,
    //     agit != nullptr && agit->m_pData != nullptr
    //         ? agit->m_pData->m_Id
    //         : -1,
    //     ownedAgitId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] pledge[%d] try to acquire a duplicated instant "
    //     L"agit[%d]. owned instant agit[%d]",
    //     kInstantAgitSourceWide,
    //     0x911,
    //     pledgeId,
    //     agit != nullptr && agit->m_pData != nullptr
    //         ? agit->m_pData->m_Id
    //         : -1,
    //     ownedAgitId)
    L2_LOG_ERROR(
        Log, "Cannot assign provisional clan hall %d to clan %d: its existing hall registration %d is retained.",
        agit != nullptr && agit->m_pData != nullptr ? agit->m_pData->m_Id : -1, pledgeId, ownedAgitId
    );
    return false;
}

void CInstantAgitManager::ResetInstantAgit(int pledgeId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_PledgeInstantAgitMap.erase(pledgeId);
}

void CInstantAgitManager::ExpelNotPledgeMember(User& user) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(user)) {
        agit->ExpelNotPledgeMember(user);
    }
}

void CInstantAgitManager::ExpelInstantAgit(User& user) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(user)) {
        agit->ExpelInstantAgit(user);
    }
}

CInstantAgit* CInstantAgitManager::GetUsingInstantAgit(User& user) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(user);
    return agit != nullptr && agit->IsUsingNow(user) ? agit : nullptr;
}

bool CInstantAgitManager::TryReturnInstantAgit(int userSid, User& user) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(user);
    if (agit == nullptr || !agit->ResetOwner(user)) {
        return false;
    }

    const int pledgeId = static_cast<int>(user.m_data->m_nPledgeID);
    m_PledgeInstantAgitMap.erase(pledgeId);
    if (CPledge* const pledge = user.GetPledge()) {
        pledge->UpdateInfoWindowPacket();
        const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x7f6, pledgeId, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), u"", u"", pledge->GetPledgeLevel(),
            pledge->GetDbMemberCount(), agit->m_pData->m_Id, agit->GetInstantZoneTypeId(), 0, 0, 0, 0, 0, 0, u"", u"", u"",
            u"", 0
        );
    }

    CReturnInstantAgit request{};
    request.npcIndex = userSid;
    request.userIndex = user.GetIndex();
    request.pledgeDBId = pledgeId;
    request.nReturn = 0;
    db.RequestTPacketProcess(g_nWorldId, 0x37, static_cast<int>(sizeof(request)), request);
    return true;
}

bool CInstantAgitManager::TryReturnInstantAgit(int pledgeId) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(pledgeId);
    if (agit == nullptr) {
        return false;
    }

    if (agit->ResetOwner()) {
        CInstantAgitManager::inst().ResetInstantAgit(pledgeId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
        if (pledge) {
            pledge->UpdateInfoWindowPacket();
            const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x7f6, pledgeId, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), u"", u"", pledge->GetPledgeLevel(),
                pledge->GetDbMemberCount(), agit->m_pData->m_Id, agit->GetInstantZoneTypeId(), 0, 0, 0, 0, 0, 0, u"", u"",
                u"", u"", 0
            );
        }
    }

    CReturnInstantAgit request{};
    request.npcIndex = -1;
    request.userIndex = -1;
    request.pledgeDBId = pledgeId;
    request.nReturn = 0;
    db.RequestTPacketProcess(g_nWorldId, 0x37, static_cast<int>(sizeof(request)), request);
    return true;
}

bool CInstantAgitManager::ResetLotteryDate(int agitId, int lotteryDate) {
    L2_GUARD_FUNCTION();
    CInstantAgitData* const data = CInstantAgitManager::inst().GetInstantAgitData(agitId);
    if (data != nullptr) {
        data->SetNextLotteryDate(runtime::crt::Time32Now() + lotteryDate);
    }
    return data != nullptr;
}

bool CInstantAgitManager::SetAgitOwner(int agitId, CPledge* pledge) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return false;
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble("d", pledge->GetDbId());
    CInstantAgitData* const data = CInstantAgitManager::inst().GetInstantAgitData(agitId);
    if (data != nullptr) {
        data->SetChooseOwner(runtime::crt::Time32Now(), packet);
    }
    return data != nullptr;
}

bool CInstantAgitManager::TryApplyInstantAgit(int userSid, User& user, int agitId, bool builder) {
    L2_GUARD_FUNCTION();
    CInstantAgitData* const data = GetInstantAgitData(agitId);
    const int pledgeId = static_cast<int>(user.m_data->m_nPledgeID);
    if (data != nullptr && !IsAlreadyAppliedPledge(pledgeId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[NO_ERROR][InstantAgitApply] Request! "
        //     "UserName[%d] InstantAgitId[%d]",
        //     user.GetName(),
        //     agitId)
        // Noise: This request-entry trace precedes validation and says nothing about the outcome. The application/reply flow
        // already reports results. L2_LOG_ERROR(Log,
        //     "[NO_ERROR][InstantAgitApply] Request! "
        //     "UserName[%d] InstantAgitId[%d]",
        //     user.GetName(),
        //     agitId)
        return data->TryApply(userSid, user, builder);
    }

    const char16_t* pledgeName = u"";
    if (CPledge* const pledge = user.GetPledge()) {
        pledgeName = pledge->GetPledgeName();
    }
    const int instantZoneTypeId = data != nullptr ? data->m_InstantZoneTypeId : 0;
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x804, &user, nullptr, pledgeName, u"", u"", pledgeId, agitId, instantZoneTypeId, 0, 0, 0, 0, 0, 0, 0, 0
    );
    if (CUserSocket* const socket = user.GetSocket()) {
        socket->SendSystemMessage(SystemMessageId::PROVISIONAL_CLAN_HALL_BID_ALREADY_PLACED);
    }
    return false;
}

void CInstantAgitManager::TryAddInstantAgitDeco(int npcIndex, User& user, int decoId) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(user)) {
        agit->TryAddAgitDeco(npcIndex, user.GetIndex(), decoId);
    }
}

void CInstantAgitManager::TryAddInstantAgitDeco(int npcIndex, int pledgeId, int decoId) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(pledgeId)) {
        agit->TryAddAgitDeco(npcIndex, 0, decoId);
    }
}

void CInstantAgitManager::OnAddInstantAgitDeco(int pledgeId, int decoId) {
    L2_GUARD_FUNCTION();
    if (CInstantAgit* const agit = GetInstantAgit(pledgeId)) {
        agit->OnAddAgitDeco(decoId);
    }
}

void CInstantAgitManager::NpcPacketHandler(char opcode, int userSid, User& user, int value) {
    L2_GUARD_FUNCTION();
    switch (opcode) {
        case 0:
            TryApplyInstantAgit(userSid, user, value, false);
            break;
        case 1:
            TryAddInstantAgitDeco(userSid, user, value);
            break;
        case 2:
            TryReturnInstantAgit(userSid, user);
            break;
        case 3:
            TryEnterInstantAgit(user);
            break;
        case 4:
            TryLeaveInstantAgit(user);
            break;
        case 5:
            ExpelNotPledgeMember(user);
            break;
        default:
            break;
    }
}

bool CInstantAgitManager::DBPacketHandler(CDBSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    (void)socket;
    char opcode = 0;
    const unsigned char* const payload = ::Disassemble(packet, "c", &opcode);
    CInstantAgitManager& manager = CInstantAgitManager::inst();
    switch (opcode) {
        case 6:
            manager.OnChooseOwner(payload);
            break;
        case 7:
            manager.OnLoadSummary(payload);
            break;
        case 8:
            manager.OnLoadLotteryDate(payload);
            break;
        case 9:
            manager.OnLoadInstantAgit(payload);
            break;
        case 10:
            manager.OnLoadApplicant(payload);
            break;
        case 11:
            manager.OnLoadDeco(payload);
            break;
        case 13:
            manager.OnLoadDecoNpc(payload);
            break;
        default:
            break;
    }
    return false;
}

void CInstantAgitManager::TryAddInstantAgitDecoNpc(CSPointer<User> user, int npcClassId, int slot) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CInstantAgit* const agit = GetInstantAgit(static_cast<int>(user->m_data->m_nPledgeID));
    if (agit != nullptr) {
        agit->TryAddAgitDecoNpc(user, npcClassId, slot);
    }
}

bool CInstantAgitManager::OnAddInstantAgitDecoNpc(
    CSPointer<User> user, int pledgeId, int npcClassId, int slot, std::int32_t expireTime
) {
    L2_GUARD_FUNCTION();
    CInstantAgit* const agit = GetInstantAgit(pledgeId);
    return agit != nullptr && agit->OnAddAgitDecoNpc(user, npcClassId, slot, expireTime);
}

void CInstantAgitManager::TryRemoveInstantAgitDecoNpc(CSPointer<User> user, int slot) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CInstantAgit* const agit = GetInstantAgit(static_cast<int>(user->m_data->m_nPledgeID));
    if (agit != nullptr) {
        agit->TryResetAgitDecoNpc(user, slot);
    }
}

void CInstantAgitManager::CheckLettery() {
    L2_GUARD_FUNCTION();
    const std::int32_t currentTime = runtime::crt::Time32Now();
    for (const auto& entry : m_InstantAgitDataMap) {
        if (entry.second != nullptr) {
            entry.second->OnTick(currentTime);
        }
    }
}

void CInstantAgitManager::DoDecoNpcCheck() {
    L2_GUARD_FUNCTION();
    std::vector<int> pledgeIds;
    pledgeIds.reserve(m_PledgeInstantAgitMap.size());
    for (const auto& entry : m_PledgeInstantAgitMap) {
        pledgeIds.push_back(entry.first);
    }
    const std::int32_t currentTime = runtime::crt::Time32Now();
    for (const int pledgeId : pledgeIds) {
        if (CInstantAgit* const agit = GetInstantAgit(pledgeId)) {
            agit->DoDecoNpcCheck(currentTime);
        }
    }
}

void CInstantAgitManager::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    if (id == 1) {
        CheckLettery();
        DoDecoNpcCheck();
        AddTimer(InstantAgitConst::_TIMER_PERIOD, 1);
    } else {
        CheckLoadingCompleted();
    }
}

void CInstantAgitManager::Init() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\InstantAgit.txt";
#else
    constexpr const wchar_t* kDataPath = L"../Script/InstantAgit.txt";
#endif
    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from InstantAgit file failed",
        //     kInstantAgitSourceWide,
        //     0x75f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from InstantAgit file failed",
        //     kInstantAgitSourceWide,
        //     0x75f)
        L2_LOG_ERROR(
            Log, u"Cannot read provisional clan-hall definitions from '%ls'; loading returns without parsing.", kDataPath
        );
        return;
    }

    CParserForInstantAgit parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForInstantAgit create failed",
        //     kInstantAgitSourceWide,
        //     0x768)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForInstantAgit create failed",
        //     kInstantAgitSourceWide,
        //     0x768)
        L2_LOG_ERROR(
            Log, u"Cannot create the provisional clan-hall parser for '%ls'; definitions are not parsed.", kDataPath
        );
        return;
    }
    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing InstantAgit file failed after line[%d]",
        //     kInstantAgitSourceWide,
        //     0x775,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing InstantAgit file failed after line[%d]",
        //     kInstantAgitSourceWide,
        //     0x775,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            u"Parsing '%ls' failed at line %lld; published definitions are retained and post-load work is not scheduled.",
            kDataPath, static_cast<long long>(parser.m_lexer.line) + 1
        );
        return;
    }
    (new CInstantAgitPostLoadMessage())->Post(0);
}

bool InstantAgitUtil::IsInInstantAgit(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }

    CInstantAgit* const agit = CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(user->m_data->m_nPledgeID));
    return agit != nullptr && user->IsInInZone(static_cast<unsigned int>(agit->InstantZoneSid()));
}
