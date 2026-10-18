#include "Dominion.h"
#include "Alliance.h"
#include "Castle.h"
#include "ControlTower.h"
#include "DB.h"
#include "Fortress.h"
#include "OwnThing.h"
#include "Packet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SerializedMessage.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/LogFormatter.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace {

void CopyTargetWide100(char16_t (&destination)[100], const wchar_t* const source) {
    const std::u16string target = source != nullptr ? WideToUtf16(std::wstring_view(source)) : std::u16string();
    if (target.size() >= 100) {
        destination[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::memcpy(destination, target.c_str(), (target.size() + 1U) * sizeof(char16_t));
}

} // namespace

CDominion::CDominion() :
    m_pMainCastle(nullptr),
    m_pMainOwnThing(nullptr),
    m_pSiegeEntry(nullptr) {
    // Current L2Server 0x006dd8f0 leaves m_bPaused and both
    // fixed name buffers untouched.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_OwnThingLock);
    m_OwnThingList.clear();
}

CDominion::~CDominion() {
    // Current L2Server 0x006ddc14 clears borrowed pointers only. The
    // container envelopes unwind normally afterwards.
    L2_GUARD_FUNCTION();
    m_pMainCastle = nullptr;
    m_pMainOwnThing = nullptr;
    m_pSiegeEntry = nullptr;
}

bool CDominion::IsExtended() const { return m_data->m_nDominionStatus == 2; }

bool CDominion::IsDominion() const { return true; }

CDominion* CDominion::CastDominion() const { return const_cast<CDominion*>(this); }

bool CDominion::IsUnderSiege() const {
    const ResidenceState state = m_data->m_ResidenceState;
    return state == RS_SIEGE || state == RS_MID_VICTORY || state == RS_DOMINION_SIEGE;
}

void CDominion::StartTimer() {
    // Current L2Server 0x006fe5d4 is the shared intentional no-op.
}

void CDominion::ChangeOwner(CPledge*, int, bool) {
    // Current L2Server 0x006fe5d4 is the shared intentional no-op.
}

void CDominion::PassSiege() {
    // Current L2Server 0x006d826c.
    L2_GUARD_FUNCTION();
    ReleaseEntry();
    db.RequestSaveDominionRegistry(*this, 1, 0, 0);
}

void CDominion::ExpelNonPledgeMember() {
    // Current L2Server 0x006dfb8c.
    BanNonDominionMember(false);
}

void CDominion::BanNonDominionMember(const bool expel) {
    // Current L2Server 0x006de6f8.
    L2_GUARD_FUNCTION();
    if (m_pMainCastle == nullptr) {
        return;
    }

    if (expel) {
        m_pMainCastle->BanNonDominionUsers(m_pSiegeEntry);
    } else {
        static_cast<CResidence*>(m_pMainCastle)->ExpelNonPledgeMember();
    }

    m_sDomainFortress = m_pMainCastle->GetDomainFortress();
    if (expel) {
        for (CFortress* const fortress : m_sDomainFortress) {
            if (fortress != nullptr) {
                fortress->BanNonDominionUsers(m_pSiegeEntry);
            }
        }
    }
}

void CDominion::StartSiege() {
    // Current L2Server 0x006de8b0.
    L2_GUARD_FUNCTION();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    if (state != RS_STANDBY && state != RS_DOMINION_SIEGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid status in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"invalid status in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(
            Log,
            "Territory %d: cannot start war in residence state %d; expected STANDBY or DOMINION_SIEGE. Returning to "
            "ANNOUNCING.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<int>(state)
        );
        ChangeResidenceState(RS_ANNOUNCING);
        return;
    }
    if (m_pSiegeEntry == nullptr || m_pSiegeEntry->IsEmpty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"siege entry doesn't exist in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"siege entry doesn't exist in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(
            Log, "Territory %d: cannot start war because its participant registry is %s; returning to ANNOUNCING.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, m_pSiegeEntry == nullptr ? "missing" : "empty"
        );
        ChangeResidenceState(RS_ANNOUNCING);
        return;
    }

    ChangeResidenceState(RS_DOMINION_SIEGE);
    m_pSiegeEntry->SendDominionEvent(true);
    BanNonDominionMember(true);
    if (m_pMainCastle == nullptr) {
        return;
    }

    m_pMainCastle->m_bIsDominionSiege = true;
    m_pMainCastle->m_bSiegeContinued = false;
    static_cast<CResidence*>(m_pMainCastle)->StartSiege();
    m_sDomainFortress = m_pMainCastle->GetDomainFortress();
    for (CFortress* const fortress : m_sDomainFortress) {
        if (fortress == nullptr) {
            continue;
        }
        fortress->m_bIsDominionSiege = true;
        fortress->m_bSiegeContinued = false;
        fortress->m_nDominionSiegeID = m_data != nullptr ? m_data->m_nResidenceID : 0;
        fortress->SetBattleField(true);
        for (CControlTower* const tower : fortress->m_ControlTowerList) {
            tower->OnSiegeStarted(false);
        }
        fortress->SiegeStart();
    }

    {
        L2_GUARD(m_OwnThingLock);
        if (!m_OwnThingList.empty()) {
            CPledge* const pledge = m_pMainCastle->GetResidenceOwnerPledge();
            if (pledge != nullptr) {
                pledge->SendDominionMessageToAllOnlineMember(
                    0xad8, m_data != nullptr ? m_data->m_nResidenceID : 0, EST_STRING, u"", false
                );
            }
        }
    }

    L2_GUARD(m_OwnThingLock);
    for (const auto& entry : m_OwnThingList) {
        COwnThing* const ownThing = entry.first;
        if (ownThing == nullptr) {
            continue;
        }
        ownThing->HandleChangeSiegeState(this, true);
        CPledge* const pledge = m_pMainCastle->GetResidenceOwnerPledge();
        CDominion* const original = g_ResidenceDB.GetDominion(ownThing->GetOriginalResidenceID());
        if (pledge != nullptr && original != nullptr) {
            original->RemoveResidenceSkill(*pledge);
        }
    }
}

void CDominion::EndSiege() {
    // Current L2Server 0x006dece0.
    L2_GUARD_FUNCTION();
    if (m_pMainCastle == nullptr) {
        return;
    }

    m_pMainCastle->m_bIsDominionSiege = true;
    m_pMainCastle->m_bSiegeContinued = false;
    static_cast<CResidence*>(m_pMainCastle)->EndSiege();
    m_pMainCastle->m_bIsDominionSiege = false;
    m_pMainCastle->m_bSiegeContinued = false;
    m_sDomainFortress = m_pMainCastle->GetDomainFortress();
    BanNonDominionMember(false);

    for (CFortress* const fortress : m_sDomainFortress) {
        if (fortress == nullptr) {
            continue;
        }
        fortress->NotifyDominionSiegeEnd();
        fortress->m_bIsDominionSiege = false;
        fortress->m_bSiegeContinued = false;
        fortress->m_nDominionSiegeID = 0;
    }
    if (m_pSiegeEntry != nullptr) {
        m_pSiegeEntry->SendDominionEvent(false);
        m_pSiegeEntry->UpdateWindowPacket();
    }
    ChangeResidenceState(RS_ANNOUNCING);
    db.RequestSaveDominionRegistry(*this, 1, 0, 0);

    {
        L2_GUARD(m_OwnThingLock);
        for (const auto& entry : m_OwnThingList) {
            if (entry.first != nullptr) {
                entry.first->HandleChangeSiegeState(this, false);
            }
        }
    }
    AddAllResidenceSkill();
}

void CDominion::PauseSiege() {
    // Current L2Server 0x006def60.
    L2_GUARD_FUNCTION();
    if (m_bPaused) {
        return;
    }
    m_bPaused = true;
    if (m_pMainCastle == nullptr) {
        return;
    }

    m_pMainCastle->m_bIsDominionSiege = true;
    m_pMainCastle->m_bSiegeContinued = false;
    static_cast<CResidence*>(m_pMainCastle)->PauseSiege();
    if (CDominionManager::Inst()->m_bServerDownResume) {
        m_pMainCastle->SetBattleField(true);
    }
    m_sDomainFortress = m_pMainCastle->GetDomainFortress();
    for (CFortress* const fortress : m_sDomainFortress) {
        if (fortress == nullptr) {
            continue;
        }
        fortress->m_bIsDominionSiege = true;
        fortress->m_bSiegeContinued = false;
        fortress->SetFreezeField(true);
        if (CDominionManager::Inst()->m_bServerDownResume) {
            fortress->SetBattleField(true);
        }
    }
}

