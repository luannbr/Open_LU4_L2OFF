#include "DBSocketOp.h"

#include "CommissionTrade.h"
#include "DB.h"
#include "Friends.h"
#include "MakerEvent.h"
#include "Pet.h"
#include "PetDataSheet.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "SSQ.h"
#include "Setting.h"
#include "SkillAcquire.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace {

CSPointer<User> FindUserByDatabaseId(const int databaseId) {
    return g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), databaseId));
}

bool IsLoggedIn(const CSPointer<User>& user) {
    if (!user) {
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    return socket != nullptr && socket->IsLoggedIn();
}

int TargetCoordinate(const double value) noexcept { return runtime::Win64CvttDoubleToInt32(value); }

} // namespace

CDBSocketOp::CDBSocketOp(const int type) {
    L2_GUARD_FUNCTION();
    m_nType = type;
}

CDBSocketOp::~CDBSocketOp() = default;

void CDBSocketOp::SendHome(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5c9c.
    L2_GUARD_FUNCTION();

    int characterDatabaseId = 0;
    int race = 0;
    std::array<char16_t, 25> characterName{};
    int x = 0;
    int y = 0;
    int z = 0;
    int karma = 0;
    ::Disassemble(packet, "ddSdddd", &characterDatabaseId, &race, 0x32, characterName.data(), &x, &y, &z, &karma);

    CSPointer<User> user = FindUserByDatabaseId(characterDatabaseId);
    if (IsLoggedIn(user)) {
        user->StopMove();
        const FVector restart = Singleton<CSetting>::inst().GetRestartPoint(
            user->m_data->m_Race, user->GetPos(), user->m_data->m_nKarma < 0, user->m_data->m_nPKCount
        );
        user->TeleportToLocation(
            TargetCoordinate(restart.x), TargetCoordinate(restart.y), TargetCoordinate(restart.z), 0, 0x10000,
            TeleportSourceType::SendHome, nullptr, 0
        );
        return;
    }

    const FVector position{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    const FVector restart = Singleton<CSetting>::inst().GetRestartPoint(static_cast<RaceType>(race), position, karma < 0, 0);
    const std::u16string characterNameText(FixedUtf16View(characterName));
    db.RequestChangePos(
        characterNameText.c_str(), TargetCoordinate(restart.x), TargetCoordinate(restart.y), TargetCoordinate(restart.z)
    );
}

void CDBSocketOp::PledgeLev(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5040.
    L2_GUARD_FUNCTION();

    int pledgeDatabaseId = 0;
    int level = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &level);
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (pledge) {
        pledge->SetPledgeLevel(static_cast<unsigned int>(level), true);
        pledge->UpdateInfoWindowPacket();
    }
}

void CDBSocketOp::SetSkillAll(const std::uint8_t* const packet) {
    // Current L2Server 0x006d60cc.
    L2_GUARD_FUNCTION();

    int characterDatabaseId = 0;
    int classType = 0;
    int maximumLevel = 0;
    int raceCondition = 0;
    std::array<char16_t, 25> characterName{};
    ::Disassemble(
        packet, "ddddS", &characterDatabaseId, &classType, &maximumLevel, &raceCondition, 0x32, characterName.data()
    );

    const auto& skills = g_SkillAcquireDB.GetAcquirableSkills(static_cast<ClassType>(classType));
    CSPointer<User> user = FindUserByDatabaseId(characterDatabaseId);
    if (!IsLoggedIn(user)) {
        const RaceType race = static_cast<RaceType>(raceCondition);
        for (const SkillAcquireInfo* const skill : skills) {
            if ((skill->m_RaceConditions.empty() || skill->m_RaceConditions.find(race) != skill->m_RaceConditions.end()) &&
                skill->nGetLevel <= maximumLevel) {
                db.SendAcquiredSkillForAdmin(characterDatabaseId, skill->nSkillID, skill->nSkillLevel, 0, false);
            }
        }
        return;
    }
    user->SetSkillAll(classType);
}

