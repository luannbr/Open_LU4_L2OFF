#include "PledgeJoinSystem.h"
#include "CConfig.h"
#include "DB.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>

namespace {


std::tm TargetLocalTime32Secure(const std::int32_t value) noexcept {
    std::tm result;
    std::memset(&result, 0xff, sizeof(result));
    static_cast<void>(runtime::crt::LocalTime32(value, result));
    return result;
}

} // namespace

CPledgeJoinSystem::CPledgeJoinSystem() = default;

CPledgeJoinSystem::~CPledgeJoinSystem() = default;

CPledgeJoinSystem* CPledgeJoinSystem::Inst() {
    // Current callers use the compiler-generated function-local static guard
    // around the single 0x288-byte process-lifetime object.
    static CPledgeJoinSystem system;
    return &system;
}

L2_DEFINE_OBJECT_RELEASE(CPledgeJoinSystem)

void CPledgeJoinSystem::Init() {
    // Current L2Server 0x009b4954.
    L2_GUARD_FUNCTION();
    m_mapDraftUser.clear();
    m_mapPledgeBoard.clear();
    m_mapWaitingUser.clear();
    m_mapPledgeNotifyTable.clear();
    db.RequestPledgeJoinSystemInit(0);
    db.RequestLoadPledgeOnPledgeRecruitBoard();
    m_nInitTime = 0x278d00;
}

void CPledgeJoinSystem::SetDelTime(const int days) {
    // Current L2Server 0x009b43e4.
    L2_GUARD_FUNCTION();
    m_nInitTime = runtime::MultiplyInt32Wrapping(days, 0x15180);
}

bool CPledgeJoinSystem::ConfirmTimePenalty(const int key, TimeMap* const table) {
    // Current L2Server 0x009b4b54 locks a local RWLock rather
    // than the table's owning lock.
    L2_GUARD_FUNCTION();
    RWLock localLock;
    const bool allowed = [&] {
        L2_READ_GUARD(localLock);
        const auto found = table->find(key);
        bool allowed = found == table->end();
        if (!allowed) {
            const std::int32_t threshold = runtime::AddInt32Wrapping(found->second, 300);
            allowed = runtime::crt::Time32Now() >= threshold;
        }
        return allowed;
    }();
    return allowed;
}

std::int32_t CPledgeJoinSystem::GetRemainTime(const int key, TimeMap* const table) {
    // Current L2Server 0x009b4cb4 has the same local-lock oddity.
    L2_GUARD_FUNCTION();
    RWLock localLock;
    const std::int32_t remaining = [&] {
        L2_READ_GUARD(localLock);
        const auto found = table->find(key);
        std::int32_t remaining = 0;
        if (found != table->end()) {
            remaining = runtime::AddInt32Wrapping(
                runtime::SubtractInt32Wrapping(found->second, runtime::crt::Time32Now()), 300
            );
        }
        return remaining;
    }();
    return remaining;
}

void CPledgeJoinSystem::SetTimeTable(const int key, TimeMap* const table, RWLock& lock) {
    // Current L2Server 0x009b5100.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(lock);
        const std::int32_t now = runtime::crt::Time32Now();
        const auto found = table->find(key);
        if (found == table->end()) {
            table->emplace(key, now);
        } else {
            found->second = now;
        }
    }
}

void CPledgeJoinSystem::ClearTimeTable(const int key, TimeMap* const table, RWLock& lock) {
    // Current L2Server 0x009b5254.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(lock);
        if (table->find(key) != table->end()) {
            table->erase(key);
        }
    }
}

void CPledgeJoinSystem::ClearPledgeRecruitTimePenalty(const int key) {
    // Current L2Server 0x009b659c.
    ClearTimeTable(key, &m_mapPledgeBoard, m_pledgeLock);
    ClearTimeTable(key, &m_mapPledgeDel, m_pledgeDelLock);
}

void CPledgeJoinSystem::ClearDraftTimePenalty(const int key) {
    // Current L2Server 0x009b65e0.
    ClearTimeTable(key, &m_mapDraftUser, m_draftLock);
    ClearTimeTable(key, &m_mapDraftDel, m_draftDelLock);
}

void CPledgeJoinSystem::ClearWaitTimePenalty(const int key) {
    // Current L2Server 0x009b6624.
    ClearTimeTable(key, &m_mapWaitingUser, m_waitLock);
    ClearTimeTable(key, &m_mapWaitDel, m_waitDelLock);
}