void CDominion::ResumeSiege() {
    // Current L2Server 0x006df1a8.
    L2_GUARD_FUNCTION();
    const bool serverDownResume = CDominionManager::Inst()->m_bServerDownResume;
    if (!m_bPaused && !serverDownResume) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Try duplicated resume siege in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // Noise: A repeated resume request for a territory that is not paused is an expected no-op. The existing resume
        // guard is retained. L2_LOG_ERROR(Log,
        //     L"Try duplicated resume siege in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        return;
    }
    m_bPaused = false;
    if (m_pSiegeEntry == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"siege entry doesn't exist in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"siege entry doesn't exist in dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(
            Log,
            "Territory %d: cannot resume war because its participant registry is missing; the pause flag is already cleared "
            "and the territory returns to ANNOUNCING.",
            m_data != nullptr ? m_data->m_nResidenceID : 0
        );
        ChangeResidenceState(RS_ANNOUNCING);
        return;
    }

    ChangeResidenceState(RS_DOMINION_SIEGE);
    if (m_pMainCastle == nullptr) {
        return;
    }
    m_pMainCastle->m_bIsDominionSiege = true;
    m_pMainCastle->m_bSiegeContinued = true;
    if (!serverDownResume) {
        static_cast<CResidence*>(m_pMainCastle)->ResumeSiege();
    } else {
        db.RequestLoadBattleCamp(m_pMainCastle, 0);
        db.RequestLoadBattleCamp(m_pMainCastle, 1);
        static_cast<CResidence*>(m_pMainCastle)->StartSiege();
    }

    m_sDomainFortress = m_pMainCastle->GetDomainFortress();
    for (CFortress* const fortress : m_sDomainFortress) {
        if (fortress == nullptr) {
            continue;
        }
        fortress->m_bIsDominionSiege = true;
        fortress->m_bSiegeContinued = false;
        fortress->m_nDominionSiegeID = m_data != nullptr ? m_data->m_nResidenceID : 0;
        if (serverDownResume) {
            db.RequestLoadBattleCamp(fortress, 0);
            db.RequestLoadBattleCamp(fortress, 1);
            for (CControlTower* const tower : fortress->m_ControlTowerList) {
                tower->OnSiegeStarted(true);
            }
        }
        fortress->SetFreezeField(false);
        fortress->SiegeStart();
    }
}

void CDominion::AddAllResidenceSkill() {
    // Current L2Server 0x006d900c.
    L2_GUARD_FUNCTION();
    if (m_pMainOwnThing == nullptr ||
        m_pMainOwnThing->GetOwningResidenceID() != (m_data != nullptr ? m_data->m_nResidenceID : 0)) {
        return;
    }
    CPledge* const pledge = m_pMainCastle != nullptr ? m_pMainCastle->GetResidenceOwnerPledge() : nullptr;
    if (pledge == nullptr) {
        return;
    }

    L2_ADD_REF(pledge, ObjectRefType::FunctionChain);
    {
        L2_GUARD(m_OwnThingLock);
        for (const auto& entry : m_OwnThingList) {
            if (entry.first == nullptr) {
                continue;
            }
            CDominion* const dominion = g_ResidenceDB.GetDominion(entry.first->GetOriginalResidenceID());
            if (dominion != nullptr) {
                dominion->AddResidenceSkill(*pledge, CastleType::CASTLE_NOTHING);
            }
        }
    }
    L2_RELEASE(pledge, ObjectRefType::FunctionChain);
}

void CDominion::RemoveAllResidenceSkill(CSPointer<CPledge> pledge) {
    // Current L2Server 0x006d91ac.
    L2_GUARD_FUNCTION();
    if (m_pMainOwnThing == nullptr ||
        m_pMainOwnThing->GetOwningResidenceID() != (m_data != nullptr ? m_data->m_nResidenceID : 0) || !pledge) {
        return;
    }

    L2_GUARD(m_OwnThingLock);
    for (const auto& entry : m_OwnThingList) {
        if (entry.first == nullptr) {
            continue;
        }
        CDominion* const dominion = g_ResidenceDB.GetDominion(entry.first->GetOriginalResidenceID());
        if (dominion != nullptr) {
            dominion->RemoveResidenceSkill(*pledge.get());
        }
    }
}

void CDominion::SetPickupOwnThingName(wchar_t* const name) {
    // Current L2Server 0x006d85f8.
    L2_GUARD_FUNCTION();
    CopyTargetWide100(m_sPickupOwnThingName, name);
}

void CDominion::SetHideNameItemName(wchar_t* const name) {
    // Current L2Server 0x006d86b0.
    L2_GUARD_FUNCTION();
    CopyTargetWide100(m_sHideNameItemName, name);
}

void CDominion::SetMainCastle(CCastle* const castle) {
    // Current L2Server 0x006d8990.
    m_pMainCastle = castle;
    if (castle == nullptr) {
        return;
    }

    castle->SetDominion(this);
    CPledge* const pledge = castle->GetResidenceOwnerPledge();
    if (pledge == nullptr) {
        return;
    }

    L2_ADD_REF(pledge, ObjectRefType::FunctionChain);
    pledge->SetDominion(this);
    SetPledge(pledge);
    L2_RELEASE(pledge, ObjectRefType::FunctionChain);
}

void CDominion::AddPos(FVector position) {
    // Current L2Server 0x006df6c8.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_PosLock);
        m_vPosList.push_back(std::make_pair(position, false));
    }
}

wchar_t* CDominion::GetPickupOwnThingName() {
    L2_GUARD_FUNCTION();
#if WCHAR_MAX <= 0xffff
    return reinterpret_cast<wchar_t*>(m_sPickupOwnThingName);
#else
    thread_local std::wstring hostName;
    hostName = Utf16ToWide(std::u16string_view(m_sPickupOwnThingName));
    return hostName.data();
#endif
}

wchar_t* CDominion::GetHideNameItemName() {
    // Current L2Server 0x006d7e48.
    L2_GUARD_FUNCTION();
#if WCHAR_MAX <= 0xffff
    return reinterpret_cast<wchar_t*>(m_sHideNameItemName);
#else
    thread_local std::wstring hostName;
    hostName = Utf16ToWide(std::u16string_view(m_sHideNameItemName));
    return hostName.data();
#endif
}

FVector CDominion::ReservePos() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PosLock);
    FVector result{};
    for (auto& entry : m_vPosList) {
        if (!entry.second) {
            result = entry.first;
            entry.second = true;
            break;
        }
    }
    if (result.IsEqual(FVector{})) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ownthing pos is full! dominion %d",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"ownthing pos is full! dominion %d",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(
            Log,
            "Territory %d: no usable ward position was reserved (result 0, 0, 0; configured positions %llu); the ward is "
            "not added to the territory registry.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<unsigned long long>(m_vPosList.size())
        );
    }
    return result;
}

void CDominion::FreePos(FVector position) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PosLock);
    for (auto& entry : m_vPosList) {
        if (entry.first.IsEqual(position)) {
            entry.second = false;
            break;
        }
    }
}

FVector CDominion::_GetPos(COwnThing* const ownThing) {
    L2_GUARD_FUNCTION();
    FVector result{};
    for (const auto& entry : m_OwnThingList) {
        if (entry.first == ownThing) {
            result = entry.second;
            break;
        }
    }
    return result;
}

FVector CDominion::GetPos(COwnThing* const ownThing) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_OwnThingLock);
    return _GetPos(ownThing);
}

void CDominion::RemoveOwnthing(COwnThing* const ownThing) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_OwnThingLock);
    for (auto position = m_OwnThingList.begin(); position != m_OwnThingList.end(); ++position) {
        if (position->first == ownThing) {
            FreePos(position->second);
            m_OwnThingList.erase(position);
            return;
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"Attempted to delete non-existing ownthing!")
    // L2_LOG_ERROR(Log,
    //     L"Attempted to delete non-existing ownthing!")
    L2_LOG_WARN(
        Log, "Territory %d: ward removal requested for an unregistered ward; no position is freed.",
        m_data != nullptr ? m_data->m_nResidenceID : 0
    );
}

void CDominion::AddOwnThing(COwnThing* const ownThing) {
    L2_GUARD_FUNCTION();
    const FVector position = ReservePos();
    if (!position.IsEqual(FVector{})) {
        L2_GUARD(m_OwnThingLock);
        m_OwnThingList.push_back(std::make_pair(ownThing, position));
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"No more usable position to set ownthing. error %d",
    //     m_data != nullptr ? m_data->m_nResidenceID : 0)
    // Noise: ReservePos already reports the unusable zero-vector result. Repeating it in AddOwnThing adds no new failure
    // context. L2_LOG_ERROR(Log,
    //     L"No more usable position to set ownthing. error %d",
    //     m_data != nullptr ? m_data->m_nResidenceID : 0)
}

CDominionSiegeEntry::CDominionSiegeEntry(CDominion* const dominion) {
    L2_GUARD_FUNCTION();
    Clear();
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Dominion pointer miss in making Siege Entry!")
        // L2_LOG_ERROR(Log,
        //     L"Dominion pointer miss in making Siege Entry!")
        L2_LOG_ERROR(
            Log, "Cannot initialize the territory-war participant registry: no territory was supplied; initialization stops."
        );
        return;
    }

    m_pDominion = dominion;
    int dominionId = 0;
    if (dominion->m_data != nullptr) {
        dominionId = dominion->m_data->m_nResidenceID;
    }
    m_nDominionID = dominionId;
}

