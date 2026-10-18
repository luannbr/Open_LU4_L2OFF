#include "Agit.h"
#include "Alliance.h"
#include "BattleCamp.h"
#include "CConfig.h"
#include "CWorld.h"
#include "Castle.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Dominion.h"
#include "Fortress.h"
#include "HolyThing.h"
#include "HtmlBuilder.h"
#include "Item.h"
#include "MiniGameAgit.h"
#include "MultiPartyCommandChannel.h"
#include "ObjectDB.h"
#include "OlympiadStuff.h"
#include "OwnThing.h"
#include "PVPMatch.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeGameSystem.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Protocol/src/SystemMessageIds.h"
#include "RelationManager.h"
#include "Relay.h"
#include "Residence.h"
#include "SSQ.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "SiegeAgit.h"
#include "SignBoard.h"
#include "TeamBattleAgit.h"
#include "User.h"
#include "UserSocket.h"
#include "WHeroSystem.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {


const wchar_t* ObserverAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 16>& storage) {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(source.data());
#else
    std::size_t length = 0;
    while (length < source.size() && source[length] != u'\0') {
        ++length;
    }
    const std::wstring converted = ::Utf16ToWide(std::u16string_view(source.data(), length));
    const std::size_t copyLength = std::min(converted.size(), storage.size() - 1U);
    std::copy_n(converted.data(), copyLength, storage.data());
    storage[copyLength] = L'\0';
    return storage.data();
#endif
}

CSPointer<CPledge> FindPledgeByPacketName(const int worldId, const wchar_t* const pledgeName) {
    std::array<SharedUtilsWideChar, 32> targetName{};
    if (pledgeName == nullptr) {
        return {};
    }
#if WCHAR_MAX <= 0xffff
    const std::size_t length = std::min<std::size_t>(std::wcslen(pledgeName), targetName.size() - 1U);
    std::copy_n(reinterpret_cast<const SharedUtilsWideChar*>(pledgeName), length, targetName.data());
    targetName[length] = SharedUtilsWideChar{};
#else
    const std::u16string converted = WideToUtf16(std::wstring_view(pledgeName));
    const std::size_t length = std::min(converted.size(), targetName.size() - 1U);
    std::copy_n(converted.data(), length, targetName.data());
    targetName[length] = SharedUtilsWideChar{};
#endif
    return CPledge::FindPledge(worldId, targetName.data());
}

} // namespace

bool User::CanDismissPledge() {
    // Current L2Server 0x00b9f6b0. The original physical owner is
    // user_pledge.cpp (TotalChecker2015 CodeView object structure).
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    std::int32_t proposeTime = runtime::SubtractInt32Wrapping(m_nPledgeDismissTime, BP::_PLEDGE_DISMISS_TIMEOUT);

    CPledge* const pledge = GetPledge();
    if (pledge != nullptr) {
        proposeTime = runtime::Int32FromBits(
            pledge->GetDismissReservedTime() - static_cast<std::uint32_t>(BP::_PLEDGE_DISMISS_TIMEOUT)
        );
    }

    return runtime::SubtractInt32Wrapping(now, proposeTime) >= BP::_PLEDGE_DISMISS_PROPOSE_PENALTY_TIMEOUT;
}

void User::RenameByDominionSiege(const bool renamed, const bool saveToDatabase) {
    L2_GUARD_FUNCTION();

    if (renamed && !CDominionManager::Inst()->IsInReadyToDominionSiege()) {
        return;
    }
    if (renamed == m_bDominionRenamed) {
        return;
    }

    if (saveToDatabase) {
        db.RequestSaveDominionRenamed(*this, renamed ? 1 : 0);
    }
    m_bDominionRenamed = renamed;
    SendDominionRenamedToParty(renamed);
    SendDominionStartEnd(true, nullptr);
    ParamChanged(PCB_USER | PCB_CHAR_DOMINION);
}

bool User::IsFrozen(bool sendMessage) {
    if (m_bIsFreezedByGM && sendMessage) {
        GetSocket()->Send(
            "chcch", 0x62, SystemMessageId::IMMOBILIZED_WITH_MINUTES_REMAINING, 1, 0x15,
            static_cast<int>(static_cast<std::int16_t>(dwFreezeRemainTime / 60000U))
        );
    }

    return m_data->m_bIsFreezed || m_bIsObserving || m_bIsFreezedByGM || m_EventMatchUser.m_bIsPause ||
        m_DuelUserInfo.IsDuelDead(this);
}

void User::ObserverStart(const int x, const int y, const int z, const int yaw, const int pitch, const int period) {
    // Current L2Server 0x00ba0328.
    L2_GUARD_FUNCTION();

    if (m_pObserverRemains != nullptr) {
        SendSystemMessage(u"SYS", u"781");
        return;
    }
    if (IsShopManaging() || IsShopOpen()) {
        return;
    }
    if (IsOlympiadObserver()) {
        SendSystemMessage(SystemMessageId::OLYMPIAD_PARTICIPANT_CANNOT_OBSERVE);
        return;
    }
    if (g_PledgeGameSystem.IsRegisteredUser(this)) {
        SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        return;
    }
    if (m_data->m_bGMInvisible) {
        if (!IsBuilder()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] not GM is in invisi mode.",
            //     "user_pledge.cpp",
            //     0xc04)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] not GM is in invisi mode.",
            //     "user_pledge.cpp",
            //     0xc04)
            L2_LOG_ERROR(
                Log, "Character %u has GM invisibility enabled without builder access; entering observer mode is rejected.",
                static_cast<unsigned>(GetDBID())
            );
        } else {
            SendSystemMessage(u"SYS", u"disable invisi mode first");
        }
        return;
    }
    if (!CWorld::Inst().IsValidPosition(x, y)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     x,
        //     y,
        //     "user_pledge.cpp",
        //     0xc0c)
        // L2_LOG_ERROR(Log,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     x,
        //     y,
        //     "user_pledge.cpp",
        //     0xc0c)
        L2_LOG_ERROR(
            Log, "Character %u cannot enter observer mode at (%d, %d): the world position is invalid.",
            static_cast<unsigned>(GetDBID()), x, y
        );
        return;
    }

    DoNothing();
    CObserverRemains* const observerRemains = new CObserverRemains();
    observerRemains->SetUser(this);

    const bool observingStarted = [&] {
        L2_GUARD(*m_lock);
        if (!HaveSummon() && !HavePet()) {
            m_data->m_bGMInvisible = true;
            m_pObserverRemains = observerRemains;
            m_bIsObserving = true;
            m_vOriginalPositionBeforeObserving = GetPos();
            CIOObject::SetTimeStamp(&m_nObservingStartTime);
            m_nObservingPeriod = period;
            m_nNextObservingAnnounceTime = 600;
            return true;
        }
        return false;
    }();
    if (observingStarted) {

        DespawnAllCubic(true);
        ToggleSkillAllOff(true);
        AuraSkillAllOff(true);
        GetSocket()->Send("cddddd", 0xeb, x, y, z, yaw, pitch);
        CWorld::Inst().LeaveWorld(this, FVector(x, y, z), false);

        m_bStopTimeByObserving = false;
        if (!m_bStopTime) {
            m_bStopTime = true;
            m_bStopTimeByObserving = true;
            GetSocket()->SetSitTimeStart();
        } else {
            GetSocket()->ShowRemainTime();
        }
        return;
    }

    // Literal current behavior: the failure path reads the member rather
    // than the freshly allocated local remains object.
    CObserverRemains* const remains = m_pObserverRemains;
    m_pObserverRemains = nullptr;
    if (remains != nullptr) {
        remains->RemoveRemains();
        L2_RELEASE(remains, ObjectRefType::Anonymous);
        remains->Delete();
    }
}

void User::ObserverEnd() {
    L2_GUARD_FUNCTION();

    CObserverRemains* remains;
    {
        L2_GUARD(*m_lock);
        m_data->m_bGMInvisible = false;
        remains = m_pObserverRemains;
        m_pObserverRemains = nullptr;
        m_bIsObserving = false;
    }

    if (remains != nullptr) {
        remains->RemoveRemains();
        L2_RELEASE(remains, ObjectRefType::Anonymous);
        remains->Delete();
    }

    const FVector originalPosition = m_vOriginalPositionBeforeObserving;
    GetSocket()->Send(
        "cddd", 0xec, runtime::Win64CvttDoubleToInt32(originalPosition.x),
        runtime::Win64CvttDoubleToInt32(originalPosition.y), runtime::Win64CvttDoubleToInt32(originalPosition.z)
    );
    CWorld::Inst().LeaveWorld(this, originalPosition, false);

    if (!m_bStopTime) {
        GetSocket()->ShowRemainTime();
    } else if (m_bStopTimeByObserving) {
        m_bStopTime = false;
        GetSocket()->SetEndSitTimeStart();
    }
    m_bStopTimeByObserving = false;

    const std::int64_t adenaAmount = [&] {
        L2_READ_GUARD(m_inv);
        return m_inv.GetAdenaAmount();
    }();

    const FVector position = GetPos();
    const std::int32_t now = runtime::crt::Time32Now();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = ObserverAccountName(m_sAccountName, accountNameStorage);
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     0x34c,
    //     GetDBID(),
    //     GetAccountID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     m_data->m_Race,
    //     m_data->m_Sex,
    //     m_data->m_nClass,
    //     m_data->m_nLevel,
    //     now - m_nObservingStartTime,
    //     adenaAmount,
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        0x34c, GetDBID(), GetAccountID(), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), m_data->m_Race,
        m_data->m_Sex, m_data->m_nClass, m_data->m_nLevel, now - m_nObservingStartTime, adenaAmount, GetName(), accountName
    );
}