void CPledgeJoinSystem::ClearUserTimePenalty(const int userDatabaseId, const int pledgeDatabaseId) {
    // Current L2Server 0x009b64a8.
    L2_GUARD_FUNCTION();
    if (userDatabaseId != 0) {
        ClearTimeTable(userDatabaseId, &m_mapWaitingUser, m_waitLock);
        ClearTimeTable(userDatabaseId, &m_mapDraftUser, m_draftLock);
    }
    if (pledgeDatabaseId != 0) {
        ClearTimeTable(pledgeDatabaseId, &m_mapPledgeBoard, m_pledgeLock);
    }
}

void CPledgeJoinSystem::ClearUserListByPledgeJoin(User* const user) {
    // Current L2Server 0x009b6384.
    L2_GUARD_FUNCTION();
    ClearTimeTable(static_cast<int>(user->GetDBID()), &m_mapWaitingUser, m_waitLock);
    ClearTimeTable(static_cast<int>(user->GetDBID()), &m_mapDraftUser, m_draftLock);
    db.RequestDeletePledgeWaitingUserList(
        user, static_cast<int>(user->m_data->m_nPledgeID), static_cast<int>(user->GetDBID())
    );
    db.RequestDeletePledgeDraftUserList(user);
}

void CPledgeJoinSystem::InitPledgeInfoPerDay() {
    // Current L2Server 0x009b4320.
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    if (local != nullptr && local->tm_hour == 6 && local->tm_min == 30) {
        db.RequestPledgeJoinSystemInit(m_nInitTime);
    }
}

void CPledgeJoinSystem::RequestModifyPledgeInfo(CPledge* const pledge) {
    L2_GUARD_FUNCTION();
    if (pledge != nullptr && pledge->IsWaitingToDismiss()) {
        db.RequestModifyPledgeInfoOnBoard(pledge);
    }
}

void CPledgeJoinSystem::RequestSearchPledgeRecruitBoard(
    User* const user,
    const char16_t* const pledgeName,
    const int minimumLevel,
    const int maximumLevel,
    const char16_t* const masterName,
    const int pledgeType,
    const int sortCondition,
    const int orderCondition,
    const int searchCondition
) {
    // Current L2Server 0x009b4544.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        db.RequestSearchPledgeRecruitBoard(
            user, pledgeName, minimumLevel, maximumLevel, masterName, pledgeType, sortCondition, orderCondition,
            searchCondition
        );
    }
}

void CPledgeJoinSystem::RequestPledgeOnPledgeRecruitBoard(User* const user, const int pledgeId) {
    // Current L2Server 0x009b464c.
    L2_GUARD_FUNCTION();
    db.RequestPledgeOnPledgeRecruitBoard(user, pledgeId);
}

void CPledgeJoinSystem::RequestPledgeWaitingUserList(User* const user, const int pledgeId) {
    // Current L2Server 0x009b46f8.
    L2_GUARD_FUNCTION();
    db.RequestPledgeWaitingUserList(user, pledgeId);
}

void CPledgeJoinSystem::RequestUserOnPledgeWaitingList(User* const user, const int pledgeId, const int characterId) {
    // Current L2Server 0x009b47a4.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        db.RequestUserOnPledgeWaitingList(user, pledgeId, characterId);
    }
}

void CPledgeJoinSystem::RequestPledgeDraftUserList(
    User* const user,
    const int minimumLevel,
    const int maximumLevel,
    const int classGroup,
    const char16_t* const name,
    const int sortCondition,
    const int orderCondition
) {
    // Current L2Server 0x009b4868.
    L2_GUARD_FUNCTION();
    db.RequestPledgeDraftUserList(user, minimumLevel, maximumLevel, classGroup, name, sortCondition, orderCondition);
}