bool CDominionSiegeEntry::IsEmpty() {
    L2_GUARD_FUNCTION();
    return m_vPledgeList.empty() && m_vUserList.empty();
}

void CDominionSiegeEntry::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_vPledgeList.clear();
    m_vUserList.clear();
}

void CDominionSiegeEntry::SetSiegeRole(const bool underSiege) {
    L2_GUARD_FUNCTION();
    if (m_pDominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Dominion pointer doesn't exist in SiegeEntry : "
        //     L"SetSiegeRole()")
        // L2_LOG_ERROR(Log,
        //     L"Dominion pointer doesn't exist in SiegeEntry : "
        //     L"SetSiegeRole()")
        L2_LOG_ERROR(
            Log, "Cannot %s territory-war roles because the participant registry has no territory.",
            underSiege ? "assign" : "clear"
        );
        return;
    }

    const int dominionId = underSiege && m_pDominion->m_data != nullptr ? m_pDominion->m_data->m_nResidenceID : 0;
    L2_GUARD(m_Lock);
    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        if (!user) {
            continue;
        }
        CPledge* const pledge = user->GetPledge();
        if (pledge == nullptr || pledge->SharedData() == nullptr || pledge->SharedData()->m_nDominionSiegeId == 0) {
            user->m_data->m_nDominionSiegeId = dominionId;
            user->m_data->m_nDominionSiegeStatus = underSiege ? 2 : 0;
        }
    }

    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->SetDominionUnderSiege(underSiege ? m_pDominion : nullptr, true);
        }
    }
}

void CDominionSiegeEntry::Say(User* const user, const char16_t* const message, const SayPacketType type) {
    // Current L2Server 0x006db9e8. The one lock spans both
    // membership lists and the sender name is re-read for every solo user.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->Say(user, message, type, false, u"");
        }
    }

    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        if (receiver) {
            receiver->Hear(user, user->GetName(), message, type);
        }
    }
}

void CDominionSiegeEntry::SendDominionChannelSet(const bool enabled) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const std::uint32_t pledgeId : m_vPledgeList) {
            CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
            if (pledge) {
                pledge->SendDominionChannelSet(enabled);
            }
        }
        for (const std::uint32_t userId : m_vUserList) {
            CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
            if (user) {
                user->PushSerializedMessage(new CSerializedDominionChannelSetMessage(user.get(), enabled));
            }
        }
    }
}

void CDominionSiegeEntry::CancelAllRenamed() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->CancelAllRenamed();
        }
    }
    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        if (user) {
            user->m_nDominionRenamedID = 0;
            user->PushSerializedMessage(new CSerializedRenameByDominionSiegeMessage(user.get(), false));
        }
    }
}

void CDominionSiegeEntry::SendDominionMessage(
    const int messageId,
    const int argument,
    const eSMsgType argumentType,
    const char16_t* const text,
    const bool argumentFirst
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->SendDominionMessageToAllOnlineMember(messageId, argument, argumentType, text, argumentFirst);
        }
    }
    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        CUserSocket* const socket = user ? user->GetSocket() : nullptr;
        if (socket == nullptr) {
            continue;
        }
        if (argumentFirst) {
            socket->Send(
                "chccSch", 0x62, static_cast<std::int16_t>(messageId), 2, argumentType, text, EST_CASTLEID,
                static_cast<std::int16_t>(argument)
            );
        } else {
            socket->Send(
                "chcchcS", 0x62, static_cast<std::int16_t>(messageId), 2, EST_CASTLEID, static_cast<std::int16_t>(argument),
                argumentType, text
            );
        }
    }
}

void CDominionSiegeEntry::SendDominionMessage2(const int messageId, const int argument) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->SendDominionMessageToAllOnlineMember2(messageId, argument);
        }
    }
    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        if (!user) {
            continue;
        }
        if (argument == 0) {
            user->SendSystemMessage(messageId);
        } else {
            user->SendSystemMessage_d(messageId, argument);
        }
    }
}

void CDominionSiegeEntry::SendDominionEventToPledge(const bool start) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->SendDominionEventToAllOnlineMember(0, m_nDominionID, !start, start);
        }
    }
}

void CDominionSiegeEntry::SendDominionEventToUser(const bool start) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        if (!user) {
            continue;
        }

        CSPointer<CPledge> pledge(user->GetPledge(), ObjectRefType::FunctionChain);
        if (!pledge || pledge->SharedData()->m_nDominionSiegeId == 0) {
            user->PushSerializedMessage(new CSerializedDominionEventMessage(user.get(), !start, 0, m_nDominionID, start));
        }
    }
}

void CDominionSiegeEntry::SendDominionScriptEventToPledge(const int eventType, const int npcId, const int dominionId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->SendDominionScriptEventToAllOnlineMember(eventType, npcId, dominionId);
        }
    }
}

void CDominionSiegeEntry::SendDominionScriptEventToUser(const int eventType, const int npcId, const int dominionId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t userId : m_vUserList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userId)));
        if (user) {
            user->PushSerializedMessage(new CSerializedDominionScriptEventMessage(user.get(), eventType, npcId, dominionId));
        }
    }
}

void CDominionSiegeEntry::SendDominionEvent(const bool start) {
    L2_GUARD_FUNCTION();
    SendDominionEventToUser(start);
    SendDominionEventToPledge(start);
}

void CDominionSiegeEntry::SendDominionScriptEvent(const int eventType, const int npcId, const int dominionId) {
    L2_GUARD_FUNCTION();
    SendDominionScriptEventToUser(eventType, npcId, dominionId);
    SendDominionScriptEventToPledge(eventType, npcId, dominionId);
}

void CDominionSiegeEntry::UpdateWindowPacket() {
    // Current L2Server 0x006d966c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const std::uint32_t pledgeId : m_vPledgeList) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeId));
        if (pledge) {
            pledge->UpdateInfoWindowPacket();
        }
    }
}

bool CDominionSiegeEntry::IsInSiegePledge(std::uint32_t pledgeId) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_Lock);
    bool found = false;
    for (const std::uint32_t registeredId : m_vPledgeList) {
        if (registeredId == pledgeId) {
            found = true;
        }
    }
    return found;
}

bool CDominionSiegeEntry::IsInSiegeUser(std::uint32_t userId) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_Lock);
    bool found = false;
    for (const std::uint32_t registeredId : m_vUserList) {
        if (registeredId == userId) {
            found = true;
        }
    }
    return found;
}

bool CDominionSiegeEntry::AddPledge(const std::uint32_t pledgeId) {
    L2_GUARD_FUNCTION();
    if (CDominionManager::Inst()->GetPledgeDominionSiegeID(static_cast<int>(pledgeId)) != 0) {
        return false;
    }
    L2_GUARD(m_Lock);
    m_vPledgeList.push_back(pledgeId);
    return true;
}

bool CDominionSiegeEntry::AddUser(const std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    if (CDominionManager::Inst()->GetUserDominionSiegeID(static_cast<int>(userId)) != 0) {
        return false;
    }
    L2_GUARD(m_Lock);
    m_vUserList.push_back(userId);
    return true;
}

bool CDominionSiegeEntry::RemovePledge(const std::uint32_t pledgeId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (auto position = m_vPledgeList.begin(); position != m_vPledgeList.end(); ++position) {
        if (*position == pledgeId) {
            m_vPledgeList.erase(position);
            return true;
        }
    }
    return false;
}

bool CDominionSiegeEntry::RemoveUser(const std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (auto position = m_vUserList.begin(); position != m_vUserList.end(); ++position) {
        if (*position == userId) {
            m_vUserList.erase(position);
            return true;
        }
    }
    return false;
}

bool CDominion::IsPledgeInSiege(int pledgeId) {
    L2_GUARD_FUNCTION();
    return m_pSiegeEntry != nullptr && m_pSiegeEntry->IsInSiegePledge(static_cast<std::uint32_t>(pledgeId));
}

bool CDominion::IsUserInSiege(int userId) {
    L2_GUARD_FUNCTION();
    return m_pSiegeEntry != nullptr && m_pSiegeEntry->IsInSiegeUser(static_cast<std::uint32_t>(userId));
}

bool CDominion::RegisterPledge(const int pledgeId, const bool save) {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry == nullptr || !m_pSiegeEntry->AddPledge(static_cast<std::uint32_t>(pledgeId))) {
        return false;
    }
    if (save) {
        db.RequestSaveDominionRegistry(*this, 0, 1, pledgeId);
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    if (pledge) {
        pledge->SetDominionUnderSiege(this, false);
        pledge->UpdateInfoWindowPacket();
    }
    return true;
}

bool CDominion::RegisterUser(const int userId, const bool save) {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry == nullptr || !m_pSiegeEntry->AddUser(static_cast<std::uint32_t>(userId))) {
        return false;
    }
    if (save) {
        db.RequestSaveDominionRegistry(*this, 0, 0, userId);
    }
    return true;
}