bool User::IsPledgeMaster() {
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    return pledge != nullptr && pledge->GetMasterDbId() == GetDBID();
}

bool User::OnStartPledgeWarPacket(const wchar_t* const pledgeName) {
    // Current L2Server 0x00ba3b30, original owner user_pledge.cpp.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    CUserSocket* const socket = GetSocket();
    if (pledge == nullptr) {
        SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
        return false;
    }
    if (!HavePledgePower(PP_DECLARE_WAR)) {
        SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }

    CSPointer<CPledge> enemyPledge = FindPledgeByPacketName(m_data->m_nWorldId, pledgeName);
    if (!enemyPledge) {
        SendSystemMessage(SystemMessageId::CANNOT_DECLARE_WAR_ON_NONEXISTENT_OR_LONG_INACTIVE_CLAN);
        return false;
    }
    if (!pledge->CanDeclareWar(enemyPledge, *socket)) {
        return false;
    }

    DxDeclareWar packet(
        static_cast<int>(pledge->Id()), static_cast<int>(pledge->GetDbId()), static_cast<int>(enemyPledge->Id()),
        static_cast<int>(enemyPledge->GetDbId()), static_cast<int>(GetServerId()), runtime::crt::Time32Now()
    );
    db.RequestTPacketProcess(m_data->m_nWorldId, 0x1f, static_cast<int>(sizeof(packet)), packet);
    return false;
}

bool User::CanStopPledgeWar(const wchar_t* const pledgeName) {
    // Current L2Server 0x00ba3ddc, original owner user_pledge.cpp.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    CUserSocket* const socket = GetSocket();
    if (pledge == nullptr) {
        SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
        return false;
    }
    if (!HavePledgePower(PP_DECLARE_WAR)) {
        SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }

    CSPointer<CPledge> enemyPledge = FindPledgeByPacketName(m_data->m_nWorldId, pledgeName);
    if (!enemyPledge) {
        SendSystemMessage(SystemMessageId::TARGET_CLAN_DOES_NOT_EXIST);
        return false;
    }
    if (!pledge->DidDeclareWar(static_cast<int>(enemyPledge->Id()))) {
        socket->SendSystemMessage_S(SystemMessageId::WAR_NOT_DECLARED_ON_NAMED_CLAN, WideToUtf16(pledgeName).c_str());
        return false;
    }
    if (pledge->IsInCombatMode()) {
        SendSystemMessage(SystemMessageId::CANNOT_CANCEL_WAR_DECLARATION_CLAN_MEMBER_IN_COMBAT);
        return false;
    }

    int transaction = 3;
    if (!pledge->BeginTransaction(&transaction)) {
        SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        return false;
    }
    return true;
}

bool User::OnStopPledgeWarPacket(const wchar_t* const pledgeName) {
    // Current L2Server 0x00ba4050, original owner user_pledge.cpp.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    CSPointer<CPledge> enemyPledge = FindPledgeByPacketName(m_data->m_nWorldId, pledgeName);
    if (pledge == nullptr) {
        return false;
    }
    if (!enemyPledge) {
        pledge->EndTransaction();
        return false;
    }

    DxCancelWar packet(
        static_cast<int>(pledge->Id()), static_cast<int>(pledge->GetDbId()), static_cast<int>(enemyPledge->Id()),
        static_cast<int>(enemyPledge->GetDbId()), static_cast<int>(GetServerId()), runtime::crt::Time32Now(), true, 0
    );
    db.RequestTPacketProcess(m_data->m_nWorldId, 0x20, static_cast<int>(sizeof(packet)), packet);
    return false;
}

bool User::DropCastleGuard(CItem* const item, const unsigned int) {
    // Current L2Server 0x00ba4bf4, original owner user_pledge.cpp.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || pledge->GetCastle() == nullptr || !HavePledgePower(PP_MANAGE_MERCENARY)) {
        SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_POSITION_MERCENARIES);
        return false;
    }

    CCastle* const castle = pledge->GetCastle();
    CResidence* const residence = castle->CastCastle();
    const std::uint32_t itemClassId = static_cast<std::uint32_t>(item->m_nClassId);
    const auto guardItem = residence->m_mapResidenceGuardItem.find(itemClassId);
    if (guardItem == residence->m_mapResidenceGuardItem.end()) {
        SendSystemMessage(SystemMessageId::CANNOT_POSITION_MERCENARY_OUTSIDE_ITS_CASTLE_TERRITORY);
        return false;
    }

    ResidenceGuardInfo* const guardInfo = guardItem->second;
    const FVector position = GetPos();
    if (residence->m_OwnerRestartTerritory.IsInside(position, true)) {
        SendSystemMessage(SystemMessageId::CANNOT_POSITION_MERCENARY_OUTSIDE_ITS_CASTLE_TERRITORY);
        return false;
    }
    if (IsInRealWorld() && !residence->IsInResidence(position, false)) {
        SendSystemMessage(SystemMessageId::CANNOT_POSITION_MERCENARY_OUTSIDE_ITS_CASTLE_TERRITORY);
        return false;
    }
    if (residence->IsUnderSiege()) {
        SendSystemMessage(SystemMessageId::CANNOT_POSITION_MERCENARIES_DURING_SIEGE);
        return false;
    }

    if (SSQSystem::inst().mSharedInfo->mRoundNumber != 0 && SSQSystem::inst().mSharedInfo->m_nCanDropGuard == 0) {
        const ResidenceState state = residence->m_data != nullptr ? residence->m_data->m_ResidenceState : RS_MAX;
        if (state != RS_STANDBY) {
            SendSystemMessage(SystemMessageId::MERCENARY_PLACEMENT_AVAILABLE_BEFORE_SIEGE);
            return false;
        }
    }

    {
        L2_WRITE_GUARD(*residence);
        const unsigned int ssqType = static_cast<unsigned int>(static_cast<int>(guardInfo->cSSQType));
        const unsigned int sealOwner = static_cast<unsigned int>(SSQSystem::inst().GetSealOwner(SSQSeal::STRIFE));
        if (((ssqType >> (sealOwner & 0x1fU)) & 1U) == 0 || static_cast<std::uint8_t>(guardInfo->cSSQType) > 7U ||
            guardInfo->nGuardType < RGT_TELEPORTER || guardInfo->nGuardType > RGT_BOW) {
            SendSystemMessage(SystemMessageId::CANNOT_POSITION_MERCENARY_DUE_TO_SEAL_OF_STRIFE);
            return false;
        }

        auto guardCount = residence->m_GuardNumMap.find(static_cast<int>(itemClassId));
        if (guardCount == residence->m_GuardNumMap.end() ||
            static_cast<unsigned int>(guardCount->second) + 1U > guardInfo->nMaxNum) {
            SendSystemMessage(SystemMessageId::CANNOT_POSITION_MORE_OF_THIS_MERCENARY);
            return false;
        }

        for (const auto& guard : residence->m_mapResidenceGuardDataCheck) {
            const FVector& guardPosition = guard.first;
            const double xDistance = position.x - guardPosition.x;
            const double yDistance = position.y - guardPosition.y;
            const double zDistance = position.z - guardPosition.z;
            const double minimumDistanceSquared = guard.second < RGT_SWORD ? 10000.0 : 2500.0;
            if (xDistance * xDistance + yDistance * yDistance + zDistance * zDistance < minimumDistanceSquared) {
                SendSystemMessage(SystemMessageId::CANNOT_POSITION_MERCENARIES_TOO_CLOSE_TOGETHER);
                return false;
            }
        }

        ++guardCount->second;
        const FVector guardPosition{static_cast<double>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<double>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<double>(runtime::Win64CvttDoubleToInt32(position.z))};
        residence->m_mapResidenceGuardDataCheck.emplace(guardPosition, guardInfo->nGuardType);

        const unsigned int residenceId =
            residence->m_data != nullptr ? static_cast<unsigned int>(residence->m_data->m_nResidenceID) : 0U;
        db.RequestDropCastleGuard(item, this, guardPosition, m_data->GetDir(), residenceId, guardInfo);
    }
    return true;
}

bool User::CheckMyPledgeDeclareWar(User* user) {
    // Current L2Server 0x00ba0dbc.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || user == nullptr) {
        return false;
    }
    CPledge* const targetPledge = user->GetPledge();
    return targetPledge != nullptr && pledge->DidDeclareWar(static_cast<int>(targetPledge->GetDbId()));
}

bool User::IsInPledgeWarAgainst(User* user) {
    // Current L2Server 0x00ba0ea4.
    L2_GUARD_FUNCTION();
    return user != nullptr && (CheckMyPledgeDeclareWar(user) || user->CheckMyPledgeDeclareWar(this));
}

bool User::IsInPledgeWarBoth(User* user) {
    // Current L2Server 0x00ba0f6c.
    L2_GUARD_FUNCTION();
    return user != nullptr && CheckMyPledgeDeclareWar(user) && user->CheckMyPledgeDeclareWar(this);
}

void User::HandleOwnThing(User*) {
    // Current L2Server 0x00ba3a7c. The killer parameter is intentionally
    // unused by the current body.
    L2_GUARD_FUNCTION();
    if (m_pOwnThing != nullptr) {
        DeleteOwnThing(true, false);
    }
}

bool User::IsInFortressSiege() {
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    return pledge != nullptr && m_data->m_nResidenceID != -1 &&
        (pledge->m_pFortressUnderDefenceSiege != nullptr || pledge->m_pFortressUnderOffenceSiege != nullptr);
}