void CPledgeJoinSystem::RequestModifyPledgeRecruitBoard(
    User* const user,
    const int recruitType,
    const char16_t* const title,
    const char16_t* const description,
    const int sortCondition,
    const PledgeType pledgeType
) {
    // Current L2Server 0x009b4e08.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->IsWaitingToDismiss()) {
        return;
    }
    if (!user->HavePledgePower(PP_MANAGE_GRADE)) {
        user->SendSystemMessage(SystemMessageId::CLAN_INFORMATION_CHANGE_REQUIRES_LEADER_OR_RANK_MANAGEMENT_AUTHORITY);
        return;
    }
    if (!pledge->HasPledgeType(pledgeType)) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Modifying board with wrong pledge type : %d",
        //     static_cast<int>(pledgeType))
        // L2_LOG_DEBUG(Log,
        //     L"Modifying board with wrong pledge type : %d",
        //     static_cast<int>(pledgeType))
        L2_LOG_WARN(
            Log,
            "Character %u cannot modify the recruitment board for clan %u: subclan type %d is not present in this clan.",
            static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(pledge->GetDbId()), static_cast<int>(pledgeType)
        );
        return;
    }
    if (ConfirmTimePenalty(static_cast<int>(pledge->GetDbId()), &m_mapPledgeBoard)) {
        user->SendSystemMessage(SystemMessageId::CLAN_INFORMATION_EDIT_AND_DELETION_PENALTY_NOTICE);
        db.RequestModifyPledgeRecruitBoard(user, recruitType, title, description, sortCondition, pledgeType);
    }
}

void CPledgeJoinSystem::RequestWritePledgeRecruitBoard(
    User* const user,
    const int recruitType,
    const char16_t* const title,
    const char16_t* const description,
    const int sortCondition,
    const PledgeType pledgeType
) {
    // Current L2Server 0x009b5374.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->IsWaitingToDismiss()) {
        return;
    }
    if (!user->HavePledgePower(PP_MANAGE_GRADE)) {
        user->GetSocket()->SendSystemMessage(
            SystemMessageId::CLAN_REGISTRATION_REQUIRES_LEADER_OR_RANK_MANAGEMENT_AUTHORITY
        );
        return;
    }
    if (!pledge->HasPledgeType(pledgeType)) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Writing board with wrong pledge type : %d",
        //     static_cast<int>(pledgeType))
        // L2_LOG_DEBUG(Log,
        //     L"Writing board with wrong pledge type : %d",
        //     static_cast<int>(pledgeType))
        L2_LOG_WARN(
            Log,
            "Character %u cannot publish the recruitment board for clan %u: subclan type %d is not present in this clan.",
            static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(pledge->GetDbId()), static_cast<int>(pledgeType)
        );
        return;
    }

    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    if (!ConfirmTimePenalty(pledgeDatabaseId, &m_mapPledgeBoard)) {
        const std::int32_t remaining = GetRemainTime(pledgeDatabaseId, &m_mapPledgeBoard);
        if (remaining < 60) {
            user->GetSocket()->Send(
                "chccc", 0x62, SystemMessageId::CLAN_REGISTRATION_DELAY_AFTER_POST_DELETION_IN_SECONDS, 1, 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(remaining)))
            );
        } else {
            user->GetSocket()->Send(
                "chccc", 0x62, SystemMessageId::CLAN_REGISTRATION_DELAY_AFTER_POST_DELETION_IN_MINUTES, 1, 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(remaining / 60)))
            );
        }
        return;
    }

    user->SendSystemMessage(SystemMessageId::CLAN_RECRUITMENT_LIST_REGISTERED_WITH_EXPIRATION_RULES);
    SetTimeTable(pledgeDatabaseId, &m_mapPledgeDel, m_pledgeDelLock);
    db.RequestWritePledgeRecruitBoard(user, recruitType, title, description, sortCondition, pledgeType);

    std::u16string targetDescription = Utf16Text(description).String();
    if (targetDescription.size() > 199U) {
        targetDescription.resize(199U);
    }
    const std::u16string targetTitle = Utf16Text(title).String();
    const std::u16string targetPledgeName = std::u16string(pledge->GetPledgeName());
    const std::u16string targetUserName = std::u16string(user->GetName());
    const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    CLogFormatter formatter;
    formatter.WriteLog(
        0x810, user->GetDBID(), user->GetAccountID(), 0, 0, x, y, z, targetDescription.c_str(), targetTitle.c_str(),
        targetPledgeName.c_str(), pledgeDatabaseId, pledge->GetPledgeLevel(), recruitType, sortCondition,
        static_cast<int>(pledgeType), 0, 0, 0, 0, 0, targetUserName.c_str(), user->m_sAccountName.data(), nullptr, nullptr, 0
    );
}