bool CDominion::UnregisterPledge(const int pledgeId, const bool save) {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry == nullptr || !m_pSiegeEntry->RemovePledge(static_cast<std::uint32_t>(pledgeId))) {
        return false;
    }
    if (save) {
        db.RequestSaveDominionRegistry(*this, 2, 1, pledgeId);
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    if (pledge) {
        pledge->SetDominionUnderSiege(nullptr, false);
        pledge->UpdateInfoWindowPacket();
    }
    return true;
}

bool CDominion::UnregisterUser(const int userId, const bool save) {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry == nullptr || !m_pSiegeEntry->RemoveUser(static_cast<std::uint32_t>(userId))) {
        return false;
    }
    if (save) {
        db.RequestSaveDominionRegistry(*this, 2, 0, userId);
    }
    return true;
}

void CDominion::ReleaseEntry() {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry != nullptr) {
        L2_RELEASE(m_pSiegeEntry, ObjectRefType::CreateDelete);
        m_pSiegeEntry = nullptr;
    }
}

void CDominion::StandBySiege() {
    L2_GUARD_FUNCTION();
    ChangeResidenceState(RS_STANDBY);
}

void CDominion::MakeEntry() {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Dominion Siege Entry Doesn't Released! ID %d",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"Dominion Siege Entry Doesn't Released! ID %d",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(
            Log,
            "Territory %d: replacing a participant registry that was not released; the previous registry is released before "
            "a new one is created.",
            m_data != nullptr ? m_data->m_nResidenceID : 0
        );
        L2_RELEASE(m_pSiegeEntry, ObjectRefType::CreateDelete);
        m_pSiegeEntry = nullptr;
    }
    m_pSiegeEntry = new CDominionSiegeEntry(this);
}

void CDominion::CancelAllRenamed() {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot rename because no entry, dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"Cannot rename because no entry, dominion %d.",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(
            Log, "Territory %d: cannot clear war aliases because its participant registry is missing.",
            m_data != nullptr ? m_data->m_nResidenceID : 0
        );
        return;
    }
    m_pSiegeEntry->CancelAllRenamed();
}

void CDominion::Say(User* const user, const char16_t* const message, const SayPacketType type) {
    // Current L2Server 0x006dcacc.
    L2_GUARD_FUNCTION();
    if (user != nullptr && m_pSiegeEntry != nullptr) {
        m_pSiegeEntry->Say(user, message, type);
    }
}

void CDominion::SetSiegeRole(const bool underSiege) {
    L2_GUARD_FUNCTION();
    if (m_pSiegeEntry != nullptr) {
        m_pSiegeEntry->SetSiegeRole(underSiege);
    }
}

void CDominion::RequestBroadcastMessage(const int messageType) {
    L2_GUARD_FUNCTION();
    if (m_pMainCastle == nullptr) {
        return;
    }
    m_pMainCastle->BroadcastDominionMessage(messageType);
    m_sDomainFortress = m_pMainCastle->GetDomainFortress();
    for (CFortress* const fortress : m_sDomainFortress) {
        if (fortress != nullptr) {
            fortress->BroadcastDominionMessage(messageType);
        }
    }
}

void CDominion::StartRegistration() {
    L2_GUARD_FUNCTION();
    ChangeResidenceState(RS_REGISTRATION);
    MakeEntry();
    RegisterPledge(m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0, true);
}

void CDominion::ChangeDominionState(const int state) {
    L2_GUARD_FUNCTION();
    if (m_data != nullptr) {
        m_data->m_ResidenceState = static_cast<ResidenceState>(state);
        db.RequestSaveDominion(*this, m_data->m_nDominionStatus, state);
    }
}

void CDominion::DisableDominionExtends(CSPointer<CPledge> pledge) {
    // Current L2Server 0x006d8c48.
    L2_GUARD_FUNCTION();
    if (m_pMainCastle == nullptr) {
        return;
    }

    if (IsExtended()) {
        m_pMainCastle->SetTaxRates(
            m_pMainCastle->m_data->m_nTaxRateToChange + 10,
            static_cast<CastleType::Enum>(m_pMainCastle->m_data->m_ResidenceType)
        );
        m_pMainCastle->ChangeTaxRate();
    }
    m_pMainCastle->SetMaxChangeRateToAllManor(1.0);

    if (pledge) {
        L2_GUARD(m_OwnThingLock);
        for (const auto& entry : m_OwnThingList) {
            if (entry.first == nullptr) {
                continue;
            }
            CResidence* const residence = entry.first->GetOwningResidence();
            if (residence != nullptr && residence->IsFortress()) {
                residence->CastFortress()->RemoveResidenceSkill(*pledge.get());
            }
        }
    }
    m_pMainCastle->SetAllNPCPledge(0);
}

void CDominion::ChangeDominionStatus(const int status) {
    L2_GUARD_FUNCTION();
    if (m_data != nullptr) {
        m_data->m_nDominionStatus = status;
        db.RequestSaveDominion(*this, status, static_cast<int>(m_data->m_ResidenceState));
    }
}

int CDominion::EnableDominionExtends() {
    // Current L2Server 0x006d7ed8.
    L2_GUARD_FUNCTION();
    if (m_pMainCastle != nullptr) {
        if (m_data->m_nDominionStatus != 2) {
            m_pMainCastle->SetTaxRates(
                m_pMainCastle->m_data->m_nTaxRateToChange - 10,
                static_cast<CastleType::Enum>(m_pMainCastle->m_data->m_ResidenceType)
            );
            m_pMainCastle->ChangeTaxRate();
        }
        m_pMainCastle->SetMaxChangeRateToAllManor(1.1);
        CPledge* const owner = m_pMainCastle->GetResidenceOwnerPledge();
        if (owner != nullptr) {
            m_pMainCastle->SetAllNPCPledge(owner->GetDbId());
        }
    }
    return 1;
}

int CDominion::ExtendDominion() {
    // Current L2Server 0x006d8768.
    L2_GUARD_FUNCTION();
    if (m_data->m_nDominionStatus == 2) {
        return 0;
    }

    CPledge* const owner = GetResidenceOwnerPledge();
    if (owner != nullptr) {
        int residenceId = 0;
        int ownerPledgeId = 0;
        if (m_data != nullptr) {
            residenceId = m_data->m_nResidenceID;
            ownerPledgeId = static_cast<int>(m_data->m_nResidenceOwnerID);
        }
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7e4, GetResidenceOwnerPledge()->GetMaster(), nullptr, GetResidenceOwnerPledge()->GetPledgeName(), nullptr,
            nullptr, ownerPledgeId, residenceId, 1, 0, 0, 0, 0
        );
    }
    ChangeDominionStatus(2);
    EnableDominionExtends();
    return 1;
}

int CDominion::CancelExtendDominion() {
    // Current L2Server 0x006d8e20.
    L2_GUARD_FUNCTION();
    if (m_data->m_nDominionStatus != 2) {
        return 0;
    }

    CPledge* const owner = GetResidenceOwnerPledge();
    if (owner != nullptr) {
        int residenceId = 0;
        int ownerPledgeId = 0;
        if (m_data != nullptr) {
            residenceId = m_data->m_nResidenceID;
            ownerPledgeId = static_cast<int>(m_data->m_nResidenceOwnerID);
        }
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7e4, GetResidenceOwnerPledge()->GetMaster(), nullptr, GetResidenceOwnerPledge()->GetPledgeName(), nullptr,
            nullptr, ownerPledgeId, residenceId, 0, 0, 0, 0, 0
        );
    }
    ChangeDominionStatus(1);
    const int ownerPledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
    CSPointer<CPledge> ownerPledge = CPledge::FindPledge(g_nWorldId, ownerPledgeId);
    if (ownerPledge) {
        DisableDominionExtends(ownerPledge);
    }
    return 1;
}

CDominionManager* CDominionManager::Inst() {
    static CDominionManager instance;
    return &instance;
}

L2_DEFINE_OBJECT_CLASS_ID(CDominionManager)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CDominionManager)

L2_DEFINE_OBJECT_ALLOCATOR(CDominionManager, __GetClassName())

L2_DEFINE_OBJECT_NEW(CDominionManager)

CDominionManager::CDominionManager() :
    m_tPreviousSiegeEnd(0),
    m_tNextSiegeTime(0),
    m_bNextSiegeTimeSet(false),
    m_alignment81To84{},
    m_nErrorCount(0),
    m_bPaused(false),
    m_alignment89To8C{},
    m_tSiegeStartTime(0),
    m_tSiegeElapsedTime(0),
    m_nSaveTick(0),
    m_tSiegeTime(BP::_DOMINION_SIEGE_TIME),
    m_tPausedTime(0),
    m_nAnnouncedStatus(0),
    m_bServerDownResume(false),
    m_alignmentA5ToA8{},
    m_nState(DSST_FINISHED),
    m_bSentDominionChannelSet(false),
    m_bCanceledRenamed(false),
    m_alignmentAEToB0{},
    m_nLoadedOwnThing(0),
    m_nScriptEventSaveCount(0),
    m_nScriptEventSaveTick(0),
    m_alignmentBCToC0{} {
    L2_GUARD_FUNCTION();
    Clear();
}