bool User::IsInAgitWar(bool includeAnyWorldAgit) {
    L2_GUARD_FUNCTION();

    if (CMiniGameAgit::IsPlayer(this)) {
        return true;
    }

    CWorld& world = CWorld::Inst();
    const FVector position = GetPos();
    if (!world.GetGeo()->IsBattleField(position)) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(GetPos());
    if (residence == nullptr || !residence->IsWorldAgit()) {
        return false;
    }

    CWorldAgit* const worldAgit = residence->CastWorldAgit();
    if (worldAgit == nullptr) {
        return false;
    }
    if (includeAnyWorldAgit) {
        return true;
    }

    if (worldAgit->IsTeamBattleAgit()) {
        CTeamBattleAgit* const teamBattleAgit = worldAgit->CastTeamBattleAgit();
        CPledge* const pledge = GetPledge();
        if (teamBattleAgit == nullptr || pledge == nullptr) {
            return false;
        }
        if (teamBattleAgit->IsMasterEntryMember(GetDBID())) {
            return true;
        }
        return teamBattleAgit->IsBattleRoyalEntryMember(pledge->GetDbId(), GetDBID());
    }

    if (worldAgit->IsSiegeAgit()) {
        CSiegeAgit* const siegeAgit = worldAgit->CastSiegeAgit();
        CPledge* const pledge = GetPledge();
        return siegeAgit != nullptr && pledge != nullptr && siegeAgit->IsAttackerPledge(pledge);
    }

    return false;
}

int User::IsInMySiegeBattleField() {
    // Current L2Server 0x00ba12f0.
    if (m_AreaMod.m_bIsInPvPZone || !m_AreaMod.m_bIsInBattleField) {
        return 0;
    }

    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(GetPos());
    if (residence == nullptr) {
        return 0;
    }

    if ((residence->IsCastle() || residence->IsFortress()) && m_data->m_nDominionSiegeStatus != 0) {
        return 4;
    }

    const int residenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
    if (residence->IsCastle()) {
        const std::uint32_t castleIndex = static_cast<std::uint32_t>(residenceId - 1);
        int userSiegeId = -1;
        if (castleIndex < 9U) {
            userSiegeId = m_nCastleSiegeId[castleIndex];
        } else if (castleIndex == 9U) {
            // The current unsigned bound is literally `< 10` even though
            // m_nCastleSiegeId has nine elements. Physical +0x5154 is the
            // first m_nCastleSiegeStatus element. Preserve that read without
            // invoking source-language out-of-bounds behaviour.
            userSiegeId = m_nCastleSiegeStatus[0];
        } else {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] castle_index is wrong!! [%d]",
            //     L"c:\\work\\lineage2build\\server\\main\\l2server\\user.h",
            //     0xd1d,
            //     castleIndex)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] castle_index is wrong!! [%d]",
            //     L"c:\\work\\lineage2build\\server\\main\\l2server\\user.h",
            //     0xd1d,
            //     castleIndex)
            L2_LOG_ERROR(
                Log,
                "Siege classification encountered residence ID %d outside castle range 1 through 10; reporting the stack "
                "and continuing with the fallback checks.",
                residenceId
            );
            runtime::process::ReportNamedCallStack(L"Siege", LOG_ERROR, false);
        }
        if (residenceId == userSiegeId) {
            return 1;
        }
    }

    if (residence->IsFortress() &&
        (residenceId == m_nFortressUnderDefenceSiegeId || residenceId == m_nFortressUnderOffenceSiegeId)) {
        return 2;
    }

    CWorldAgit* const worldAgit = residence->CastWorldAgit();
    if (worldAgit != nullptr && worldAgit->IsBattleTypeAgit()) {
        if (worldAgit->IsNpcBattleAgit()) {
            return 3;
        }
        CPledge* const pledge = GetPledge();
        if (pledge != nullptr && worldAgit->IsRegisteredForSiege(pledge)) {
            return 3;
        }
    }

    return 0;
}

void User::CheckPledgeMasterOnline() {
    // Current L2Server 0x00b9efb8.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || IsPledgeMaster() || m_PledgeGameUser.IsPlaying()) {
        return;
    }

    if (m_nTimeActivatePledgeSkill == 0) {
        if (pledge->GetMasterOrAnyElite() != nullptr) {
            pledge->AddPledgeSkillByEliteTo(this);
        }
    } else if (pledge->GetMasterOrAnyElite() == nullptr) {
        pledge->RemovePledgeSkillByEliteFrom(this);
    }
}

void User::GetPVPPointByTimerInBattleField() {
    // Current L2Server 0x00ba2060.
    L2_GUARD_FUNCTION();
    if (!CanGetPVPPoint()) {
        return;
    }

    if (IsInMySiegeBattleField() == 0 || IsInAgitWar(false)) {
        if (m_bCheckPVPTimerInBattleField) {
            m_bCheckPVPTimerInBattleField = false;
            const std::int32_t now = runtime::crt::Time32Now();
            m_nSavedBattleFieldTime = runtime::SubtractInt32Wrapping(now, m_nLastEnterBattleFieldTime);
            m_nLastEnterBattleFieldTime = 0;
        }
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(GetPos());
    if (residence == nullptr) {
        return;
    }

    int place = 0;
    if (residence->IsWorldAgit()) {
        place = 2;
        CWorldAgit* const worldAgit = residence->CastWorldAgit();
        if (worldAgit != nullptr) {
            static_cast<void>(worldAgit->IsNpcBattleAgit());
        }
    } else if (residence->IsCastle()) {
        place = 0;
    } else if (residence->IsFortress()) {
        place = 1;
    } else {
        return;
    }

    if (m_data->m_nDominionSiegeStatus != 0) {
        place = 6;
    }

    if (!m_bCheckPVPTimerInBattleField) {
        m_bCheckPVPTimerInBattleField = true;
        const std::int32_t now = runtime::crt::Time32Now();
        m_nLastEnterBattleFieldTime = runtime::SubtractInt32Wrapping(now, m_nSavedBattleFieldTime);
        m_nSavedBattleFieldTime = 0;
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (runtime::SubtractInt32Wrapping(now, m_nLastEnterBattleFieldTime) <= 300) {
        return;
    }

    m_nLastEnterBattleFieldTime = runtime::crt::Time32Now();
    if (GetPledge() != nullptr) {
        RelayClient::Inst()->SendWarPacket(this, place);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] SendWarPacket [%s][%d]",
        //     GetName(),
        //     place)
        // Noise: The periodic battlefield report was sent to the relay. This per-player trace does not establish relay
        // processing and adds routine noise. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] SendWarPacket [%s][%d]",
        //     GetName(),
        //     place)
    }
}

bool User::CanInstallBattleCamp() {
    // Current L2Server 0x00ba229c.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || !m_data->m_bPledgeMaster) {
        return false;
    }

    const FVector position = GetPos();
    CResidence* const residence = g_ResidenceDB.GetWhoseBattleCampZone(position);
    if (residence == nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_BUILD_HEADQUARTERS_HERE);
        return false;
    }

    const CSPointer<CBattleCamp> existing = pledge->GetBattleCamp();
    if (existing) {
        SendSystemMessage(SystemMessageId::CANNOT_BUILD_HEADQUARTERS_OR_OUTPOST_ALREADY_EXISTS);
        return false;
    }

    CCastle* const castle = residence->CastCastle();
    CSiegeAgit* siegeAgit = nullptr;
    if (residence->IsWorldAgit()) {
        CWorldAgit* const worldAgit = residence->CastWorldAgit();
        if (worldAgit != nullptr) {
            siegeAgit = worldAgit->CastSiegeAgit();
        }
    }
    CFortress* const fortress = residence->CastFortress();

    bool mayReserve = false;
    if (castle != nullptr) {
        const int castleId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
        CCastle* const castleUnderSiege = pledge->GetCastleUnderSiege(castleId);
        if (castleUnderSiege != nullptr) {
            const int siegeCastleId = castleUnderSiege->m_data != nullptr ? castleUnderSiege->m_data->m_nResidenceID : 0;
            if (siegeCastleId == castleId &&
                castleUnderSiege->IsOffendingPledge(static_cast<int>(pledge->GetDbId()), pledge->GetWorldId())) {
                mayReserve = true;
            }
        }
        if (!mayReserve) {
            mayReserve = m_data->m_nDominionSiegeStatus != 0;
        }
    } else if (siegeAgit != nullptr) {
        mayReserve = siegeAgit->IsUnderSiege() && siegeAgit->IsAttackerPledge(pledge);
    } else if (fortress != nullptr) {
        CFortress* const attackingFortress = pledge->m_pFortressUnderOffenceSiege;
        if (attackingFortress != nullptr) {
            const int attackingId = attackingFortress->m_data != nullptr ? attackingFortress->m_data->m_nResidenceID : 0;
            const int zoneId = fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0;
            mayReserve = attackingId == zoneId;
        }
        if (!mayReserve) {
            mayReserve = m_data->m_nDominionSiegeStatus != 0;
        }
    } else {
        mayReserve = true;
    }

    if (!mayReserve) {
        SendSystemMessage(SystemMessageId::CANNOT_SUMMON_ENCAMPMENT_WITHOUT_ATTACKER_CLAN_PARTICIPATION);
        return false;
    }
    if (CBattleCampDb::inst().ReserveBattleCamp(pledge->GetDbId(), position)) {
        return true;
    }
    SendSystemMessage(SystemMessageId::CANNOT_BUILD_HEADQUARTERS_TOO_CLOSE_TO_ANOTHER);
    return false;
}