void CPledgeJoinSystem::RequestDeletePledgeRecruitBoard(User* const user, const bool fromDatabase) {
    // Current L2Server 0x009b5814.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->HavePledgePower(PP_MANAGE_GRADE)) {
        return;
    }
    if (!fromDatabase) {
        SetTimeTable(static_cast<int>(pledge->GetDbId()), &m_mapPledgeBoard, m_pledgeLock);
    }
    ClearTimeTable(runtime::Int32FromBits(user->m_data->m_nPledgeID), &m_mapPledgeDel, m_pledgeDelLock);
    pledge->ClearRecruitInfo();
    db.RequestDeletePledgeRecruitBoard(user);
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x812, user, nullptr, nullptr, pledge->GetPledgeName(), nullptr, pledge->GetDbId(), pledge->GetStatus(), 0, 0, 0, 0,
        0
    );
}

void CPledgeJoinSystem::RequestDeletePledgeRecruitBoard(CPledge* const pledge, bool) {
    // Current L2Server 0x009b59cc.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    ClearTimeTable(static_cast<int>(pledge->GetDbId()), &m_mapPledgeDel, m_pledgeDelLock);
    pledge->ClearRecruitInfo();
    db.RequestDeletePledgeRecruitBoard(pledge);
}

void CPledgeJoinSystem::SetNotifyTable(const int pledgeDatabaseId, const std::int32_t notifyTime) {
    // Current L2Server 0x009b4fac.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_notifyLock);
        const auto found = m_mapPledgeNotifyTable.find(pledgeDatabaseId);
        if (found == m_mapPledgeNotifyTable.end()) {
            m_mapPledgeNotifyTable.emplace(pledgeDatabaseId, notifyTime);
        } else {
            found->second = notifyTime;
        }
    }
}

std::int32_t CPledgeJoinSystem::GetNotifyTable(const int pledgeDatabaseId) {
    // Current L2Server 0x009b4a34.
    L2_GUARD_FUNCTION();

    // The original code reads the value after releasing the write lock.
    const std::int32_t* const notifyTime = [&]() -> const std::int32_t* {
        L2_WRITE_GUARD(m_notifyLock);
        const auto found = m_mapPledgeNotifyTable.find(pledgeDatabaseId);
        return found == m_mapPledgeNotifyTable.end() ? nullptr : &found->second;
    }();
    // const std::int32_t notifyTime =
    //     found == m_mapPledgeNotifyTable.end() ? 0 : found->second.
    return notifyTime != nullptr ? *notifyTime : 0;
}

void CPledgeJoinSystem::SendRemainTimeOnPledge(User* const user, const int remainingTime) {
    // Current L2Server 0x009b6668.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::int32_t pledgeDatabaseId = runtime::Int32FromBits(user->m_data->m_nPledgeID);
    if (m_mapPledgeDel.find(pledgeDatabaseId) == m_mapPledgeDel.end()) {
        m_mapPledgeDel.emplace(pledgeDatabaseId, remainingTime);
    }

    const std::tm local = TargetLocalTime32Secure(runtime::AddInt32Wrapping(m_nInitTime, remainingTime));
    user->GetSocket()->Send(
        "chcchcccc", 0x62, SystemMessageId::CLAN_INTRODUCTION_SCHEDULED_DELETION_DATE, 3, 0x15,
        static_cast<int>(static_cast<std::int16_t>(local.tm_year + 0x76c)), 0x14,
        static_cast<int>(static_cast<std::int8_t>(local.tm_mon + 1)), 0x14,
        static_cast<int>(static_cast<std::int8_t>(local.tm_mday))
    );
}