void CDBSocketOp::ManIntAnn(const std::uint8_t*) {
    // Current L2Server 0x006d462c.
    L2_GUARD_FUNCTION();
    db.LoadIntervalAnnounce();
}

void CDBSocketOp::ModWeekPlay(const std::uint8_t* const packet) {
    // Current L2Server 0x006d6360.
    L2_GUARD_FUNCTION();

    int characterDatabaseId = 0;
    int type = 0;
    int value = 0;
    ::Disassemble(packet, "ddd", &characterDatabaseId, &type, &value);
    CSPointer<User> user = FindUserByDatabaseId(characterDatabaseId);
    if (user && type == 1) {
        db.RequestChangeTimeLimit(user.get(), user->GetName(), 1, value, user->GetServerId());
    } else if (user && type == 2) {
        db.RequestChangeTimeLimit(user.get(), user->GetName(), 2, value, user->GetServerId());
    }
}

void CDBSocketOp::ChangeCharName(const std::uint8_t* const packet) {
    // Current L2Server 0x006d64ec.
    L2_GUARD_FUNCTION();

    std::uint32_t characterDatabaseId = 0;
    std::array<char16_t, 25> newName{};
    std::array<char16_t, 25> oldName{};
    int pledgeDatabaseId = 0;
    std::uint32_t classType = 0;
    int listenerCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dSSddd", &characterDatabaseId, 0x32, newName.data(), 0x32, oldName.data(), &pledgeDatabaseId, &classType,
        &listenerCount
    );

    std::wstring hostNewName = FixedUtf16ToWide(newName);
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (pledge) {
        pledge->ChangeMemberName(characterDatabaseId, hostNewName.data());
    }

    for (int index = 0; index < listenerCount; ++index) {
        int listenerDatabaseId = 0;
        cursor = ::Disassemble(cursor, "d", &listenerDatabaseId);
        CSPointer<User> listener = FindUserByDatabaseId(listenerDatabaseId);
        if (listener) {
            CSPointer<CFriends> friends = listener->GetFriends();
            if (friends) {
                friends->ChangeFriendName(characterDatabaseId, std::u16string(FixedUtf16View(newName)).c_str());
            }
        }
    }

    if (classType > 147U) {
        SharedOlympiadInfo::GetInfo()->ChangeRankName(static_cast<ClassType>(classType), oldName.data(), newName.data());
    }
}

void CDBSocketOp::GmReg(const std::uint8_t* const packet) {
    // Current L2Server 0x006d6760.
    L2_GUARD_FUNCTION();

    int characterDatabaseId = 0;
    int action = 0;
    ::Disassemble(packet, "dd", &characterDatabaseId, &action);
    CSPointer<User> user = FindUserByDatabaseId(characterDatabaseId);
    if (!IsLoggedIn(user)) {
        return;
    }

    std::uint32_t userDatabaseId;
    std::uint32_t accountId;
    std::optional<std::wstring> userNameStorage;
    std::array<char16_t, 15> accountName;
    std::optional<FVector> positionStorage;
    {
        L2_GUARD(*user->m_lock);
        userDatabaseId = user->GetDBID();
        accountId = user->GetAccountID();
        userNameStorage.emplace(Utf16ToWide(user->GetName()));
        accountName = user->m_sAccountName;
        positionStorage.emplace(user->GetPos());
    }
    const auto& userName = *userNameStorage;
    const auto& position = *positionStorage;

    const int gmState = action == 0 ? 1 : 0;
    if (action == 0) {
        CGMListMgr::inst().Insert(user->GetName());
    } else {
        CGMListMgr::inst().Delete(user->GetName());
    }

    const std::wstring hostAccountName = FixedUtf16ToWide(accountName);
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,,,,,,,,,,%s,%s,,,",
        std::int64_t{0x203}, static_cast<std::int64_t>(userDatabaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(TargetCoordinate(position.x)), static_cast<std::int64_t>(TargetCoordinate(position.y)),
        static_cast<std::int64_t>(TargetCoordinate(position.z)), static_cast<std::int64_t>(gmState), userName.c_str(),
        hostAccountName.c_str()
    );
}