CDominionManager::~CDominionManager() { L2_GUARD_FUNCTION(); }

L2_DEFINE_OBJECT_TYPED_RELEASE(CDominionManager)

void CDominionManager::Clear() {
    L2_GUARD_FUNCTION();
    SetSiegeEndTime(0, false);
    m_tNextSiegeTime = 0;
    m_bServerDownResume = false;
    m_tSiegeStartTime = 0;
    m_nSaveTick = 0;
    m_bPaused = false;
    m_tSiegeTime = BP::_DOMINION_SIEGE_TIME;
    m_lstDominion.clear();
    m_lstStart.clear();
    m_nState = DSST_FINISHED;
    m_bCanceledRenamed = false;
    m_bNextSiegeTimeSet = false;
    m_nLoadedOwnThing = 0;
    m_nErrorCount = 0;
}

void CDominionManager::SetSiegeEndTime(const int siegeEndTime, const bool save) {
    L2_GUARD_FUNCTION();
    m_tPreviousSiegeEnd = siegeEndTime;
    if (save) {
        db.RequestSaveDominionSiege(static_cast<int>(m_nState), m_tPreviousSiegeEnd, m_tSiegeElapsedTime, m_tNextSiegeTime);
    }
}

bool CDominionManager::IsInReadyToDominionSiege() {
    const int now = runtime::crt::Time32Now();
    if (m_nState == DSST_DOMINION_SIEGE) {
        return true;
    }
    if (m_nState == DSST_STANDBY) {
        return m_tNextSiegeTime - now >= 1200;
    }
    if (m_nState == DSST_FINISHED) {
        return now - m_tPreviousSiegeEnd >= 600;
    }
    return false;
}

void CDominionManager::SetSiegeTime() {
    L2_GUARD_FUNCTION();
    m_tSiegeTime = BP::_DOMINION_SIEGE_TIME - m_tSiegeElapsedTime;
}

void CDominionManager::SetSiegeTimeByBuilder() {
    L2_GUARD_FUNCTION();
    const int now = runtime::crt::Time32Now();
    if (m_nState == DSST_DOMINION_SIEGE && m_tSiegeStartTime + BP::_DOMINION_SIEGE_TIME < now) {
        BP::_DOMINION_SIEGE_TIME = now - m_tSiegeStartTime;
    }
    m_tSiegeTime = BP::_DOMINION_SIEGE_TIME;
}

void CDominionManager::SetNextSiegeTime(const int nextSiegeTime, const bool save) {
    L2_GUARD_FUNCTION();
    m_tNextSiegeTime = nextSiegeTime;
    if (save) {
        db.RequestSaveDominionSiege(static_cast<int>(m_nState), m_tPreviousSiegeEnd, m_tSiegeElapsedTime, m_tNextSiegeTime);
    }
}

void CDominionManager::ChangeState(const DominionSiegeStateType state, const bool save) {
    L2_GUARD_FUNCTION();
    m_nState = state;
    m_bSentDominionChannelSet = false;
    if (save) {
        db.RequestSaveDominionSiege(static_cast<int>(m_nState), m_tPreviousSiegeEnd, m_tSiegeElapsedTime, m_tNextSiegeTime);
    }
}

int CDominionManager::AdjustNextSiegeTime() {
    L2_GUARD_FUNCTION();
    if (m_tPreviousSiegeEnd == 0) {
        // Log.Add(LOG_ERROR, L"Dominion Ended Time 0!")
        // L2_LOG_ERROR(Log, L"Dominion Ended Time 0!")
        L2_LOG_INFO(
            Log,
            "No previous territory-war end timestamp is stored; initializing it to the current time before scheduling the "
            "next war."
        );
        SetSiegeEndTime(runtime::crt::Time32Now(), true);
    }

    const std::int32_t adjusted = runtime::SubtractInt32Wrapping(m_tPreviousSiegeEnd, 0x15180);
    const std::tm* const local = runtime::crt::LocalTime32(adjusted);
    if (local == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "localtime_l2 is null in CDominion::GetSelectableSiegeTimes")
        // L2_LOG_ERROR(Log,
        //     "localtime_l2 is null in CDominion::GetSelectableSiegeTimes")
        L2_LOG_ERROR(
            Log,
            "Cannot calculate the next territory war: local-time conversion failed for adjusted epoch time %d (previous end "
            "%d); the schedule calculation returns 0.",
            adjusted, m_tPreviousSiegeEnd
        );
        return 0;
    }

    const int weekdayOffset = local->tm_wday < 1 ? 6 : local->tm_wday - 1;
    const std::int32_t hourOfWeek =
        runtime::AddInt32Wrapping(local->tm_hour, runtime::MultiplyInt32Wrapping(weekdayOffset, 24));
    const std::int32_t minuteOfWeek =
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(hourOfWeek, 60), local->tm_min);
    const std::int32_t weekBase = runtime::SubtractInt32Wrapping(
        runtime::AddInt32Wrapping(adjusted, runtime::MultiplyInt32Wrapping(minuteOfWeek, -60)), local->tm_sec
    );
    std::int32_t next = runtime::AddInt32Wrapping(weekBase, 0x1a25c0);
    if (g_nCountryCode == 2) {
        next = runtime::AddInt32Wrapping(weekBase, 0x1a33d0);
    }

    // Noise: Calendar conversion was only preparation for the suppressed intermediate time dump.
    // const std::tm* const nextLocal =
    //     runtime::crt::LocalTime32(next)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"%d year %d month %d day %d hour %d min.",
    //     nextLocal->tm_year,
    //     nextLocal->tm_mon,
    //     nextLocal->tm_mday,
    //     nextLocal->tm_hour,
    //     nextLocal->tm_min)
    // Noise: This intermediate calendar dump precedes the final fallback. StartRegistration reports the selected schedule
    // once. L2_LOG_INFO(Log,
    //     L"%d year %d month %d day %d hour %d min.",
    //     nextLocal->tm_year,
    //     nextLocal->tm_mon,
    //     nextLocal->tm_mday,
    //     nextLocal->tm_hour,
    //     nextLocal->tm_min)

    const std::int32_t now = runtime::crt::Time32Now();
    // Noise: Calendar conversion was only preparation for the suppressed intermediate time dump.
    // const std::tm* const nowLocal =
    //     runtime::crt::LocalTime32(now)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"now : %d year %d month %d day %d hour %d min.",
    //     nowLocal->tm_year,
    //     nowLocal->tm_mon,
    //     nowLocal->tm_mday,
    //     nowLocal->tm_hour,
    //     nowLocal->tm_min)
    // Noise: The current clock is already in the log prefix. The selected war schedule is reported by StartRegistration.
    // L2_LOG_INFO(Log,
    //     L"now : %d year %d month %d day %d hour %d min.",
    //     nowLocal->tm_year,
    //     nowLocal->tm_mon,
    //     nowLocal->tm_mday,
    //     nowLocal->tm_hour,
    //     nowLocal->tm_min)

    if (next == 0) {
        next = runtime::AddInt32Wrapping(now, 0xe10);
    }
    return next;
}

void CDominionManager::Add(CDominion* const dominion) {
    L2_GUARD_FUNCTION();
    if (dominion == nullptr) {
        // Log.Add(LOG_ERROR, "Adding dominion to manager failed!")
        // L2_LOG_ERROR(Log, "Adding dominion to manager failed!")
        L2_LOG_ERROR(Log, "Cannot register a territory with the war manager: the territory pointer is null.");
        return;
    }
    L2_GUARD(m_DominionLock);
    m_lstDominion.push_back(dominion);
}

void CDominionManager::AddStart(CDominion* const dominion) {
    L2_GUARD_FUNCTION();
    if (dominion == nullptr) {
        // Log.Add(LOG_ERROR, "Adding dominion to manager failed!")
        // L2_LOG_ERROR(Log, "Adding dominion to manager failed!")
        L2_LOG_ERROR(Log, "Cannot add a territory to the active war list: the territory pointer is null.");
        return;
    }
    L2_GUARD(m_StartLock);
    m_lstStart.push_back(dominion);
}

int CDominionManager::GetUserDominionSiegeID(const int userId) {
    L2_GUARD_FUNCTION();
    if ((m_nState == DSST_FINISHED || m_nState == DSST_CASTLE_SIEGE) && !IsInReadyToDominionSiege()) {
        return 0;
    }
    L2_GUARD(m_StartLock);
    int dominionId = 0;
    for (CDominion* const dominion : m_lstStart) {
        if (dominion != nullptr && dominion->IsUserInSiege(userId)) {
            dominionId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
        }
    }
    return dominionId;
}