void CPledgeJoinSystem::SendRemainTimeOnUser(User* const user, const int type, const int remainingTime) {
    // Current L2Server 0x009b6840.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::tm local = TargetLocalTime32Secure(runtime::AddInt32Wrapping(m_nInitTime, remainingTime));
    std::uint32_t messageId = 0;
    if (type == 1) {
        const std::int32_t databaseId = runtime::Int32FromBits(user->GetDBID());
        if (m_mapWaitDel.find(databaseId) == m_mapWaitDel.end()) {
            m_mapWaitDel.emplace(runtime::Int32FromBits(user->GetDBID()), remainingTime);
        }
        messageId = SystemMessageId::CLAN_JOIN_APPLICATION_EXPIRATION_DATE;
    } else if (type == 2) {
        const std::int32_t databaseId = runtime::Int32FromBits(user->GetDBID());
        if (m_mapDraftDel.find(databaseId) == m_mapDraftDel.end()) {
            m_mapDraftDel.emplace(runtime::Int32FromBits(user->GetDBID()), remainingTime);
        }
        messageId = SystemMessageId::CLAN_JOIN_WAITING_LIST_EXPIRATION_DATE;
    } else {
        return;
    }

    user->GetSocket()->Send(
        "chcchcccc", 0x62, messageId, 3, 0x15, static_cast<int>(static_cast<std::int16_t>(local.tm_year + 0x76c)), 0x14,
        static_cast<int>(static_cast<std::int8_t>(local.tm_mon + 1)), 0x14,
        static_cast<int>(static_cast<std::int8_t>(local.tm_mday))
    );
}

void CPledgeJoinSystem::RequestWritePledgeWaitingUserList(User* const user, const int pledgeId, const char16_t* const comment) {
    // Current L2Server 0x009b5aa8.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetPledge() != nullptr) {
        return;
    }

    if (!ConfirmTimePenalty(static_cast<int>(user->GetDBID()), &m_mapWaitingUser)) {
        const std::int32_t remaining = GetRemainTime(static_cast<int>(user->GetDBID()), &m_mapWaitingUser);
        if (remaining < 60) {
            user->GetSocket()->Send(
                "chccc", 0x62, SystemMessageId::CLAN_APPLICATION_DELAY_AFTER_CANCELLATION_IN_SECONDS, 1, 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(remaining)))
            );
        } else {
            user->GetSocket()->Send(
                "chccc", 0x62, SystemMessageId::CLAN_APPLICATION_DELAY_AFTER_CANCELLATION_IN_MINUTES, 1, 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(remaining / 60)))
            );
        }
        return;
    }

    user->SendSystemMessage(SystemMessageId::CLAN_JOIN_APPLICATION_SUBMITTED_WITH_EXPIRATION_RULES);
    SetNotifyTable(pledgeId, runtime::crt::Time32Now());
    SetTimeTable(pledgeId, &m_mapWaitDel, m_waitDelLock);
    db.RequestWritePledgeWaitingUserList(user, pledgeId, comment);

    // Current code releases the temporary kept lookup before consuming
    // the raw pointer. Preserve that lifetime ordering literally.
    CPledge* const pledge = CPledge::FindPledge(g_nWorldId, pledgeId).get();
    if (pledge != nullptr) {
        std::u16string sanitizedComment = Utf16Text(comment).String();
        if (sanitizedComment.size() > 50U) {
            sanitizedComment.resize(50U);
        }
        std::replace(sanitizedComment.begin(), sanitizedComment.end(), u',', u' ');
        std::replace(sanitizedComment.begin(), sanitizedComment.end(), u'\r', u' ');
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x811, user, nullptr, sanitizedComment, pledge->GetPledgeName(), nullptr, pledge->GetDbId(),
            pledge->GetPledgeLevel(), 0, 0, 0, 0, 0
        );
    }
}

void CPledgeJoinSystem::RequestDeletePledgeWaitingUserList(User* const user, const bool fromDatabase) {
    // Current L2Server 0x009b5da8.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetPledge() != nullptr) {
        return;
    }
    user->SendSystemMessage(SystemMessageId::CLAN_JOIN_APPLICATION_CANCELLED_WITH_REAPPLY_DELAY);
    const int userDatabaseId = static_cast<int>(user->GetDBID());
    if (!fromDatabase) {
        SetTimeTable(userDatabaseId, &m_mapWaitingUser, m_waitLock);
    }
    ClearTimeTable(userDatabaseId, &m_mapWaitDel, m_waitDelLock);
    db.RequestDeletePledgeWaitingUserList(user, user->m_data->m_nWorldId, userDatabaseId);
    CLogFormatter formatter;
    formatter.WriteLog1(0x813, user, nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
}