void CDBSocketOp::CreateSubjob(const std::uint8_t* const packet) {
    // Current L2Server 0x006d6bd8.
    L2_GUARD_FUNCTION();

    int characterDatabaseId = 0;
    int subJobId = 0;
    int newClassId = -1;
    ::Disassemble(packet, "ddd", &characterDatabaseId, &subJobId, &newClassId);
    CSPointer<User> user = FindUserByDatabaseId(characterDatabaseId);
    if (!IsLoggedIn(user)) {
        return;
    }

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(static_cast<int>(user->Id()));
    if (socket && socket->IsLoggedIn()) {
        User* const socketUser = socket->GetUser();
        if (socketUser != nullptr) {
            db.RequestCreateSubJob(socketUser, subJobId, newClassId, true);
        }
    }
}

void CDBSocketOp::SetInstantAnnounce(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5194.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 50> message{};
    ::Disassemble(packet, "S", 100, message.data());
    CUserBroadcaster::inst().BroadcastToAllUserAnnounce(message.data());
}

void CDBSocketOp::ModDepositedSSQItem(const std::uint8_t* const packet) {
    // Current L2Server 0x006d46c4.
    L2_GUARD_FUNCTION();

    int rawPart = 0;
    std::int32_t delta = 0;
    ::Disassemble(packet, "dd", &rawPart, &delta);
    SSQSystem& ssq = SSQSystem::inst();
    {
        L2_WRITE_GUARD(ssq);
        const SSQPart::type part = static_cast<SSQPart::type>(rawPart);
        const std::int32_t current = static_cast<std::int32_t>(ssq.GetCollectedPoint(part));
        const std::int32_t updated = runtime::AddInt32Wrapping(current, delta);
        ssq.SetCollectedPoint(part, static_cast<std::int64_t>(updated));
    }
}

void CDBSocketOp::CreatePet(const std::uint8_t* const packet) {
    // Current L2Server 0x006d47c8.
    L2_GUARD_FUNCTION();

    int characterDatabaseId = 0;
    int itemClassId = 0;
    int level = 0;
    ::Disassemble(packet, "ddd", &characterDatabaseId, &itemClassId, &level);
    const int npcClassId = PetDataSheetTable::Inst().NpcClassIdFromItemClassID(itemClassId);
    if (npcClassId >= 0) {
        CPet::CreatePetForAdmin(characterDatabaseId, itemClassId, npcClassId, level);
    }
}

void CDBSocketOp::OustPledgeMember(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5618.
    L2_GUARD_FUNCTION();

    int pledgeDatabaseId = 0;
    std::uint32_t characterDatabaseId = 0;
    std::array<char16_t, 25> characterName{};
    ::Disassemble(packet, "ddS", &pledgeDatabaseId, &characterDatabaseId, 0x32, characterName.data());
    if (characterDatabaseId == 0) {
        return;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (pledge) {
        const std::wstring hostCharacterName = FixedUtf16ToWide(characterName);
        pledge->Oust(characterDatabaseId, hostCharacterName.c_str());
    }
}

void CDBSocketOp::DelPledgeAnnounce(const std::uint8_t* const packet) {
    // Current L2Server 0x006d48b0.
    L2_GUARD_FUNCTION();

    std::uint32_t first = 0;
    int second = 0;
    std::uint32_t third = 0;
    std::array<char16_t, 256> message{};
    int fifth = 1;
    ::Disassemble(packet, "dddSd", &first, &second, &third, 0x200, message.data(), &fifth);
    if (third != 0) {
        const std::u16string messageText(FixedUtf16View(message));
        db.RequestSetPledgeAnnounce(first, third, second, messageText.c_str());
    }
}

void CDBSocketOp::DelPledgeEmblem(const std::uint8_t* const packet) {
    // Current L2Server 0x006d49f4.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    if (pledgeDatabaseId != 0) {
        db.RequestDeletePledgeEmblem(pledgeDatabaseId);
    }
}