int CDominionManager::GetPledgeDominionSiegeID(const int pledgeId) {
    L2_GUARD_FUNCTION();
    if ((m_nState == DSST_FINISHED || m_nState == DSST_CASTLE_SIEGE) && !IsInReadyToDominionSiege()) {
        return 0;
    }
    L2_GUARD(m_StartLock);
    for (CDominion* const dominion : m_lstStart) {
        if (dominion != nullptr && dominion->IsPledgeInSiege(pledgeId)) {
            return dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
        }
    }
    return 0;
}

CDominionManager::DominionList CDominionManager::GetDominionList() const { return m_lstDominion; }

void CDominionManager::StandBySiege() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_StartLock);
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->StandBySiege();
            }
        }
    }
    ChangeState(DSST_STANDBY, true);
}

void CDominionManager::PauseSiege() {
    L2_GUARD_FUNCTION();
    if (m_bPaused) {
        return;
    }

    m_bPaused = true;
    m_tPausedTime = runtime::crt::Time32Now();
    L2_GUARD(m_StartLock);
    for (CDominion* const dominion : m_lstStart) {
        if (dominion != nullptr) {
            dominion->PauseSiege();
        }
    }
}

void CDominionManager::StartSiege() {
    L2_GUARD_FUNCTION();
    SetSiegeTime();
    ChangeState(DSST_DOMINION_SIEGE, true);
    m_bSentDominionChannelSet = false;
    m_bCanceledRenamed = false;
    {
        L2_GUARD(m_StartLock);
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->SetSiegeRole(true);
            }
        }
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->StartSiege();
            }
        }
    }
    m_tSiegeStartTime = runtime::crt::Time32Now();
}

void CDominionManager::ResumeSiege() {
    L2_GUARD_FUNCTION();
    if (!m_bPaused && !m_bServerDownResume) {
        return;
    }

    SetSiegeTime();
    m_bPaused = false;
    {
        L2_GUARD(m_StartLock);
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->SetSiegeRole(true);
            }
        }
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->ResumeSiege();
            }
        }
    }
    ChangeState(DSST_DOMINION_SIEGE, true);
    m_bServerDownResume = false;
}

void CDominionManager::EndSiege() {
    L2_GUARD_FUNCTION();
    ChangeState(DSST_FINISHED, true);
    {
        L2_GUARD(m_StartLock);
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->SetSiegeRole(false);
            }
        }
        for (CDominion* const dominion : m_lstStart) {
            if (dominion != nullptr) {
                dominion->EndSiege();
            }
        }
    }
    m_bServerDownResume = false;
    m_tSiegeElapsedTime = 0;
    SetSiegeEndTime(runtime::crt::Time32Now(), true);
    m_bNextSiegeTimeSet = false;
}

void CDominionManager::ProcessExtraEndJob() {
    L2_GUARD_FUNCTION();
    if (m_bCanceledRenamed) {
        return;
    }

    const char16_t emptyText[] = u"";
    SendDominionMessage(0xaea, 0, EST_STRING, emptyText, false);
    {
        L2_GUARD(m_StartLock);
        m_bCanceledRenamed = true;
        for (CDominion* const dominion : m_lstStart) {
            if (dominion == nullptr) {
                // Log.Add(LOG_ERROR, L"Invalid dominion!")
                // L2_LOG_ERROR(Log, L"Invalid dominion!")
                L2_LOG_ERROR(
                    Log,
                    "Territory-war cleanup encountered a null territory in the active list; this entry is skipped, and "
                    "global alias cleanup will still be requested."
                );
                continue;
            }
            dominion->CancelAllRenamed();
            dominion->ReleaseEntry();
        }
    }
    db.RequestDeleteAllDominionRenamed();
}

void CDominionManager::SendDominionChannelSet(const bool enabled) {
    L2_GUARD_FUNCTION();
    if (m_bSentDominionChannelSet) {
        return;
    }

    {
        L2_GUARD(m_StartLock);
        for (CDominion* const dominion : m_lstStart) {
            if (dominion == nullptr || dominion->m_pSiegeEntry == nullptr) {
                // Log.Add(LOG_ERROR, L"Invalid dominion or Entry!")
                // L2_LOG_ERROR(Log, L"Invalid dominion or Entry!")
                L2_LOG_ERROR(
                    Log,
                    "Territory-war channel %s skipped for territory %d: %s; this notification pass will still be marked "
                    "sent.",
                    enabled ? "enable" : "disable",
                    dominion != nullptr && dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0,
                    dominion == nullptr ? "territory is missing" : "participant registry is missing"
                );
                continue;
            }
            dominion->m_pSiegeEntry->SendDominionChannelSet(enabled);
        }
    }
    m_bSentDominionChannelSet = true;
}

void CDominionManager::SendDominionMessageToMembers(const int messageType) {
    L2_GUARD_FUNCTION();
    if (m_lstStart.empty()) {
        return;
    }

    int messageId = 0;
    int argument = 0;
    switch (messageType) {
        case 2:
            messageId = 0xb62;
            break;
        case 3:
            messageId = 0x963;
            break;
        case 4:
            messageId = 0x964;
            break;
        case 5:
            messageId = 0x965;
            break;
        case 6:
            messageId = 0x966;
            break;
        case 8:
            messageId = 0xaee;
            argument = 1;
            break;
        case 9:
            messageId = 0xaef;
            argument = 30;
            break;
        case 10:
            messageId = 0xaef;
            argument = 10;
            break;
        case 11:
            messageId = 0xaef;
            argument = 5;
            break;
        case 12:
            messageId = 0xaef;
            argument = 1;
            break;
        case 13:
            messageId = 0xb54;
            argument = 30;
            break;
        case 14:
            messageId = 0xb54;
            argument = 10;
            break;
        case 15:
            messageId = 0xb54;
            argument = 9;
            break;
        case 16:
            messageId = 0xb54;
            argument = 8;
            break;
        case 17:
            messageId = 0xb54;
            argument = 7;
            break;
        case 18:
            messageId = 0xb54;
            argument = 6;
            break;
        case 19:
            messageId = 0xb54;
            argument = 5;
            break;
        case 20:
            messageId = 0xb54;
            argument = 4;
            break;
        case 21:
            messageId = 0xb54;
            argument = 3;
            break;
        case 22:
            messageId = 0xb54;
            argument = 2;
            break;
        case 23:
            messageId = 0xb54;
            argument = 1;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"SendToMembers message is invalid. %d",
            //     messageType)
            // L2_LOG_ERROR(Log,
            //     L"SendToMembers message is invalid. %d",
            //     messageType)
            L2_LOG_ERROR(
                Log,
                "Cannot send territory-war member announcement type %d: the type is unsupported; no announcement is sent.",
                messageType
            );
            return;
    }

    L2_GUARD(m_StartLock);
    for (CDominion* const dominion : m_lstStart) {
        if (dominion != nullptr && dominion->m_pSiegeEntry != nullptr) {
            dominion->m_pSiegeEntry->SendDominionMessage2(messageId, argument);
        }
    }
}

void CDominionManager::SendDominionMessage(
    const int messageId, const int argument, const eSMsgType argumentType, const char16_t* const text, const bool showEffect
) {
    L2_GUARD_FUNCTION();
    if (m_lstStart.empty()) {
        return;
    }

    L2_GUARD(m_StartLock);
    for (CDominion* const dominion : m_lstStart) {
        if (dominion != nullptr && dominion->m_pSiegeEntry != nullptr) {
            dominion->m_pSiegeEntry->SendDominionMessage(messageId, argument, argumentType, text, showEffect);
        }
    }
}

void CDominionManager::SendDominionScriptEvent(const int eventType, const int npcId, const int dominionId) {
    L2_GUARD_FUNCTION();
    if (m_lstStart.empty()) {
        return;
    }

    L2_GUARD(m_StartLock);
    for (CDominion* const dominion : m_lstStart) {
        if (dominion != nullptr && dominion->m_pSiegeEntry != nullptr) {
            dominion->m_pSiegeEntry->SendDominionScriptEvent(eventType, npcId, dominionId);
        }
    }

    const int now = runtime::crt::Time32Now();
    if (now == m_nScriptEventSaveTick) {
        ++m_nScriptEventSaveCount;
        if (m_nScriptEventSaveCount % 20 == 19) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Script event count reached %d. Arg 1/2/3 %d/%d/%d",
            //     m_nScriptEventSaveCount,
            //     eventType,
            //     npcId,
            //     dominionId)
            // L2_LOG_ERROR(Log,
            //     L"Script event count reached %d. Arg 1/2/3 %d/%d/%d",
            //     m_nScriptEventSaveCount,
            //     eventType,
            //     npcId,
            //     dominionId)
            L2_LOG_WARN(
                Log,
                "Territory-war script-event burst: %lld calls in the current second (epoch %d); latest event %d, arguments "
                "%d and %d. Dispatch has already been attempted.",
                static_cast<long long>(m_nScriptEventSaveCount) + 1, now, eventType, npcId, dominionId
            );
        }
    } else {
        m_nScriptEventSaveTick = now;
        m_nScriptEventSaveCount = 0;
    }
}