void CPledgeJoinSystem::RequestDeletePledgeWaitingUserList(const char16_t* const userName, bool) {
    // Current L2Server 0x009b5f58.
    L2_GUARD_FUNCTION();
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, std::u16string(userName != nullptr ? userName : u""));
    int worldId = g_nWorldId;
    if (target) {
        ClearTimeTable(static_cast<int>(target->GetDBID()), &m_mapWaitDel, m_waitDelLock);
        worldId = target->m_data->m_nWorldId;
    }
    db.RequestDeletePledgeWaitingUserList(userName, worldId);
}

void CPledgeJoinSystem::RequestWriteDraftUserList(User* const user, const int value) {
    // Current L2Server 0x009b60c0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetPledge() != nullptr) {
        return;
    }
    user->SendSystemMessage(SystemMessageId::CLAN_WAITING_LIST_REGISTERED_WITH_EXPIRATION_RULES);
    if (ConfirmTimePenalty(static_cast<int>(user->GetDBID()), &m_mapDraftUser)) {
        SetTimeTable(static_cast<int>(user->GetDBID()), &m_mapDraftDel, m_draftDelLock);
        db.RequestWritePledgeDraftUserList(user, value);
        return;
    }

    const std::int32_t remaining = GetRemainTime(static_cast<int>(user->GetDBID()), &m_mapDraftUser);
    if (remaining < 60) {
        user->GetSocket()->Send(
            "chccc", 0x62, SystemMessageId::CLAN_WAITING_LIST_DELAY_AFTER_CANCELLATION_IN_SECONDS, 1, 0x14,
            static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(remaining)))
        );
    } else {
        user->GetSocket()->Send(
            "chccc", 0x62, SystemMessageId::CLAN_WAITING_LIST_DELAY_AFTER_CANCELLATION_IN_MINUTES, 1, 0x14,
            static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(remaining / 60)))
        );
    }
}

void CPledgeJoinSystem::RequestDeleteDraftUserList(User* const user) {
    // Current L2Server 0x009b628c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    SetTimeTable(static_cast<int>(user->GetDBID()), &m_mapDraftUser, m_draftLock);
    ClearTimeTable(static_cast<int>(user->GetDBID()), &m_mapDraftDel, m_draftDelLock);
    db.RequestDeletePledgeDraftUserList(user);
}

void CPledgeJoinSystem::RequestPledgeJoinRemainTime(User* const user) {
    // Current L2Server 0x009b6e90 performs these lookups
    // without the maps' RWLocks.
    L2_GUARD_FUNCTION();
    const int pledgeDatabaseId = runtime::Int32FromBits(user->m_data->m_nPledgeID);
    if (pledgeDatabaseId != 0) {
        const auto pledge = m_mapPledgeDel.find(pledgeDatabaseId);
        if (pledge == m_mapPledgeDel.end()) {
            db.RequestPledgeRemainTime(user);
        } else {
            SendRemainTimeOnPledge(user, pledge->second);
        }
        return;
    }

    const auto waiting = m_mapWaitDel.find(static_cast<int>(user->GetDBID()));
    const auto draft = m_mapDraftDel.find(static_cast<int>(user->GetDBID()));
    if (waiting == m_mapWaitDel.end()) {
        if (draft == m_mapDraftDel.end()) {
            db.RequestUserRemainTime(user);
        } else {
            SendRemainTimeOnUser(user, 2, draft->second);
        }
    } else {
        SendRemainTimeOnUser(user, 1, waiting->second);
    }
}

void CPledgeJoinSystem::RequestRejectPledgeWiatingUser(User* const user, const int worldId, const int characterId) {
    // Current L2Server 0x009b73c0 keeps the original Wiating typo.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::u16string targetUserName = std::u16string(user->GetName());
    user->GetSocket()->Send(
        "chccS", 0x62, SystemMessageId::CHARACTER_CLAN_APPLICATION_REJECTED, 1, 0x0c, targetUserName.c_str()
    );
    CFHTML::StrStrMapType parameters;
    const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"g_pledgejoinsystem001.htm", parameters, 0);
    const std::u16string contents = CFHTML::MakeHTMLFromStrMap(u"g_pledgejoinsystem002.htm", parameters, 0);
    db.RequestWritePostBirth(characterId, title.c_str(), contents.c_str(), -1, 10, 0, static_cast<int>(user->GetDBID()), 0);
    ClearTimeTable(characterId, &m_mapWaitingUser, m_waitLock);
    db.RequestDeletePledgeWaitingUserList(user, worldId, characterId);
}