bool User::CanInstallAdvancedBattleCamp() {
    // Current L2Server 0x00ba2d1c.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || !m_data->m_bPledgeMaster || pledge->GetCastle() == nullptr) {
        return false;
    }

    const FVector position = GetPos();
    if (g_ResidenceDB.GetWhoseBattleCampZone(position) == nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_BUILD_HEADQUARTERS_HERE);
        return false;
    }

    const CSPointer<CBattleCamp> existing = pledge->GetBattleCamp();
    if (existing) {
        SendSystemMessage(SystemMessageId::CANNOT_BUILD_HEADQUARTERS_OR_OUTPOST_ALREADY_EXISTS);
        return false;
    }
    if (m_data->m_nDominionSiegeStatus == 0) {
        SendSystemMessage(SystemMessageId::CANNOT_SUMMON_ENCAMPMENT_WITHOUT_ATTACKER_CLAN_PARTICIPATION);
        return false;
    }
    if (CBattleCampDb::inst().ReserveBattleCamp(pledge->GetDbId(), position)) {
        return true;
    }
    SendSystemMessage(SystemMessageId::CANNOT_BUILD_HEADQUARTERS_TOO_CLOSE_TO_ANOTHER);
    return false;
}

void User::InstallBattleCamp(bool advanced) {
    // Current L2Server 0x00ba2670.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || !m_data->m_bPledgeMaster) {
        return;
    }

    const FVector position = GetPos();
    CResidence* const residence = g_ResidenceDB.GetWhoseBattleCampZone(position);
    if (residence == nullptr || pledge->GetBattleCamp()) {
        return;
    }

    CCastle* const castle = residence->CastCastle();
    CSiegeAgit* siegeAgit = nullptr;
    if (residence->IsWorldAgit()) {
        CWorldAgit* const worldAgit = residence->CastWorldAgit();
        if (worldAgit != nullptr) {
            siegeAgit = worldAgit->CastSiegeAgit();
        }
    }
    CFortress* const fortress = residence->CastFortress();

    bool mayInstall = false;
    if (castle != nullptr) {
        const int residenceId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
        CCastle* const castleUnderSiege = pledge->GetCastleUnderSiege(residenceId);
        if (castleUnderSiege != nullptr) {
            const int siegeResidenceId = castleUnderSiege->m_data != nullptr ? castleUnderSiege->m_data->m_nResidenceID : 0;
            if (siegeResidenceId == residenceId &&
                castleUnderSiege->IsOffendingPledge(static_cast<int>(pledge->GetDbId()), pledge->GetWorldId())) {
                mayInstall = true;
            }
        }
        if (!mayInstall) {
            mayInstall = m_data->m_nDominionSiegeStatus != 0;
        }
    } else if (siegeAgit != nullptr) {
        mayInstall = siegeAgit->IsAttackerPledge(pledge);
    } else if (fortress != nullptr) {
        CFortress* const attackingFortress = pledge->m_pFortressUnderOffenceSiege;
        if (attackingFortress != nullptr) {
            const int attackingId = attackingFortress->m_data != nullptr ? attackingFortress->m_data->m_nResidenceID : 0;
            const int residenceId = fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0;
            mayInstall = attackingId == residenceId;
        }
        if (!mayInstall) {
            mayInstall = m_data->m_nDominionSiegeStatus != 0;
        }
    } else {
        mayInstall = true;
    }

    if (!mayInstall) {
        return;
    }

    unsigned int maximumHp = static_cast<unsigned int>(static_cast<int>(CBattleCamp::GetStaticMaxHP()));
    if (advanced) {
        maximumHp <<= 2;
    }
    db.RequestInstallBattleCamp(
        pledge, static_cast<unsigned int>(residence->m_data->m_nResidenceID), maximumHp, position, 0
    );
}

void User::InstallAdvancedBattleCamp() {
    // Current L2Server 0x00ba2f58.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || !m_data->m_bPledgeMaster) {
        return;
    }

    const FVector position = GetPos();
    CResidence* const residence = g_ResidenceDB.GetWhoseBattleCampZone(position);
    if (residence == nullptr || pledge->GetBattleCamp() || m_data->m_nDominionSiegeStatus == 0) {
        return;
    }

    const unsigned int maximumHp = static_cast<unsigned int>(static_cast<int>(CBattleCamp::GetStaticMaxHP()));
    if (pledge->m_pDominion != nullptr) {
        const int dominionId = pledge->m_pDominion->m_data != nullptr ? pledge->m_pDominion->m_data->m_nResidenceID : 0;
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7e6, this, nullptr, pledge->GetPledgeName(), nullptr, nullptr, dominionId, pledge->GetDbId(), 1, 0, 0, 0, 0
        );
    }

    db.RequestInstallBattleCamp(
        pledge, static_cast<unsigned int>(residence->m_data->m_nResidenceID), maximumHp, position, 1
    );
}

void User::UninstallAdvancedBattleCamp() {
    // Current L2Server 0x00ba3194.
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr) {
        return;
    }

    const CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
    if (!battleCamp || !battleCamp->IsEnabled() || !battleCamp->IsAdvanceBase()) {
        return;
    }

    if (pledge->m_pDominion != nullptr) {
        const int dominionId = pledge->m_pDominion->m_data != nullptr ? pledge->m_pDominion->m_data->m_nResidenceID : 0;
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7e6, this, nullptr, pledge->GetPledgeName(), nullptr, nullptr, dominionId, pledge->GetDbId(), 0, 0, 0, 0, 0
        );
    }

    db.RequestUninstallBattleCampBy(pledge, battleCamp->GetBaseType());
}

bool User::CanResurrection(int& castleSiegeStatus, int& fortressSiegeStatus) {
    // Current L2Server 0x00ba33f0.
    L2_GUARD_FUNCTION();

    if (GetInZoneID() != 0) {
        return true;
    }

    castleSiegeStatus = 0;
    fortressSiegeStatus = 0;

    int castleResidenceId = 0;
    int fortressResidenceId = 0;
    const FVector position = GetPos();
    CResidence* residence = CResidence::GetBattleFieldResidence(position);
    if (residence != nullptr) {
        if (residence->IsCastle()) {
            castleResidenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
        } else if (residence->IsFortress()) {
            fortressResidenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
        }
    } else {
        if (!m_AreaMod.m_bIsInBattleField || m_AreaMod.m_bIsInPvPZone) {
            return true;
        }

        CGeoData* const geo = CWorld::Inst().GetGeo();
        const int positionX = runtime::Win64CvttDoubleToInt32(position.x);
        const int positionY = runtime::Win64CvttDoubleToInt32(position.y);
        const int zoneX = static_cast<int>((static_cast<std::int64_t>(positionX) + 0x50000) >> 15) + 10;
        const int zoneY = static_cast<int>((static_cast<std::int64_t>(positionY) + 0x40000) >> 15) + 10;
        CGeoZone* const zone = geo->GetZone(zoneX, zoneY);
        CGeoSector* const sector =
            zone != nullptr ? zone->GetSector((positionX >> 7) & 0xff, (positionY >> 7) & 0xff) : nullptr;
        constexpr std::uint16_t battleResidenceMask = 0x8000;
        if (sector == nullptr ||
            (static_cast<std::uint16_t>(sector->m_Field) & battleResidenceMask) != battleResidenceMask) {
            return true;
        }

        residence = g_ResidenceDB.GetResidence(zone->castleId);
        if (residence == nullptr) {
            return true;
        }
        if (residence->IsCastle()) {
            castleResidenceId = zone->castleId;
        } else if (residence->IsFortress()) {
            fortressResidenceId = zone->castleId;
        } else {
            return true;
        }
    }

    CPledge* const pledge = GetPledge();
    const int dominionId = m_data->m_nDominionSiegeId;
    if (dominionId != 0) {
        if (pledge != nullptr) {
            const CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
            if (battleCamp && battleCamp->IsEnabled()) {
                return true;
            }
        }

        CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
        CPledge* const ownerPledge = dominion != nullptr ? dominion->GetResidenceOwnerPledge() : nullptr;
        if (ownerPledge != nullptr) {
            const CSPointer<CBattleCamp> battleCamp = ownerPledge->GetBattleCamp();
            if (battleCamp && battleCamp->IsAdvanceBase() && battleCamp->IsEnabled()) {
                return true;
            }
        }
    }

    if (pledge == nullptr) {
        return false;
    }

    CCastle* const castle = pledge->GetCastleUnderSiege(castleResidenceId);
    CFortress* const offenceFortress = pledge->m_pFortressUnderOffenceSiege;
    CFortress* const defenceFortress = pledge->m_pFortressUnderDefenceSiege;

    if (castle != nullptr) {
        const int residenceId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
        if (residenceId == castleResidenceId) {
            castleSiegeStatus = pledge->GetCurrentCastleSiegeStatus(castleResidenceId);
            if (castleSiegeStatus == 1) {
                const CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
                return battleCamp && battleCamp->IsEnabled() && battleCamp->GetBaseResidenceId() == residenceId;
            }
            if (castleSiegeStatus != 2) {
                return false;
            }

            const ResidenceState state = castle->m_data != nullptr ? castle->m_data->m_ResidenceState : RS_MAX;
            if (state == RS_SIEGE && GetSSQPart() == SSQPart::TWILIGHT) {
                return false;
            }
            return castle->m_data->m_nLifeControlLevel > 1;
        }
    }

    if (offenceFortress != nullptr) {
        const int residenceId = offenceFortress->m_data != nullptr ? offenceFortress->m_data->m_nResidenceID : 0;
        if (residenceId == fortressResidenceId) {
            const CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
            return battleCamp && battleCamp->IsEnabled() && battleCamp->GetBaseResidenceId() == residenceId;
        }
    }

    if (defenceFortress != nullptr) {
        const int residenceId = defenceFortress->m_data != nullptr ? defenceFortress->m_data->m_nResidenceID : 0;
        if (residenceId == fortressResidenceId) {
            return true;
        }
    }
    return false;
}