void CDominionManager::SendDominionRegistry(const int userId, const int dominionId) {
    // Current L2Server 0x006de078.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }

    CDominion* const requestedDominion = g_ResidenceDB.GetDominion(dominionId);
    if (requestedDominion == nullptr) {
        return;
    }

    int registeredPledgeCount = 0;
    int registeredUserCount = 0;
    if (requestedDominion->m_pSiegeEntry != nullptr) {
        registeredPledgeCount = static_cast<int>(requestedDominion->m_pSiegeEntry->m_vPledgeList.size());
        registeredUserCount = static_cast<int>(requestedDominion->m_pSiegeEntry->m_vUserList.size());
    }

    CPledge* const owner = requestedDominion->GetResidenceOwnerPledge();
    if (owner == nullptr) {
        // Log.Add(LOG_ERROR, L"Invalid pledge!")
        // L2_LOG_ERROR(Log, L"Invalid pledge!")
        L2_LOG_WARN(
            Log,
            "Cannot send territory %d registry to character SID %u: the territory has no available owner clan; no reply is "
            "sent.",
            dominionId, static_cast<unsigned>(userId)
        );
        return;
    }

    CAlliance* const alliance = owner->GetAlliance();
    const std::int32_t now = runtime::crt::Time32Now();
    std::array<char, 0x5000> dominionPayload{};
    int dominionPayloadSize = 0;
    int dominionCount = 0;

    const DominionList dominions = GetDominionList();
    for (CDominion* const dominion : dominions) {
        if (dominion == nullptr) {
            continue;
        }

        const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
        int assembled =
            ::Assemble(dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "d", residenceId);
        if (assembled > 0) {
            dominionPayloadSize += assembled;
        }

        std::array<char, 0x5000> ownThingPayload{};
        int ownThingPayloadSize = 0;
        int ownThingCount = 0;
        const CDominion::OwnThingList ownThings = dominion->GetOwnThingList();
        for (const auto& entry : ownThings) {
            if (entry.first == nullptr) {
                continue;
            }
            assembled = ::Assemble(
                ownThingPayload.data() + ownThingPayloadSize, 0x5000 - ownThingPayloadSize, "d",
                entry.first->GetOriginalResidenceID()
            );
            if (assembled > 0) {
                ownThingPayloadSize += assembled;
            }
            ++ownThingCount;
        }

        if (ownThingCount < 1) {
            assembled = ::Assemble(dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "d", 0);
        } else {
            assembled = ::Assemble(
                dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "db", ownThingCount,
                ownThingPayloadSize, ownThingPayload.data()
            );
        }
        if (assembled > 0) {
            dominionPayloadSize += assembled;
        }
        ++dominionCount;
    }

    const char16_t* const allianceName = alliance != nullptr ? alliance->m_data->m_sAllianceName : u"";
    CUserSocket* const socket = user->GetSocket();
    const int userPledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    const int nextSiegeTime = m_tNextSiegeTime;
    const int isPledgeMaster = user->IsPledgeMaster() ? 1 : 0;
    const int pledgeDominionId = CDominionManager::Inst()->GetPledgeDominionSiegeID(userPledgeId);
    const char16_t* const ownerMasterNameRaw = owner->GetMasterName();
    const char16_t* const ownerNameRaw = owner->GetPledgeName();
    const std::u16string ownerMasterName = std::u16string(ownerMasterNameRaw);
    const std::u16string ownerName = std::u16string(ownerNameRaw);

    if (dominionCount < 1) {
        socket->Send(
            "chdSSSdddddddd", 0xfe, 0x91, dominionId, ownerName.c_str(), ownerMasterName.c_str(), allianceName,
            registeredPledgeCount, registeredUserCount, nextSiegeTime, now, pledgeDominionId,
            CDominionManager::Inst()->GetUserDominionSiegeID(static_cast<int>(user->GetDBID())), isPledgeMaster, 0
        );
    } else {
        socket->Send(
            "chdSSSddddddddb", 0xfe, 0x91, dominionId, ownerName.c_str(), ownerMasterName.c_str(), allianceName,
            registeredPledgeCount, registeredUserCount, nextSiegeTime, now, pledgeDominionId,
            CDominionManager::Inst()->GetUserDominionSiegeID(static_cast<int>(user->GetDBID())), isPledgeMaster,
            // Current 0x006de61f passes the list count in the eighth
            // `d` lane. The following `b` consumes only payload size and
            // pointer. An extra zero here shifts both variadic operands.
            dominionCount, dominionPayloadSize, dominionPayload.data()
        );
    }
}

void CDominionManager::BroadcastDominionMessageToAll(const int messageType) {
    L2_GUARD_FUNCTION();
    int messageId = 0;
    switch (messageType) {
        case 0:
            messageId = SystemMessageId::CLAN_LEADER_DECLARED_TERRITORY_LORD;
            break;
        case 1:
            messageId = SystemMessageId::TERRITORY_WAR_REGISTRATION_ENDED;
            break;
        case 6:
            messageId = SystemMessageId::TERRITORY_WAR_STARTED;
            break;
        case 0x18:
            messageId = SystemMessageId::TERRITORY_WAR_ENDED;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"Broadcast message is invalid. %d",
            //     messageType)
            // L2_LOG_ERROR(Log,
            //     L"Broadcast message is invalid. %d",
            //     messageType)
            L2_LOG_ERROR(
                Log,
                "Cannot broadcast territory-war announcement type %d to all players: the type is unsupported; no "
                "announcement is sent.",
                messageType
            );
            return;
    }

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user->IsInRealWorld()) {
            user->GetSocket()->SendSystemMessage(static_cast<unsigned int>(messageId));
        }
    });
}

void CDominionManager::BroadcastDominionMessage(const int messageType) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_StartLock);
    for (CDominion* const dominion : m_lstStart) {
        if (dominion == nullptr) {
            // Log.Add(LOG_ERROR, L"Invalid dominion!")
            // L2_LOG_ERROR(Log, L"Invalid dominion!")
            L2_LOG_ERROR(
                Log,
                "Territory-war announcement type %d skipped for a null territory in the active list; other territories are "
                "still processed.",
                messageType
            );
        } else {
            dominion->RequestBroadcastMessage(messageType);
        }
    }
}

void CDominionManager::AnnounceMessage(const int messageType, const int targetType) {
    L2_GUARD_FUNCTION();
    if (m_nAnnouncedStatus == messageType) {
        return;
    }

    m_nAnnouncedStatus = messageType;
    if (targetType == 0) {
        BroadcastDominionMessageToAll(messageType);
    } else if (targetType == 1) {
        BroadcastDominionMessage(messageType);
    } else if (targetType == 2) {
        SendDominionMessageToMembers(messageType);
    }
}

void CDominionManager::StartRegistration() {
    L2_GUARD_FUNCTION();
    SetNextSiegeTime(AdjustNextSiegeTime(), false);
    m_bNextSiegeTimeSet = true;

    std::tm nextLocal;
    std::tm previousLocal;
    std::memset(&nextLocal, 0xff, sizeof(nextLocal));
    std::memset(&previousLocal, 0xff, sizeof(previousLocal));
    // static_cast<void>(runtime::crt::LocalTime32(
    //     m_tNextSiegeTime,
    //     nextLocal))
    const bool nextLocalValid = runtime::crt::LocalTime32(m_tNextSiegeTime, nextLocal);
    // static_cast<void>(runtime::crt::LocalTime32(
    //     m_tPreviousSiegeEnd,
    //     previousLocal))
    const bool previousLocalValid = runtime::crt::LocalTime32(m_tPreviousSiegeEnd, previousLocal);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] ended at %d.%d/%d.%d:%d / next at %d.%d/%d.%d:%d",
    //     previousLocal.tm_year + 1900,
    //     previousLocal.tm_mon + 1,
    //     previousLocal.tm_mday,
    //     previousLocal.tm_hour,
    //     previousLocal.tm_min,
    //     nextLocal.tm_year + 1900,
    //     nextLocal.tm_mon + 1,
    //     nextLocal.tm_mday,
    //     nextLocal.tm_hour,
    //     nextLocal.tm_min)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] ended at %d.%d/%d.%d:%d / next at %d.%d/%d.%d:%d",
    //     previousLocal.tm_year + 1900,
    //     previousLocal.tm_mon + 1,
    //     previousLocal.tm_mday,
    //     previousLocal.tm_hour,
    //     previousLocal.tm_min,
    //     nextLocal.tm_year + 1900,
    //     nextLocal.tm_mon + 1,
    //     nextLocal.tm_mday,
    //     nextLocal.tm_hour,
    //     nextLocal.tm_min)
    if (previousLocalValid && nextLocalValid) {
        L2_LOG_INFO(
            Log,
            "Territory-war registration is opening: previous war ended %04d-%02d-%02d %02d:%02d; next war scheduled for "
            "%04d-%02d-%02d %02d:%02d (server local time).",
            previousLocal.tm_year + 1900, previousLocal.tm_mon + 1, previousLocal.tm_mday, previousLocal.tm_hour,
            previousLocal.tm_min, nextLocal.tm_year + 1900, nextLocal.tm_mon + 1, nextLocal.tm_mday, nextLocal.tm_hour,
            nextLocal.tm_min
        );
    } else {
        L2_LOG_WARN(
            Log,
            "Territory-war registration is opening with previous end epoch %d and next war epoch %d; local-time conversion "
            "failed for %s. Registration still proceeds.",
            m_tPreviousSiegeEnd, m_tNextSiegeTime,
            previousLocalValid ? "the next war time" : (nextLocalValid ? "the previous end time" : "both times")
        );
    }
    ChangeState(DSST_REGISTRATION, true);
    AnnounceMessage(0x19, 1);
}