void CDBSocketOp::SetNobless(const std::uint8_t* const packet) {
    // Current L2Server 0x006d4aac.
    L2_GUARD_FUNCTION();
    int value = 0;
    ::Disassemble(packet, "d", &value);
}

void CDBSocketOp::SetHero(const std::uint8_t* const packet) {
    // Current L2Server 0x006d4b50.
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    ::Disassemble(packet, "ddd", &first, &second, &third);
}

void CDBSocketOp::AddPledgeSkill(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5860.
    L2_GUARD_FUNCTION();

    int pledgeDatabaseId = 0;
    int skillId = 0;
    int skillLevel = 0;
    std::int32_t expiryTime = 0;
    const std::uint8_t* const cursor = ::Disassemble(packet, "ddd", &pledgeDatabaseId, &skillId, &skillLevel);
    ::Disassemble(cursor, "d", &expiryTime);
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (pledge) {
        pledge->AddPledgeSkillWithNotify(static_cast<unsigned int>(skillId), skillLevel, expiryTime);
        pledge->SavePledgeSkillToDB(skillId, skillLevel, expiryTime);
        pledge->ApplyPledgeSkillsToMembers(false, true, false);
    }
}

void CDBSocketOp::ModPledgeSkill(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5a28.
    L2_GUARD_FUNCTION();
    AddPledgeSkill(packet);
}

void CDBSocketOp::DelPledgeSkill(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5b78.
    L2_GUARD_FUNCTION();

    int pledgeDatabaseId = 0;
    int skillId = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &skillId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (pledge) {
        pledge->RemovePledgeSkill(skillId, true);
    }
}

void CDBSocketOp::LoadNobless(const std::uint8_t* const packet) {
    // Current L2Server 0x006d4cc0.
    L2_GUARD_FUNCTION();
    int value = 0;
    ::Disassemble(packet, "d", &value);
}

void CDBSocketOp::AddSubPledgeSkill(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5750.
    L2_GUARD_FUNCTION();

    int pledgeDatabaseId = 0;
    int subPledgeType = 0;
    int skillId = 0;
    int skillLevel = 0;
    ::Disassemble(packet, "dddd", &pledgeDatabaseId, &subPledgeType, &skillId, &skillLevel);
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    static_cast<void>(pledge);
}

void CDBSocketOp::DelSubPledgeSKill(const std::uint8_t*) {
    // Current L2Server 0x006d4c0c.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d]DEPRECATED!!!!!",
    //     L"dbsocketop.cpp",
    //     0x245)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d]DEPRECATED!!!!!",
    //     L"dbsocketop.cpp",
    //     0x245)
    L2_LOG_WARN(Log, "Ignoring deprecated sub-pledge skill deletion request; no skill is removed.");
}

void CDBSocketOp::ModSubPledgeSkill(const std::uint8_t* const packet) {
    // Current L2Server 0x006d5ad0.
    L2_GUARD_FUNCTION();
    AddSubPledgeSkill(packet);
}

void CDBSocketOp::BroadcastInstantAnnounce(const std::uint8_t* packet) {
    // Current L2Server 0x006d5270.
    L2_GUARD_FUNCTION();

    int count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        int ignored = 0;
        std::array<char16_t, 256> message{};
        packet = ::Disassemble(packet, "dS", &ignored, 0x200, message.data());
        CUserBroadcaster::inst().BroadcastToAllUserAnnounce(message.data());
    }
}