bool User::HasFlagOfSiege() {
    L2_GUARD_FUNCTION();

    return m_pOwnThing != nullptr || (IsInFortressSiege() && m_bHasFortressFlag);
}

void User::DeleteOwnThing(const bool appear, const bool returnToDominion) {
    // Current L2Server 0x00ba1700.
    L2_GUARD_FUNCTION();

    if (m_pOwnThing == nullptr) {
        return;
    }

    CDominion* const originalDominion = g_ResidenceDB.GetDominion(m_pOwnThing->GetOriginalResidenceID());
    if (originalDominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No Dominion For Ownthing!!! %s/%d",
        //     GetName(),
        //     m_pOwnThing->GetClassID())
        // L2_LOG_ERROR(Log,
        //     L"No Dominion For Ownthing!!! %s/%d",
        //     GetName(),
        //     m_pOwnThing->GetClassID())
        // L2_LOG_ERROR(Log,
        //     "Cannot remove the carried ward from '%s': its original dominion is unavailable (object class %d). The ward is
        //     retained.", WideToUtf8(GetName()).c_str(), m_pOwnThing->GetClassID())
        L2_LOG_ERROR(
            Log,
            "Cannot remove the carried ward from '%s': its original dominion is unavailable (object class %d); the ward is "
            "retained.",
            Utf16ToUtf8(GetName()).c_str(), m_pOwnThing->GetClassID()
        );
        return;
    }

    if (CPledge* const pledge = GetPledge()) {
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7e9, this, nullptr, pledge->GetPledgeName(), nullptr, nullptr, pledge->GetDbId(),
            m_pOwnThing->GetOriginalResidenceID(), m_pOwnThing->GetOwningResidenceID(), 0, 0, 0, 0
        );
    }

    const std::u16string pickupName = target_utf16::FromRecoveredWindowsWide(originalDominion->GetPickupOwnThingName());
    const int itemClassId = g_ObjectDB.GetClassIdFromName(pickupName.c_str(), false);

    CItem* const item = [&] {
        L2_WRITE_GUARD(m_inv);
        return m_inv.GetFirstItemByClassID(itemClassId, false, false);
    }();

    if (item != nullptr) {
        if (!returnToDominion) {
            m_pOwnThing->SetPos(GetPos());
        } else if (CResidence* const residence = m_pOwnThing->GetOwningResidence();
            residence != nullptr && residence->IsDominion()) {
            CDominion* const dominion = residence->CastDominion();
            m_pOwnThing->SetPos(dominion->GetPos(m_pOwnThing));
        }

        db.RequestDestroyItem(item, 1, this, 0);
        if (appear) {
            m_pOwnThing->Appear();
            m_pOwnThing->SendOwnThingInfo(nullptr);
        }
    }

    m_pOwnThing = nullptr;
}

void User::SendPledgeAnnounce() {
    // Current L2Server 0x00b9f868.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetPledge();
    if (pledge == nullptr) {
        return;
    }
    if (!PledgeAnnounceHtmlBuilder::Announce(GetServerId(), pledge->GetDbId())) {
        db.RequestGetPledgeAnnounce(this, pledge->GetDbId(), 1);
    }
}

void User::RemoveOwnThing() {
    // Current L2Server 0x00ba5148.
    L2_GUARD_FUNCTION();
    const CDominionManager::DominionList dominions = CDominionManager::Inst()->GetDominionList();
    for (CDominion* const dominion : dominions) {
        if (dominion == nullptr) {
            continue;
        }
        const std::u16string pickupName = target_utf16::FromRecoveredWindowsWide(dominion->GetPickupOwnThingName());
        const int itemClassId = g_ObjectDB.GetClassIdFromName(pickupName.c_str(), false);
        CItem* const item = [&] {
            L2_READ_GUARD(m_inv);
            return m_inv.GetFirstItemByClassID(itemClassId, false, false);
        }();
        if (item != nullptr) {
            db.RequestDestroyItem(item, 1, this, 0);
        }
    }
}

bool User::CanPossessHolyThing(CHolyThing* holyThing, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00ba1494.
    L2_GUARD_FUNCTION();
    if (holyThing == nullptr) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] [CHolyThing *pHolyThing] is nullptr",
        //     L"user_pledge.cpp",
        //     0x332)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CHolyThing *pHolyThing] is nullptr",
        //     L"user_pledge.cpp",
        //     0x332)
        L2_LOG_ERROR(
            Log, "Cannot check ward possession for character %u: the ward object is unavailable.",
            static_cast<unsigned>(GetDBID())
        );
        return false;
    }

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr || !m_data->m_bPledgeMaster) {
        return false;
    }

    CResidenceData* const owningResidenceData = holyThing->m_pOwningResidence->m_data;
    const int owningResidenceId = owningResidenceData != nullptr ? owningResidenceData->m_nResidenceID : 0;
    CCastle* const castle = pledge->GetCastleUnderSiege(owningResidenceId);
    if (castle == nullptr) {
        return false;
    }

    const int pledgeWorldId = pledge->GetWorldId();
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    if (!castle->IsOffendingPledge(static_cast<int>(pledgeDatabaseId), pledgeWorldId) ||
        !g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        return false;
    }

    CResidenceData* const castleData = castle->m_data;
    if (holyThing->m_nOwningResidenceID != castleData->m_nResidenceID) {
        return false;
    }

    const int skillId = skillInfo != nullptr ? skillInfo->m_nSkillId : 0;
    const int castleResidenceId = castleData != nullptr ? castleData->m_nResidenceID : 0;
    CLogFormatter formatter;
    formatter.WriteLog2_1(0xe4, this, nullptr, m_data->m_nPledgeID, castleResidenceId, skillId, 0, 0, 0, 0);
    holyThing->SendWarningMessage(skillInfo);
    holyThing->AddPossessingMaster(this);
    Log.AddT(
        LOG_AUDIT, u"%I64d,,,,,,,,%s,,,%I64d,%I64d,%I64d,,,,,,,,,,,,", 0x582, pledge->GetPledgeName(), m_data->m_nPledgeID,
        castleResidenceId, skillId
    );
    return true;
}

bool User::IsNoSiegeRelation() {
    L2_GUARD_FUNCTION();

    CPledge* const pledge = GetPledge();
    if (m_data->m_nDominionSiegeStatus != 0) {
        return false;
    }
    if (pledge == nullptr) {
        return true;
    }
    return pledge->GetCurrentCastleSiegeStatus(m_nCurCastleId) == 0 && pledge->m_data->m_nFortressUnderDefenceSiegeId == 0 &&
        pledge->m_data->m_nFortressUnderOffenceSiegeId == 0;
}

bool User::IsNoSiegeRelationForBookmark() {
    // Current L2Server.exe 0x00b9ff20.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetPledge();
    if (m_data->m_nDominionSiegeStatus != 0) {
        return false;
    }
    if (pledge == nullptr) {
        return true;
    }
    return !pledge->HaveCastleSiegeStatus(CUS_OFFENCE) && !pledge->HaveCastleSiegeStatus(CUS_DEFENCE) &&
        pledge->m_data->m_nFortressUnderDefenceSiegeId == 0 && pledge->m_data->m_nFortressUnderOffenceSiegeId == 0;
}