int CDominionManager::CheckStartDominionRegistration(const int reason, const bool force) {
    L2_GUARD_FUNCTION();
    static_cast<void>(reason);

    {
        L2_GUARD(m_StartLock);
        m_lstStart.clear();
    }

    bool allReady = true;
    {
        L2_GUARD(m_DominionLock);
        for (CDominion* const dominion : m_lstDominion) {
            if (dominion == nullptr) {
                // Log.Add(LOG_ERROR, L"Invalid dominion!")
                // L2_LOG_ERROR(Log, L"Invalid dominion!")
                L2_LOG_ERROR(
                    Log,
                    "Territory-war registration check encountered a null territory (reason %d, forced %s); this entry is "
                    "skipped without changing readiness.",
                    reason, force ? "yes" : "no"
                );
                continue;
            }

            CPledge* const owner = dominion->GetResidenceOwnerPledge();
            const ResidenceState state = dominion->m_data != nullptr ? dominion->m_data->m_ResidenceState : RS_MAX;
            if (force) {
                if (owner != nullptr) {
                    AddStart(dominion);
                }
            } else if (owner == nullptr) {
                if (state != RS_PASSED_CASTLE_SIEGE) {
                    allReady = false;
                }
            } else if (state == RS_PASSED_CASTLE_SIEGE || state == RS_REGISTRATION) {
                AddStart(dominion);
            } else {
                allReady = false;
            }
        }
    }

    DominionList startList;
    {
        L2_GUARD(m_StartLock);
        startList = m_lstStart;
    }
    if (startList.size() < 2) {
        return 1;
    }
    if (!allReady && !force) {
        return 2;
    }

    StartRegistration();
    for (CDominion* const dominion : startList) {
        if (dominion == nullptr) {
            // Log.Add(LOG_ERROR, L"Invalid dominion!")
            // L2_LOG_ERROR(Log, L"Invalid dominion!")
            L2_LOG_ERROR(
                Log,
                "Territory-war registration has opened, but an active-list entry has no territory; that entry is skipped."
            );
        } else {
            dominion->StartRegistration();
        }
    }
    {
        L2_GUARD(m_DominionLock);
        for (CDominion* const dominion : m_lstDominion) {
            if (dominion != nullptr && dominion->GetResidenceOwnerPledge() == nullptr) {
                dominion->ChangeDominionState(RS_ANNOUNCING);
            }
        }
    }
    return 0;
}

void CDominionManager::SiegeTimerExpired(const int id) {
    L2_GUARD_FUNCTION();
    static_cast<void>(id);

    if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        PauseSiege();
        AddTimer(1000, 0);
        return;
    }

    const int now = runtime::crt::Time32Now();
    int remaining = m_tSiegeStartTime + m_tSiegeTime - now;
    if (m_bServerDownResume) {
        m_tSiegeStartTime = now;
        ResumeSiege();
        AddTimer(1000, 0);
        return;
    }
    if (m_bPaused) {
        if (m_tPausedTime <= now - BP::_DOMINION_SIEGE_TIME || now < m_tPausedTime) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Paused time is invalid. time %d. now %d",
            //     m_tPausedTime,
            //     now)
            // L2_LOG_ERROR(Log,
            //     L"Paused time is invalid. time %d. now %d",
            //     m_tPausedTime,
            //     now)
            L2_LOG_WARN(
                Log,
                "Territory-war pause time %d is invalid at epoch %d (configured war duration %d seconds); the pause time is "
                "reset to now and resume continues.",
                m_tPausedTime, now, BP::_DOMINION_SIEGE_TIME
            );
            m_tPausedTime = now;
        }
        ResumeSiege();
        m_tSiegeTime += now - m_tPausedTime;
        AddTimer(1000, 0);
        return;
    }
    if (remaining < 1) {
        AnnounceMessage(0x18, 0);
        EndSiege();
        AddTimer(1000, 0);
        return;
    }

    int messageType = 0;
    if (remaining < 2) {
        messageType = 0x17;
    } else if (remaining < 3) {
        messageType = 0x16;
    } else if (remaining < 4) {
        messageType = 0x15;
    } else if (remaining < 5) {
        messageType = 0x14;
    } else if (remaining < 6) {
        messageType = 0x13;
    } else if (remaining < 7) {
        messageType = 0x12;
    } else if (remaining < 8) {
        messageType = 0x11;
    } else if (remaining < 9) {
        messageType = 0x10;
    } else if (remaining < 10) {
        messageType = 0x0f;
    } else if (remaining < 11) {
        messageType = 0x0e;
    } else if (remaining < 31) {
        messageType = 0x0d;
    } else if (remaining < 61) {
        messageType = 0x0c;
    } else if (remaining < 301) {
        messageType = 0x0b;
    } else if (remaining < 601) {
        messageType = 0x0a;
    } else if (remaining < 1801) {
        messageType = 9;
    } else if (remaining < 3601) {
        messageType = 8;
    }
    if (messageType != 0) {
        AnnounceMessage(messageType, 2);
    }

    if (m_nSaveTick == (m_nSaveTick / 30) * 30) {
        m_tSiegeElapsedTime += 30;
        db.RequestSaveDominionSiege(static_cast<int>(m_nState), m_tPreviousSiegeEnd, m_tSiegeElapsedTime, m_tNextSiegeTime);
    }
    ++m_nSaveTick;
    AddTimer(1000, 0);
}

void CDominionManager::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();
    static_cast<void>(id);
    const int now = runtime::crt::Time32Now();

    if (m_nState == DSST_FINISHED) {
        if (m_tPreviousSiegeEnd + 600 < now) {
            SendDominionChannelSet(false);
            ProcessExtraEndJob();
        }
    } else if (m_nState == DSST_CASTLE_SIEGE) {
        bool registrationFound = false;
        {
            L2_GUARD(m_DominionLock);
            for (CDominion* const dominion : m_lstDominion) {
                if (dominion != nullptr && dominion->m_data != nullptr &&
                    dominion->m_data->m_ResidenceState == RS_REGISTRATION) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"dominion %d is waiting registration, but global "
                    //     L"state is castle-siege. count [%d/360]",
                    //     dominion->m_data->m_nResidenceID,
                    //     m_nErrorCount)
                    // L2_LOG_ERROR(Log,
                    //     L"dominion %d is waiting registration, but global "
                    //     L"state is castle-siege. count [%d/360]",
                    //     dominion->m_data->m_nResidenceID,
                    //     m_nErrorCount)
                    // Noise: The 10-second timer may repeat this mismatch. Report the first occurrence, every 30 accumulated
                    // checks, and the transition threshold without changing the recovery counter.
                    if (m_nErrorCount == 0 || m_nErrorCount % 30 == 0 || m_nErrorCount >= 360) {
                        L2_LOG_WARN(
                            Log,
                            "Territory %d is awaiting registration while the manager is in CASTLE_SIEGE (mismatch found in "
                            "%lld checks, including this one); the manager switches to REGISTRATION after the count exceeds "
                            "360.",
                            dominion->m_data->m_nResidenceID, static_cast<long long>(m_nErrorCount) + 1
                        );
                    }
                    registrationFound = true;
                }
            }
        }
        if (registrationFound) {
            ++m_nErrorCount;
        }
        if (m_nErrorCount > 360) {
            m_nErrorCount = 0;
            ChangeState(DSST_REGISTRATION, true);
        }
    } else if (m_nState == DSST_REGISTRATION) {
        if (m_tNextSiegeTime - BP::_DOMINION_STANDBY_TIME < now) {
            AnnounceMessage(1, 0);
            StandBySiege();
        }
    } else if (m_nState == DSST_STANDBY) {
        if (m_tNextSiegeTime < now) {
            StartSiege();
            AnnounceMessage(6, 0);
            AnnounceMessage(7, 1);
        } else if (m_tNextSiegeTime - 60 < now) {
            AnnounceMessage(5, 2);
        } else if (m_tNextSiegeTime - 300 < now) {
            AnnounceMessage(4, 2);
        } else if (m_tNextSiegeTime - 600 < now) {
            AnnounceMessage(3, 2);
        } else if (m_tNextSiegeTime - 1200 < now) {
            SendDominionChannelSet(true);
            AnnounceMessage(2, 2);
        }
    } else if (m_nState == DSST_DOMINION_SIEGE) {
        SiegeTimerExpired(id);
        return;
    }

    AddTimer(10000, 0);
}