void CDBSocketOp::SetPetitionDisableTime(const std::uint8_t* const packet) {
    // Current L2Server 0x006d4d64.
    L2_GUARD_FUNCTION();

    int first = 0;
    int second = 0;
    ::Disassemble(packet, "dd", &first, &second);
    petitionCli.SetDisableTime(first, second);
}

void CDBSocketOp::SpawnEventNpc(const std::uint8_t* const packet) {
    // Current L2Server 0x006d6da4.
    L2_GUARD_FUNCTION();

    int eventIndex = 0;
    NpcPosEventRewardInfo reward{};
    std::uint32_t itemCountLow = 0;
    ::Disassemble(
        packet, "ddddddddd", &eventIndex, &reward.nRewardItemEnable, &reward.nRewardItemResetDB, &itemCountLow,
        &reward.nNpcExistTimeDay, &reward.nResetInzoneBindingEnabled, &reward.nResetInzoneBindingResetDB,
        &reward.nResetInzoneBindingStartSec, &reward.nResetInzoneBindingEndSec
    );
    reward.nItemCount = static_cast<std::int64_t>(itemCountLow);

    if (eventIndex < 0 || eventIndex > 5) {
        // Log.Add(LOG_ERROR, L"SpawnEventNpc error : %d ")
        // L2_LOG_ERROR(Log, L"SpawnEventNpc error : %d ")
        L2_LOG_ERROR(
            Log, "Cannot trigger NPC spawn event: index %d is outside 0 through 5; no event is triggered.", eventIndex
        );
    } else if (eventIndex < 2) {
        reward.nEventVariable = 0;
        NpcPosEventMap::inst().TriggerRewardNpcPosEvent(u"ev_apology", eventIndex, reward);
    } else if (eventIndex < 4) {
        NpcPosEventMap::inst().TriggerNpcPosEvent(u"fall_of_dragon", eventIndex - 2);
    } else {
        NpcPosEventMap::inst().TriggerNpcPosEvent(u"event_baseball_2013", eventIndex - 4);
    }
}

void CDBSocketOp::CancelCommission(const std::uint8_t* const packet) {
    // Current L2Server 0x006d4ee8.
    L2_GUARD_FUNCTION();

    std::int64_t commissionId = 0;
    CommissionFirst::Enum firstType = CommissionFirst::FirstMAX;
    CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
    ::Disassemble(packet, "Qdd", &commissionId, &firstType, &expiredType);
    CCommissionTradeManager::Inst().DeleteCommissionItem(commissionId, firstType);
    CCommissionTradeManager::Inst().DeleteExpiredCommissionData(commissionId, expiredType);
}

void CDBSocketOp::BroadcastScreenAnnounce(const std::uint8_t* packet) {
    // Current L2Server 0x006d53a8.
    L2_GUARD_FUNCTION();

    int count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        int ignored = 0;
        std::array<char16_t, 256> message{};
        packet = ::Disassemble(packet, "dS", &ignored, 0x200, message.data());
        const std::wstring hostMessage = FixedUtf16ToWide(message);
        CUserBroadcaster::inst().BroadcastToAllUserScreenAnnounce(hostMessage.c_str());
    }
}

void CDBSocketOp::IBroadcastScreenAnnounce(const std::uint8_t* packet) {
    // Current L2Server 0x006d54e0.
    L2_GUARD_FUNCTION();

    int count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        int ignored = 0;
        std::array<char16_t, 256> message{};
        packet = ::Disassemble(packet, "dS", &ignored, 0x200, message.data());
        const std::wstring hostMessage = FixedUtf16ToWide(message);
        CUserBroadcaster::inst().BroadcastToAllUserScreenAnnounceEX(hostMessage.c_str());
    }
}

void CDBSocketOp::TakeGMAnnounce(const std::uint8_t*) {
    // Current L2Server 0x006d4e28.
    L2_GUARD_FUNCTION();
    db.LoadReservationAnnounce();
}