int User::GetRelationTo(User* target) {
    L2_GUARD_FUNCTION();

    if (target == nullptr || target == this) {
        return 0;
    }

    std::uint32_t relation = 0;
    if (m_DuelUserInfo.IsInFightStatus() && m_DuelUserInfo.IsEnemy(target) && !m_DuelUserInfo.IsDuelDead(this)) {
        bool targetIsDuelDead = false;
        if (target->IsPC()) {
            User* const targetUser = target->CastUser();
            targetIsDuelDead = targetUser != nullptr && targetUser->m_DuelUserInfo.IsDuelDead(targetUser);
        }
        if (!targetIsDuelDead) {
            // relation |= 0x40000U.
            relation |= static_cast<std::uint32_t>(ERB_DUEL_ENEMY);
        }
    }

    const bool targetSiegeField = !target->m_AreaMod.m_bIsInPvPZone && target->m_AreaMod.m_bIsInBattleField;
    const bool userSiegeField = !m_AreaMod.m_bIsInPvPZone && m_AreaMod.m_bIsInBattleField;
    bool enemy = m_AreaMod.m_bIsInBattleField;
    if (targetSiegeField != userSiegeField) {
        if ((!target->IsNoSiegeRelation() && !IsNoSiegeRelation()) || m_data->m_cIsGuilty != 0) {
            enemy = !g_bIsNonPKServer;
        } else {
            enemy = false;
            if (m_data->m_nKarma < 0) {
                enemy = !g_bIsNonPKServer;
            }
        }
    }
    if (!m_AreaMod.m_bIsInPvPZone && m_AreaMod.m_bIsInBattleField) {
        // relation |= 0x1aU.
        relation |= static_cast<std::uint32_t>(ERB_GUILTY | ERB_PARTY_MEMBER | ERB_PARTY_MASTER);
    }
    if (enemy || m_EventMatchUser.m_byEventMatchTeamId != 0) {
        // relation |= 1U.
        relation |= static_cast<std::uint32_t>(ERB_IN_BATTLE_FIELD);
    }

    int relationBits = static_cast<int>(relation);
    CPVPMatchSystem::inst().GetRelationTo(*this, *target, relationBits);
    relation = static_cast<std::uint32_t>(relationBits);

    if (IsGuilty() || m_bEinhasadeTransformed) {
        // relation |= 2U.
        relation |= static_cast<std::uint32_t>(ERB_GUILTY);
    }
    if (m_data->m_nKarma < 0 || m_bEinhasadeTransformed) {
        // relation |= 4U.
        relation |= static_cast<std::uint32_t>(ERB_CHAOTIC);
    }

    if (m_data->m_nPartyId != 0) {
        // relation |= m_data->m_bPartyMaster ? 0x18U : 8U.
        relation |= m_data->m_bPartyMaster ? static_cast<std::uint32_t>(ERB_PARTY_MEMBER | ERB_PARTY_MASTER)
                                           : static_cast<std::uint32_t>(ERB_PARTY_MEMBER);
        if (m_data->m_nPartyId == target->m_data->m_nPartyId) {
            // relation |= 0x20U.
            relation |= static_cast<std::uint32_t>(ERB_PARTY_COMRADE);
        }
        if (m_AreaMod.m_bMPCCAsParty) {
            CMultiPartyCommandChannel* const userChannel = GetMPCC();
            CMultiPartyCommandChannel* const targetChannel = target->GetMPCC();
            if (userChannel != nullptr && targetChannel != nullptr && userChannel->Id() == targetChannel->Id()) {
                // relation |= 0x20U.
                relation |= static_cast<std::uint32_t>(ERB_PARTY_COMRADE);
            }
        }
    }

    if (m_data->m_nDominionSiegeStatus != 0 && target->m_data->m_nDominionSiegeStatus != 0) {
        relation |= m_data->m_nDominionSiegeId == target->m_data->m_nDominionSiegeId
            // ? 0x80000U
            ? static_cast<std::uint32_t>(ERB_COMRADE_IN_DOMINION_SIEGE)
            // : 0x100000U.
            : static_cast<std::uint32_t>(ERB_ENEMY_IN_DOMINION_SIEGE);
    }

    if (m_data->m_nPledgeID == 0) {
        return static_cast<int>(relation);
    }
    // relation |= m_data->m_bPledgeMaster ? 0xc0U : 0x40U.
    relation |= m_data->m_bPledgeMaster ? static_cast<std::uint32_t>(ERB_PLEDGE_MEMBER | ERB_PLEDGE_MASTER)
                                        : static_cast<std::uint32_t>(ERB_PLEDGE_MEMBER);
    if (m_data->m_nPledgeID == target->m_data->m_nPledgeID && m_data->m_nWorldId == target->m_data->m_nWorldId) {
        // relation |= 0x100U.
        relation |= static_cast<std::uint32_t>(ERB_PLEDGE_COMRADE);
    }
    if (!IsPledgeMemberOrAcademyMemberPledgeRelationAffected() ||
        !target->IsPledgeMemberOrAcademyMemberPledgeRelationAffected()) {
        return static_cast<int>(relation);
    }

    const bool targetInSiege = !target->m_AreaMod.m_bIsInPvPZone && target->m_AreaMod.m_bIsInBattleField;
    const bool userInSiege = !m_AreaMod.m_bIsInPvPZone && m_AreaMod.m_bIsInBattleField;

    const std::uint32_t castleIndex = static_cast<std::uint32_t>(m_nCurCastleId) - 1U;
    int userSiegeId = 0;
    int targetSiegeId = 0;
    int userSiegeStatus = 1;
    int targetSiegeStatus = 0;
    if (castleIndex < 9U) {
        userSiegeStatus = m_nCastleSiegeStatus[castleIndex];
        targetSiegeStatus = target->m_nCastleSiegeStatus[castleIndex];
        userSiegeId = m_nCastleSiegeId[castleIndex];
        targetSiegeId = target->m_nCastleSiegeId[castleIndex];
    } else {
        const int offence = m_nFortressUnderOffenceSiegeId;
        const int defence = m_nFortressUnderDefenceSiegeId;
        bool matched = false;
        if (offence != 0) {
            if (offence == target->m_nFortressUnderOffenceSiegeId) {
                userSiegeId = targetSiegeId = offence;
                userSiegeStatus = targetSiegeStatus = 1;
                matched = true;
            } else if (offence == target->m_nFortressUnderDefenceSiegeId) {
                userSiegeId = targetSiegeId = offence;
                userSiegeStatus = 1;
                targetSiegeStatus = 2;
                matched = true;
            }
        }
        if (!matched && defence != 0) {
            if (defence == target->m_nFortressUnderOffenceSiegeId) {
                userSiegeId = targetSiegeId = defence;
                userSiegeStatus = 2;
                targetSiegeStatus = 1;
                matched = true;
            } else if (defence == target->m_nFortressUnderDefenceSiegeId) {
                userSiegeId = targetSiegeId = defence;
                userSiegeStatus = targetSiegeStatus = 2;
                matched = true;
            }
        }
        if (!matched) {
            userSiegeId = offence != 0 ? offence : defence;
            userSiegeStatus = offence != 0 ? 1 : 2;
        }
    }

    if (userSiegeId != 0 && userSiegeStatus != 0) {
        // relation |= userSiegeStatus == 1 ? 0x600U : 0x200U.
        relation |= userSiegeStatus == 1
            ? static_cast<std::uint32_t>(ERB_PLEDGE_IN_CASTLE_SIEGE | ERB_ATTACKER_IN_CASTLE_SIEGE)
            : static_cast<std::uint32_t>(ERB_PLEDGE_IN_CASTLE_SIEGE);
        if (userSiegeId == targetSiegeId) {
            const bool mutualDefence = userSiegeStatus == 2 && targetSiegeStatus == 2;
            const bool noMidVictory = static_cast<std::int32_t>(castleIndex) >= 0 && m_nCastleSiegeId[castleIndex] > 0 &&
                userSiegeStatus == 1 && targetSiegeStatus == 1 &&
                g_aHadMidVictory[static_cast<std::size_t>(m_nCastleSiegeId[castleIndex])] == 0;
            if (mutualDefence || noMidVictory) {
                // relation |= 0x800U.
                relation |= static_cast<std::uint32_t>(ERB_COMRADE_IN_CASTLE_SIEGE);
            } else if (!g_bIsNonPKServer || targetSiegeStatus != 0 || targetInSiege || !userInSiege) {
                // relation |= 0x1000U.
                relation |= static_cast<std::uint32_t>(ERB_ENEMY_IN_CASTLE_SIEGE);
            }
        }
    }

    if (m_data->m_nAllianceId != 0) {
        // relation |= m_bAllianceMaster != 0 ? 0x30000U : 0x10000U.
        relation |= m_bAllianceMaster != 0 ? static_cast<std::uint32_t>(ERB_ALLIANCE_MEMBER | ERB_ALLIANCE_MASTER)
                                           : static_cast<std::uint32_t>(ERB_ALLIANCE_MEMBER);
    }

    unsigned char pledgeWarRelation = 0;
    if (m_nRelationSlot != -1 && target->m_nRelationSlot != -1) {
        pledgeWarRelation = Singleton<CRelationManager>::inst().GetRelation(this, target);
    }
    if ((pledgeWarRelation & 2U) != 0) {
        // relation |= 0x4000U.
        relation |= static_cast<std::uint32_t>(ERB_DECLARE_WAR_TO_YOUR_PLEDGE);
    }
    if ((pledgeWarRelation & 4U) != 0) {
        // relation |= 0x8000U.
        relation |= static_cast<std::uint32_t>(ERB_DECLARE_WAR_TO_MY_PLEDGE);
    }
    // if ((relation & 0x1100U) == 0x1100U) {
    if ((relation & static_cast<std::uint32_t>(ERB_PLEDGE_COMRADE | ERB_ENEMY_IN_CASTLE_SIEGE)) ==
        static_cast<std::uint32_t>(ERB_PLEDGE_COMRADE | ERB_ENEMY_IN_CASTLE_SIEGE)) {
        // relation = (relation & ~0x1000U) | 0x800U
        relation = (relation & ~static_cast<std::uint32_t>(ERB_ENEMY_IN_CASTLE_SIEGE)) |
            static_cast<std::uint32_t>(ERB_COMRADE_IN_CASTLE_SIEGE);
    }
    return static_cast<int>(relation);
}

bool User::CheckDeclareWarBoth(User* target) {
    // Current L2Server 0x00ba5ae4.
    const std::uint32_t relation = static_cast<std::uint32_t>(GetRelationTo(target));
    return
        // (relation & 0x120U) == 0 &&
        (relation & static_cast<std::uint32_t>(ERB_PARTY_COMRADE | ERB_PLEDGE_COMRADE)) == 0 &&
        // ((relation & 0x800U) == 0 ||
        ((relation & static_cast<std::uint32_t>(ERB_COMRADE_IN_CASTLE_SIEGE)) == 0 ||
            !target->m_AreaMod.m_bIsInBattleField) &&
        // (relation & 0xc000U) == 0xc000U.
        (relation & static_cast<std::uint32_t>(ERB_DECLARE_WAR_TO_YOUR_PLEDGE | ERB_DECLARE_WAR_TO_MY_PLEDGE)) ==
        static_cast<std::uint32_t>(ERB_DECLARE_WAR_TO_YOUR_PLEDGE | ERB_DECLARE_WAR_TO_MY_PLEDGE);
}

bool User::IsMakeAttackerChao(CCreature* attacker) {
    // Current L2Server.exe 0x00ba5b2c.
    L2_GUARD_FUNCTION();

    if (attacker == this) {
        return false;
    }
    if (attacker->IsSummonOrPet()) {
        CSummon* const summon = attacker->CastSummon();
        attacker = summon->GetMaster();
        if (attacker == nullptr) {
            return false;
        }
    }
    if (!attacker->IsPC()) {
        return false;
    }

    User* attackerUser = attacker->CastUser();
    if (m_DuelUserInfo.IsEnemy(attackerUser)) {
        return false;
    }
    attackerUser = attacker->CastUser();
    if (m_pPolymorphNPC != nullptr || attackerUser->m_pPolymorphNPC != nullptr ||
        attackerUser->IsInSamePVPMatch(*this, PVPMatchType::NONE, PVPMatchPlayerState::COMBAT)) {
        return false;
    }

    CPledge* const pledge = GetPledge();
    CCastle* const castle = pledge != nullptr ? pledge->GetCastle() : nullptr;
    if (castle != nullptr && castle->m_data->m_ResidenceType == 2) {
        return false;
    }

    const bool attackerInSiegeField = !attackerUser->m_AreaMod.m_bIsInPvPZone && attackerUser->m_AreaMod.m_bIsInBattleField;
    const bool userInSiegeField = !m_AreaMod.m_bIsInPvPZone && m_AreaMod.m_bIsInBattleField;
    if (attackerInSiegeField == userInSiegeField || m_data->m_cIsGuilty != 0 || m_data->m_nKarma < 0 ||
        (!attackerUser->IsNoSiegeRelation() && !IsNoSiegeRelation())) {
        attackerUser = attacker->CastUser();
        const std::uint32_t relation = static_cast<std::uint32_t>(GetRelationTo(attackerUser));
        if ((relation & 6U) != 0 || (relation & 1U) != 0) {
            return false;
        }
        if ((relation & 0x120U) == 0) {
            return (relation & 0x103000U) == 0;
        }
    }
    return true;
}

bool User::IsMakeAttackerGuilty(CCreature* attacker, const bool flag) {
    // Current L2Server.exe 0x00ba5d64.
    L2_GUARD_FUNCTION();

    if (!flag) {
        {
            L2_GUARD(*m_lock);
            if (!IsAlive()) {
                if (m_data->m_LastAttackerServerID != static_cast<std::int32_t>(attacker->Id())) {
                    return false;
                }
                if (m_bIsNonChaoInNow) {
                    return false;
                }
            }
        }
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (m_nCursedWeaponClassId > 0 || attacker == nullptr || attacker == this) {
        return false;
    }

    if (attacker->IsSummonOrPet()) {
        CSummon* const summon = attacker->CastSummon();
        User* const master = summon->GetMaster();
        if (master == this) {
            return false;
        }
        attacker = master;
        if (attacker == nullptr) {
            return false;
        }
    }

    bool useNpcMaster = attacker->IsTrap();
    if (!useNpcMaster && attacker->IsNPC()) {
        useNpcMaster = attacker->CastNPC()->IsSummonedNpc();
    }
    if (useNpcMaster) {
        CNPC* const npc = attacker->CastNPC();
        User* const master = npc->GetMaster();
        if (master == this) {
            return false;
        }
        attacker = master;
        if (attacker == nullptr) {
            return false;
        }
    }

    if (!attacker->IsPC()) {
        return false;
    }
    User* attackerUser = attacker->CastUser();
    static_cast<void>(CCursedWeaponMgr::inst());
    if (attackerUser != nullptr && attackerUser->m_nCursedWeaponClassId > 0) {
        return false;
    }

    attackerUser = attacker->CastUser();
    if (m_DuelUserInfo.IsEnemy(attackerUser) || attacker->m_data->m_nKarma < 0) {
        return false;
    }
    attackerUser = attacker->CastUser();
    if (attackerUser == nullptr || m_pPolymorphNPC != nullptr || attackerUser->m_pPolymorphNPC != nullptr) {
        return false;
    }
    if (attackerUser->IsInSamePVPMatch(*this, PVPMatchType::NONE, PVPMatchPlayerState::COMBAT)) {
        return false;
    }

    const bool attackerInSiegeField = !attackerUser->m_AreaMod.m_bIsInPvPZone && attackerUser->m_AreaMod.m_bIsInBattleField;
    const bool userInSiegeField = !m_AreaMod.m_bIsInPvPZone && m_AreaMod.m_bIsInBattleField;
    if (attackerInSiegeField == userInSiegeField || m_bEinhasadeTransformed || m_data->m_nKarma < 0 ||
        attackerUser->m_bEinhasadeTransformed || attackerUser->m_data->m_nKarma < 0 ||
        (!IsNoSiegeRelation() && !attackerUser->IsNoSiegeRelation())) {
        if (m_AreaMod.m_bIsInBattleField || m_EventMatchUser.m_byEventMatchTeamId != 0) {
            return false;
        }

        attackerUser = attacker->CastUser();
        const std::uint32_t relation = static_cast<std::uint32_t>(GetRelationTo(attackerUser));
        if ((relation & 4U) != 0) {
            return flag;
        }
        if ((relation & 0x120U) == 0) {
            if ((relation & 0x3000U) != 0) {
                return flag;
            }
            if ((relation & 0x800U) == 0) {
                if ((relation & 2U) == 0) {
                    return !flag;
                }
                return true;
            }
        }
        if ((relation & 2U) == 0) {
            return !flag;
        }
    }
    return true;
}

bool User::IsRealEnemyTo(CCreature* creature) {
    // Current L2Server.exe 0x00ba6180.
    L2_GUARD_FUNCTION();

    if (!creature->IsPC()) {
        return true;
    }
    if (IsGMInvisible()) {
        return false;
    }

    User* const target = creature->CastUser();
    if (target == nullptr) {
        return QueryRealEnemy(GetRelationTo(target));
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    const int ownCursedWeaponClassId = m_nCursedWeaponClassId;
    static_cast<void>(CCursedWeaponMgr::inst());
    const bool targetHasCursedWeapon = target->m_nCursedWeaponClassId > 0;
    if (ownCursedWeaponClassId < 1) {
        if (targetHasCursedWeapon) {
            return m_data->m_nLevel >= 20;
        }
        if (m_DuelUserInfo.IsInFightStatus() && m_DuelUserInfo.IsEnemy(target)) {
            return true;
        }
        return QueryRealEnemy(GetRelationTo(target));
    }

    if (target->m_data->m_nLevel < 20) {
        return false;
    }
    if (!targetHasCursedWeapon) {
        return true;
    }
    return m_data->m_nLevel >= 20;
}

bool User::IsEnemyTo(CCreature* creature) {
    // Current L2Server.exe 0x00ba62f0.
    L2_GUARD_FUNCTION();

    if (IsGMInvisible() || creature == nullptr || creature == this) {
        return false;
    }
    if (m_DuelUserInfo.IsInFightStatus() && IsDuelEnemy(creature)) {
        return true;
    }
    if (creature->IsSummonOrPet() && IsMySummonOrPet(creature)) {
        return creature->CastSummon()->m_betrayOwnMaster;
    }

    User* target = creature->GetUserOrMaster();
    if (creature->IsSummonedNpc()) {
        target = creature->CastNPC()->GetMaster();
    }

    if (target != nullptr) {
        if (m_pPolymorphNPC != nullptr) {
            return true;
        }

        static_cast<void>(CCursedWeaponMgr::inst());
        const int ownCursedWeaponClassId = m_nCursedWeaponClassId;
        static_cast<void>(CCursedWeaponMgr::inst());
        const bool targetHasCursedWeapon = target->m_nCursedWeaponClassId > 0;
        if (ownCursedWeaponClassId < 1) {
            if (!targetHasCursedWeapon) {
                if (IsOlympiadPlayer()) {
                    return m_pWOlympiadUser->IsEnemyTo(target->m_pWOlympiadUser);
                }
                if (m_PledgeGameUser.IsPlayer()) {
                    return m_PledgeGameUser.IsEnemyTo(&target->m_PledgeGameUser);
                }
                if (!QueryEnemy(GetRelationTo(target))) {
                    return false;
                }
            } else if (m_data->m_nLevel < 20) {
                return false;
            }
        } else {
            if (target->m_data->m_nLevel < 20) {
                return false;
            }
            if (targetHasCursedWeapon && m_data->m_nLevel < 20) {
                return false;
            }
        }
    }

    if (!creature->IsNPC()) {
        return true;
    }
    return creature->CastNPC()->IsEnemyToAsNpc(this, true);
}

bool User::HasAcademyStudentMasterRelation() {
    // Current L2Server 0x00b9ebc8.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetPledge();
    return pledge != nullptr &&
        (pledge->HasAcademyStudent(m_data->m_nAcademyPledgeDBID) || pledge->HasAcademyMaster(m_data->m_nAcademyPledgeDBID));
}

int User::GetMyStatus() {
    // Current L2Server 0x00ba52f0, original owner user_pledge.cpp.
    L2_GUARD_FUNCTION();

    int status = 0;
    if (m_AreaMod.m_bIsInBattleField || m_EventMatchUser.m_byEventMatchTeamId != 0) {
        // status = 1
        status = MSB_IN_BATTLE_FIELD;
    }
    if (IsGuilty()) {
        // status |= 2.
        status |= MSB_GUILTY;
    }
    if (m_data->m_nKarma < 0) {
        // status |= 4.
        status |= MSB_CHAOTIC;
    }

    CParty* const party = GetParty();
    if (party != nullptr) {
        // status |= 8.
        status |= MSB_PARTY_MEMBER;
        if (party->m_data->m_OnlineMaster.id == GetServerId()) {
            // status |= 0x10.
            status |= MSB_PARTY_MASTER;
        }
    }
    if (m_data->m_nDominionSiegeStatus != 0) {
        // status |= 0x1000.
        status |= MSB_PARTICIPANT_IN_DOMINION_SIEGE;
    }

    CPledge* const pledge = GetPledge();
    if (pledge != nullptr) {
        // status |= 0x20.
        status |= MSB_PLEDGE_MEMBER;
        if (m_data->m_bPledgeMaster) {
            // status |= 0x40.
            status |= MSB_PLEDGE_MASTER;
        }

        if (IsPledgeMemberOrAcademyMemberPledgeRelationAffected()) {
            if (pledge->HaveCastleUnderSiege()) {
                if (m_nCurCastleId > 0) {
                    const int siegeStatus = pledge->GetCurrentCastleSiegeStatus(m_nCurCastleId);
                    if (siegeStatus != 0) {
                        // status |= 0x80.
                        status |= MSB_PLEDGE_IN_CASTLE_SIEGE;
                        if (siegeStatus == 1) {
                            // status |= 0x100.
                            status |= MSB_ATTACKER_IN_CASTLE_SIEGE;
                        }
                    }
                }
            } else if (pledge->m_data->m_nFortressUnderOffenceSiegeId != 0) {
                // status |= 0x180.
                status |= (MSB_PLEDGE_IN_CASTLE_SIEGE | MSB_ATTACKER_IN_CASTLE_SIEGE);
            } else if (pledge->m_data->m_nFortressUnderDefenceSiegeId != 0) {
                // status |= 0x80.
                status |= MSB_PLEDGE_IN_CASTLE_SIEGE;
            }

            CAlliance* const alliance = pledge->GetAlliance();
            if (alliance != nullptr) {
                // status |= 0x200.
                status |= MSB_ALLIANCE_MEMBER;
                if (m_data->m_bPledgeMaster && alliance->m_data->m_nMasterPledgeDbId == pledge->GetDbId()) {
                    // status |= 0x400.
                    status |= MSB_ALLIANCE_MASTER;
                }
                for (const EnemyWarPair& war : alliance->m_data->m_EnemyWarPair) {
                    if (war.nEnemyId != 0) {
                        // status |= 0x800.
                        status |= MSB_ALLIANCE_IN_WAR;
                        break;
                    }
                }
            }
        }
    }

    CPVPMatchSystem::inst().GetMyStatus(*this, status);
    return status;
}

std::uint32_t User::GetPledgePowerByGrade() {
    // Current L2Server 0x00ba4644, original owner user_pledge.cpp.
    L2_GUARD_FUNCTION();
    if (m_data->m_bPledgeMaster) {
        return 0x1ffffffU;
    }
    CPledge* const pledge = GetPledge();
    return pledge != nullptr ? pledge->GetPowerGrade(m_data->m_nGradeId) : 0U;
}

bool User::HavePledgePower(PledgePowerEnum power) {
    L2_GUARD_FUNCTION();

    if (m_data->m_bPledgeMaster) {
        CheckPremiumType(PremiumUserType::PUT_FREE_USER);
        return true;
    }

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    const bool academyPowerAllowed = power == PP_VIEW_WAREHOUSE ||
        (power > PP_SUMMON_AIRSHIP &&
            (power < PP_AGIT_AUCTION || power == PP_OPEN_CASTLE_DOOR || power == PP_USE_CASTLE_FUNC));
    if (m_data->m_nPledgeType == -1 && !academyPowerAllowed) {
        return false;
    }
    return pledge->CheckPowerByGradeId(m_data->m_nGradeId, power);
}

void User::SetAlliance(CAlliance* const alliance, const bool masterPledge) {
    // Current L2Server 0x00ba1e50.
    L2_GUARD_FUNCTION();

    CVariableBitSet& changed = m_data->m_NeedUpdatePacketBitSet;
    if (alliance == nullptr) {
        changed.SetUnsignedIntValue_IfModifed(m_data->m_nAllianceId, 0, 0x2f);
        m_data->m_nAllianceIndex = static_cast<std::uint32_t>(-1);
        changed.SetUnsignedIntValue_IfModifed(m_nAllianceCrestDbId, 0, 0x31);
        m_bAllianceMaster = 0;
    } else {
        changed.SetUnsignedIntValue_IfModifed(m_data->m_nAllianceId, alliance->m_data->m_nDbId, 0x2f);
        m_data->m_nAllianceIndex = static_cast<std::uint32_t>(alliance->GetIndex());
        changed.SetUnsignedIntValue_IfModifed(m_nAllianceCrestDbId, alliance->m_data->m_nCrestId, 0x31);
        if (masterPledge && m_data->m_bPledgeMaster) {
            m_bAllianceMaster = 1;
        }
    }

    Singleton<CRelationManager>::inst().InitializeRelation(static_cast<std::uint32_t>(m_nRelationSlot));
    m_nParamChanged |= PCB_PLEDGE;
    runtime::raw_atomic::ExchangeI32(&m_nRelationChanged, 2);

    // if (m_data->m_nHeroType != 0 &&
    if (m_data->m_nHeroType != HERO_NONE && Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        Singleton<WHeroSystem>::inst().SetHeroAlliance(this);
    }
}

void User::SetPledge(CPledge* const pledge) {
    // Current L2Server 0x00ba4278.
    L2_GUARD_FUNCTION();

    CVariableBitSet& changed = m_data->m_NeedUpdatePacketBitSet;
    if (pledge == nullptr) {
        changed.SetUnsignedIntValue_IfModifed(m_nPledgeSid, 0, 0x33);
        changed.SetUnsignedIntValue_IfModifed(m_data->m_nPledgeID, 0, 0x2e);
        m_data->m_nPledgeIndex = static_cast<std::uint32_t>(-1);
        m_data->m_bPledgeMaster = false;
        changed.SetUnsignedIntValue_IfModifed(m_nPledgeCrestDbId, 0, 0x30);
        changed.SetUnsignedIntValue_IfModifed(m_nPledgeEmblemDbId, 0, 0x32);
        std::fill_n(m_nCastleSiegeId, 9, 0);
        std::fill_n(m_nCastleSiegeStatus, 9, 0);
        m_nFortressUnderOffenceSiegeId = 0;
        m_nFortressUnderDefenceSiegeId = 0;
        SetAlliance(nullptr, false);
        changed.SetIntValue_IfModifed(m_data->m_nPledgeType, 0, 0x34);
        m_data->m_nGradeId = 0;
    } else {
        changed.SetUnsignedIntValue_IfModifed(m_nPledgeSid, pledge->Id(), 0x33);
        changed.SetUnsignedIntValue_IfModifed(m_data->m_nPledgeID, pledge->GetDbId(), 0x2e);
        m_data->m_nPledgeIndex = static_cast<std::uint32_t>(pledge->GetIndex());
        changed.SetUnsignedIntValue_IfModifed(m_nPledgeCrestDbId, pledge->GetCrestId(), 0x30);
        changed.SetUnsignedIntValue_IfModifed(m_nPledgeEmblemDbId, pledge->GetEmblemId(), 0x32);
        pledge->CopyCastleSiegeStatusToUser(this);
        m_nFortressUnderOffenceSiegeId = static_cast<std::int32_t>(pledge->m_data->m_nFortressUnderOffenceSiegeId);
        m_nFortressUnderDefenceSiegeId = static_cast<std::int32_t>(pledge->m_data->m_nFortressUnderDefenceSiegeId);

        if (nDBID == pledge->GetMasterDbId()) {
            m_data->m_bPledgeMaster = true;
            SetPledgeMasterSkill();
            CheckPremiumType(PremiumUserType::PUT_FREE_USER);
        } else {
            m_data->m_bPledgeMaster = false;
        }

        CAlliance* const alliance = pledge->GetAlliance();
        const bool allianceMaster = alliance != nullptr && alliance->m_data->m_nMasterPledgeDbId == pledge->GetDbId();
        SetAlliance(alliance, allianceMaster);
    }

    Singleton<CRelationManager>::inst().InitializeRelation(static_cast<std::uint32_t>(m_nRelationSlot));
    m_nParamChanged |= PCB_PLEDGE;
    runtime::raw_atomic::ExchangeI32(&m_nRelationChanged, 2);

    // if (m_data->m_nHeroType != 0 &&
    if (m_data->m_nHeroType != HERO_NONE && Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        Singleton<WHeroSystem>::inst().SetHeroPledge(this);
    }
}

void User::SetPledgeMasterSkill() {
    // Current L2Server 0x00b9fafc.
    L2_GUARD_FUNCTION();

    if (GetPledge() == nullptr) {
        return;
    }
    PledgeFunction* const function = GetPledge()->GetFunction();
    if (function->IsEnable(PledgeFunctionType::CASTLE_SIEGE)) {
        AcquireSkill(0x00f7, 1, 0, false, true);
        AcquireSkill(0x4a5a, 1, 0, false, true);
        AcquireSkill(0x4a5b, 1, 0, false, true);
        AcquireSkill(0x3bb3, 1, 0, false, true);
        AcquireSkill(0x3bb4, 1, 0, false, true);
        AcquireSkill(0x3bb5, 1, 0, false, true);
    }
    ValidateChanged(VB_MAX);
}
