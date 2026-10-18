#include "CConfig.h"
#include "DB.h"
#include "Friends.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "ReplyChecker.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "SkillAcquire.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::u16string ToBlockListName(const char16_t* value) {
    if (value == nullptr) {
        return std::u16string();
    }
    return std::u16string(value);
}

int CompareBlockNameCaseInsensitive(const char16_t* left, const char16_t* right) noexcept {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return ::_wcsicmp(reinterpret_cast<const wchar_t*>(left), reinterpret_cast<const wchar_t*>(right));
#else
    while (*left != u'\0' && *right != u'\0') {
        const wint_t foldedLeft = std::towlower(static_cast<wint_t>(*left));
        const wint_t foldedRight = std::towlower(static_cast<wint_t>(*right));
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == u'\0' ? -1 : 1;
#endif
}

void CopyTargetMentorName(char16_t (&destination)[25], const char16_t* source) noexcept {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }

    std::size_t index = 0;
    while (index < 25 && source[index] != u'\0') {
        destination[index] = source[index];
        ++index;
    }
    if (index < 25) {
        destination[index] = u'\0';
    } else {
        destination[0] = u'\0';
    }
}


// Copy a name that fits the 25-unit buffer. A longer name leaves it empty.
void CopyMentorName(std::array<char16_t, 25>& destination, const char16_t* source) {
    destination.fill(u'\0');
    if (source == nullptr) {
        return;
    }
    std::size_t length = 0;
    while (length < destination.size() && source[length] != u'\0') {
        ++length;
    }
    if (length >= destination.size()) {
        return;
    }
    std::copy_n(source, length, destination.data());
}

std::u16string LowerMentorName(const char16_t* source) {
    if (source == nullptr) {
        return std::u16string();
    }
    const std::u16string targetName(source);
    std::u16string lowered(targetName.size() + 1, u'\0');
    ToLowerCase(
        reinterpret_cast<const SharedUtilsWideChar*>(targetName.c_str()),
        reinterpret_cast<SharedUtilsWideChar*>(lowered.data())
    );
    lowered.resize(targetName.size());
    return lowered;
}


} // namespace

void User::DeleteMentee(int menteeDatabaseId) {
    // Current L2Server 0x00b71b68, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(m_Mentor.DeleteMentee(static_cast<std::uint32_t>(menteeDatabaseId), false));
}

void User::ResetMentor() {
    // Current L2Server 0x00b72054, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(m_Mentee.ResetMentor(false));
}

void User::SetMentor(User* mentor) {
    // Current L2Server 0x00b727f0, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    MentorInfo mentorInfo;
    mentorInfo.nCharDBId = static_cast<std::int32_t>(mentor->GetDBID());
    CopyTargetMentorName(mentorInfo.sCharName, mentor->GetName());
    mentorInfo.nLevel = static_cast<std::int16_t>(mentor->Level());
    mentorInfo.nClass = static_cast<std::int16_t>(mentor->m_data->m_nClass);
    mentorInfo.nLogin = MentorState::LOGIN;
    std::memcpy(&m_Mentee.m_Mentor, &mentorInfo, sizeof(mentorInfo));
    AcquireMentorMenteeSkills(ESAT_MENTEE);
    m_Mentee.SomethingChange();
}

void User::ReplyAddMentee(User* mentee) {
    // Current L2Server 0x00b73538, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();
    if (mentee == nullptr) {
        return;
    }

    MentorInfo mentorInfo;
    mentorInfo.nCharDBId = static_cast<std::int32_t>(mentee->GetDBID());
    CopyTargetMentorName(mentorInfo.sCharName, mentee->GetName());
    mentorInfo.nClass = static_cast<std::int16_t>(mentee->m_data->m_nClass);
    mentorInfo.nLevel = static_cast<std::int16_t>(mentee->Level());
    mentorInfo.nLogin = MentorState::LOGIN;
    if (!m_Mentor.AddMentee(static_cast<std::uint32_t>(mentee->GetDBID()), mentorInfo)) {
        return;
    }

    mentee->SetMentor(this);
    mentee->SendMentorList();
    SendMentorList();
    g_MenteeWaitingList.DeleteWaitingList(mentee);
    if (m_Mentor.GetMentorStartTime() == 0) {
        m_Mentor.SetMentorStartTime(runtime::GetTickCount());
    }
    mentee->m_Mentee.SetMenteeStartTime(runtime::GetTickCount());

    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1f93, this, mentee, GetMenteeCount(), mentee->m_data->m_MainClassLevel, mentee->m_data->m_nLevel, 0, 0, 0, 0
    );
    GetSocket()->SendSystemMessage_S(SystemMessageId::CHARACTER_BECAME_YOUR_MENTEE, mentee->GetName());
    mentee->GetSocket()->SendSystemMessage_S(SystemMessageId::CHARACTER_BECAME_YOUR_MENTOR, GetName());
    mentee->m_Mentee.SendGiftByMentee(2, 0);
}

void User::ReplyAddFriendResult(User* requestee) {
    // Current L2Server 0x00b747b4, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();
    if (requestee == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User* pRequestee] is nullptr",
        //     L"user_friend.cpp",
        //     0x11f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User* pRequestee] is nullptr",
        //     L"user_friend.cpp",
        //     0x11f)
        L2_LOG_WARN(
            Log, "Cannot complete friend-add notification for character %u: the other user is unavailable.",
            static_cast<unsigned>(GetDBID())
        );
        return;
    }

    FriendInfo requesteeInfo;
    requesteeInfo.nCharDBID = static_cast<std::int32_t>(requestee->GetDBID());
    requesteeInfo.wName = std::u16string(requestee->GetName());
    requesteeInfo.nLevel = static_cast<std::int16_t>(requestee->Level());
    requesteeInfo.nClass = static_cast<std::int16_t>(requestee->GetClass());

    FriendDetailInfo requesteeDetail;
    requesteeDetail.nCharDBID = static_cast<std::int32_t>(requestee->GetDBID());
    requesteeDetail.nPledgeDBID = static_cast<std::int32_t>(requestee->m_data->m_nPledgeID);
    requesteeDetail.nAllianceDBID = requestee->GetAllianceDbId();
    requesteeDetail.cBirthMonth = static_cast<char>(requestee->m_data->m_tCreateDate.wMonth);
    requesteeDetail.cBirthDay = static_cast<char>(requestee->m_data->m_tCreateDate.wDay);
    requesteeDetail.tLastLogoutTime = -1;

    CSPointer<CFriends> requesterFriends = GetFriends();
    if (requesterFriends) {
        requesterFriends->AddFriend(requestee->GetDBID(), requesteeInfo);
        requesterFriends->SetDetail(requestee->GetDBID(), requesteeDetail);
    }

    FriendInfo requesterInfo;
    requesterInfo.nCharDBID = static_cast<std::int32_t>(GetDBID());
    requesterInfo.wName = std::u16string(GetName());
    requesterInfo.nLevel = static_cast<std::int16_t>(Level());
    requesterInfo.nClass = static_cast<std::int16_t>(GetClass());

    FriendDetailInfo requesterDetail;
    requesterDetail.nCharDBID = static_cast<std::int32_t>(GetDBID());
    requesterDetail.nPledgeDBID = static_cast<std::int32_t>(m_data->m_nPledgeID);
    requesterDetail.nAllianceDBID = GetAllianceDbId();
    requesterDetail.cBirthMonth = static_cast<char>(m_data->m_tCreateDate.wMonth);
    requesterDetail.cBirthDay = static_cast<char>(m_data->m_tCreateDate.wDay);
    requesterDetail.tLastLogoutTime = -1;

    CSPointer<CFriends> requesteeFriends = requestee->GetFriends();
    if (requesteeFriends) {
        requesteeFriends->AddFriend(GetDBID(), requesterInfo);
        requesteeFriends->SetDetail(GetDBID(), requesterDetail);
    }

    CUserSocket* const requesterSocket = GetSocket();
    requesterSocket->Send(
        "cddSddddS", 0x55, 1, requestee->GetDBID(), requestee->GetName(), 1, requestee->GetServerId(), requestee->Level(),
        requestee->GetClass(), requesteeInfo.szFriendMemo.c_str()
    );
    requesterSocket->SendSystemMessage_Char(SystemMessageId::FRIEND_ADDED, requestee->GetName());
    requesterSocket->SendSystemMessage(SystemMessageId::FRIEND_INVITATION_SUCCEEDED);

    CUserSocket* const requesteeSocket = requestee->GetSocket();
    if (requesteeSocket != nullptr) {
        requesteeSocket->Send(
            "cddSddddS", 0x55, 1, GetDBID(), GetName(), 1, GetServerId(), Level(), GetClass(),
            requesterInfo.szFriendMemo.c_str()
        );
        requesteeSocket->SendSystemMessage_Char(SystemMessageId::FRIEND_ADDED, GetName());
    }
}

void User::BreakFriendship(int characterId) {
    // Current L2Server 0x00b74d5c, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<CFriends> friends;
    CSPointer<User> otherUser;
    {
        L2_GUARD(*m_lock);
        friends = GetFriends();
        if (!friends) {
            GetSocket()->Send("cd", 0x57, 0);
        } else {
            std::array<char16_t, 25> friendName{};
            friends->GetFriendName(static_cast<unsigned int>(characterId), friendName.data(), 0x18);
            friends->RemoveFriend(static_cast<unsigned int>(characterId));
            friends->RemoveFriendDetail(static_cast<unsigned int>(characterId));
            GetSocket()->Send("cdS", 0x57, 1, friendName.data());
            GetSocket()->SendSystemMessage_Char(SystemMessageId::FRIEND_REMOVED, friendName.data());
        }

        otherUser = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
        if (otherUser) {
            CSPointer<CFriends> otherFriends = otherUser->GetFriends();
            if (otherFriends) {
                std::array<char16_t, 25> friendName{};
                otherFriends->GetFriendName(GetDBID(), friendName.data(), 0x18);
                otherFriends->RemoveFriend(GetDBID());
                otherFriends->RemoveFriendDetail(GetDBID());
                otherUser->GetSocket()->Send(
                    "cdS", 0x57, 1, friendName.data()
                );
                otherUser->GetSocket()->SendSystemMessage_Char(
                    SystemMessageId::FRIEND_REMOVED, friendName.data()
                );
            }
        }
    }
}

void User::ReplyEndMentoring(int otherDatabaseId, int endType) {
    // Current L2Server 0x00b75954, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    int penalty = g_nMentorCancelPanelty != 0 ? g_nMentorCancelPanelty : 0x2a300;
    std::array<char16_t, 25> relatedName{};
    int counterpartDatabaseId = 0;
    int waitingDatabaseId = otherDatabaseId;
    int logOtherDatabaseId = otherDatabaseId;
    bool removed = false;

    if (endType == 2) {
        CopyMentorName(relatedName, m_Mentor.GetMenteeName(otherDatabaseId));
        counterpartDatabaseId = static_cast<std::int32_t>(GetDBID());
        removed = m_Mentor.DeleteMentee(static_cast<std::uint32_t>(otherDatabaseId), true);
    } else {
        if (endType != 3 && endType != 4) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"ReplyEndMentoring type incorrect endType[%d]",
            //     endType)
            // L2_LOG_ERROR(Log,
            //     L"ReplyEndMentoring type incorrect endType[%d]",
            //     endType)
            L2_LOG_ERROR(
                Log, "Cannot end mentoring for character %u: end type %d is unsupported; expected 2, 3 or 4.",
                static_cast<unsigned>(GetDBID()), endType
            );
            return;
        }
        CopyMentorName(relatedName, m_Mentee.m_Mentor.sCharName);
        counterpartDatabaseId = m_Mentee.m_Mentor.nCharDBId;
        waitingDatabaseId = static_cast<std::int32_t>(GetDBID());
        removed = m_Mentee.ResetMentor(true);
        logOtherDatabaseId = counterpartDatabaseId;
    }
    if (!removed) {
        return;
    }

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x1f94, this, nullptr, relatedName.data(), nullptr, nullptr, endType, logOtherDatabaseId, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );

    CSPointer<User> counterpart =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), counterpartDatabaseId));
    if (endType == 4) {
        if (!IsErtheia()) {
            GetSocket()->SendSystemMessage_S(
                SystemMessageId::MENTORING_ENDED_YOU_AWAKENED_AT_LEVEL_85, std::u16string(relatedName.data()).c_str()
            );
            if (counterpart) {
                counterpart->GetSocket()->SendSystemMessage_S(SystemMessageId::MENTORING_ENDED_MENTEE_AWAKENED, GetName());
            }
        } else {
            GetSocket()->SendSystemMessage_S(
                SystemMessageId::MENTORING_ENDED_YOU_COMPLETED_THIRD_LIBERATION_AT_LEVEL_85,
                std::u16string(relatedName.data()).c_str()
            );
            if (counterpart) {
                counterpart->GetSocket()->SendSystemMessage_S(
                    SystemMessageId::MENTORING_ENDED_MENTEE_COMPLETED_THIRD_LIBERATION, GetName()
                );
            }
        }
        penalty = g_nMentorGraduatePanelty != 0 ? g_nMentorGraduatePanelty : 0x15180;
        m_Mentee.SendGiftByMentee(1, 0);
    } else {
        CSPointer<User> waitingUser =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), waitingDatabaseId));
        if (waitingUser) {
            g_MenteeWaitingList.AddWaitingList(waitingUser.get(), true);
        }
        GetSocket()->SendSystemMessage_S(
            SystemMessageId::MENTORING_RELATIONSHIP_ENDED_WITH_MENTOR_REUSE_DELAY, relatedName.data()
        );
        const std::u16string loweredName = LowerMentorName(relatedName.data());
        CSPointer<User> related = GetUserFromNameMap(g_nWorldId, loweredName);
        if (related) {
            related->GetSocket()->SendSystemMessage_S(
                SystemMessageId::MENTORING_RELATIONSHIP_ENDED_WITH_MENTOR_REUSE_DELAY, GetName()
            );
        }
    }

    if (counterpart) {
        counterpart->SetMentorPenaltyEndTime(runtime::AddInt32Wrapping(runtime::crt::Time32Now(), penalty));
    }
}

bool User::CanBeMentor(int& reason) {
    // Current L2Server 0x00b723b0, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    if (IsAlreadyHaveMentor()) {
        reason = 5;
        return false;
    }

    const int classId = static_cast<int>(m_data->m_nClass);
    const bool mentorClass = (classId >= 0x94 && classId <= 0xb5) || (classId >= 0xbc && classId <= 0xbd);
    if (!mentorClass) {
        reason = 3;
        return false;
    }
    if (m_data->m_MainClassLevel < 85) {
        reason = 2;
        return false;
    }

    const int menteeCount = runtime::Int32FromBits(static_cast<std::uint32_t>(m_Mentor.m_mapMentees.size()));
    if (menteeCount >= 3) {
        reason = 1;
        return false;
    }
    if (runtime::SubtractInt32Wrapping(m_Mentor.m_tPenaltyEnd, runtime::crt::Time32Now()) >= 1) {
        reason = 4;
        return false;
    }

    const bool allowed = F2PConfig::CanBeMentor(this);
    if (!allowed) {
        reason = 8;
    }
    return allowed;
}

void User::AddMentee(User* mentee) {
    // Current L2Server 0x00b73298, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();
    if (mentee == nullptr) {
        return;
    }

    int reason = 0;
    if (!CanBeMentor(reason)) {
        constexpr std::array<unsigned int, 8> messages{
            SystemMessageId::MENTOR_MENTEE_LIMIT,
            SystemMessageId::MUST_AWAKEN_TO_BECOME_MENTOR,
            SystemMessageId::MUST_AWAKEN_TO_BECOME_MENTOR,
            SystemMessageId::NEW_MENTEE_REUSE_TIME_REMAINING,
            SystemMessageId::TARGET_CANNOT_FORM_MORE_MENTORING_RELATIONSHIPS,
            0,
            0,
            SystemMessageId::PREMIUM_ACCOUNT_REQUIRED_TO_ADD_MENTEE,
        };
        if (reason == 4) {
            const int remaining = GetMentorPenaltyEndTimeLeft();
            const int days = remaining / 0x15180;
            const int hours = (remaining % 0x15180) / 0xe10;
            const int minutes = (remaining % 0xe10) / 0x3c;
            GetSocket()->Send(
                "chcchcccc", 0x62, SystemMessageId::NEW_MENTEE_REUSE_TIME_REMAINING, 3, 0x15,
                static_cast<int>(runtime::Int16FromBits(static_cast<std::uint16_t>(days))), 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(hours))), 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(minutes)))
            );
        } else {
            GetSocket()->SendSystemMessage(messages[static_cast<std::size_t>(reason - 1)]);
        }
        SendMentorList();
        return;
    }

    if (!mentee->CanBeMentee(reason)) {
        constexpr std::array<unsigned int, 8> messages{
            SystemMessageId::TARGET_ALREADY_HAS_MENTOR,
            SystemMessageId::TARGET_LEVEL_TOO_HIGH_TO_BECOME_MENTEE,
            0,
            0,
            SystemMessageId::TARGET_CANNOT_FORM_MORE_MENTORING_RELATIONSHIPS,
            SystemMessageId::TARGET_LACKS_ITEM_REQUIRED_TO_BECOME_MENTEE,
            SystemMessageId::MENTEE_MUST_BE_ON_MAIN_CLASS,
            SystemMessageId::TARGET_CAN_NO_LONGER_BE_MENTEE,
        };
        const unsigned int message = messages[static_cast<std::size_t>(reason - 1)];
        if (static_cast<int>(message) > 0) {
            const char16_t* const menteeName = mentee->GetName();
            GetSocket()->SendSystemMessage_S(message, menteeName);
        }
        SendMentorList();
        return;
    }

    db.RequestAddMentee(*this, runtime::Int32FromBits(mentee->GetDBID()));
}

void User::AddMentee(int menteeDatabaseId, char16_t* menteeName, int menteeClass, int menteeLevel) {
    // Current L2Server 0x00b75de0, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<User> mentee =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), menteeDatabaseId));
    const bool isOnline = static_cast<bool>(mentee);

    MentorInfo mentorInfo;
    mentorInfo.nCharDBId = menteeDatabaseId;
    CopyTargetMentorName(mentorInfo.sCharName, menteeName);
    mentorInfo.nLevel = static_cast<std::int16_t>(menteeLevel);
    mentorInfo.nClass = static_cast<std::int16_t>(menteeClass);
    mentorInfo.nLogin = isOnline ? MentorState::LOGIN : MentorState::LOGOUT;

    if (!m_Mentor.AddMentee(static_cast<std::uint32_t>(menteeDatabaseId), mentorInfo)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] already have 3 mentees[%d]",
        //     L"user_friend.cpp",
        //     0x552,
        //     menteeDatabaseId)
        // Noise: CMentor::AddMentee already reports the three-mentee limit with the rejected ID. This caller would duplicate
        // the same failure. L2_LOG_ERROR(Log,
        //     L"[%s][%d] already have 3 mentees[%d]",
        //     L"user_friend.cpp",
        //     0x552,
        //     menteeDatabaseId)
    } else if (mentee && mentee->m_data->m_nSubJobId == 0) {
        mentee->SetMentorLoginStatus(static_cast<int>(isOnline));
        if (m_Mentor.GetMentorStartTime() == 0) {
            m_Mentor.SetMentorStartTime(runtime::GetTickCount());
        }
        mentee->m_Mentee.SetMenteeStartTime(runtime::GetTickCount());
    }
}

void User::SetMyMentor(int mentorDatabaseId, char16_t* mentorName, int mentorClass, int mentorLevel) {
    // Current L2Server 0x00b75ff0, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<User> mentor =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), mentorDatabaseId));
    const bool isOnline = static_cast<bool>(mentor);

    MentorInfo mentorInfo;
    mentorInfo.nCharDBId = mentorDatabaseId;
    CopyTargetMentorName(mentorInfo.sCharName, mentorName);
    mentorInfo.nLevel = static_cast<std::int16_t>(mentorLevel);
    mentorInfo.nClass = static_cast<std::int16_t>(mentorClass);
    mentorInfo.nLogin = isOnline ? MentorState::LOGIN : MentorState::LOGOUT;
    std::memcpy(&m_Mentee.m_Mentor, &mentorInfo, sizeof(mentorInfo));

    if (mentor) {
        mentor->SetMenteeLoginStatus(static_cast<int>(GetDBID()), MentorState::LOGIN);
        m_Mentee.SomethingChange();
        m_Mentee.SetMenteeStartTime(runtime::GetTickCount());
    }
    AcquireMentorMenteeSkills(ESAT_MENTEE);
    g_MenteeWaitingList.DeleteWaitingList(this);
}

bool User::OnAddFriendRequest(const char16_t* const targetName, const bool weddingProposal) {
    // Current L2Server 0x00b76190, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CUserSocket* const socket = GetSocket();
    if (CReplyChecker::IsUserWaitingReply(this)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }

    CParty* const party = GetParty();
    if (party != nullptr && party->IsLootingTypeModifyInProgress()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_MAKE_ONE_TO_ONE_REQUEST_DURING_PARTY_LOOT_CHANGE);
        return false;
    }

    const std::u16string loweredName = LowercaseGameName(targetName);
    CSPointer<User> target = GetUserFromNameMap(m_data->m_nWorldId, loweredName);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        socket->Send("cd", 0x55, 0);
        return false;
    }

    if (IsOlympiadPlaying()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_SEND_PARTY_OR_FRIEND_INVITATION_DURING_OLYMPIAD);
        return false;
    }
    if (m_PledgeGameUser.IsPlayer()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_INVITE_FRIEND_OR_PARTY_DURING_CEREMONY_OF_CHAOS);
        return false;
    }
    if (target->IsOlympiadPlaying()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_INVITE_WHILE_REFUSING_INVITATIONS);
        return false;
    }
    if (target->m_PledgeGameUser.IsPlaying()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_INVITE_WHILE_REFUSING_INVITATIONS);
        return false;
    }

    CParty* const targetParty = target->GetParty();
    if (targetParty != nullptr && targetParty->IsLootingTypeModifyInProgress()) {
        const char16_t* const targetName = target->GetName();
        CUserSocket* const currentSocket = GetSocket();
        currentSocket->SendSystemMessage_S(SystemMessageId::CHARACTER_CHANGING_PARTY_LOOT, targetName);
        return false;
    }
    if (target->m_nId == m_nId) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_USE_ON_SELF);
        return false;
    }
    if (target->m_bBlockAll) {
        socket->SendSystemMessage_S(SystemMessageId::TARGET_BLOCKS_ALL, target->GetName());
        return false;
    }
    if (target->IsInBlockList(*this)) {
        socket->SendSystemMessage_S(SystemMessageId::CHARACTER_BLOCKED_YOU, target->GetName());
        return false;
    }
    if (!target->AllowInteractWith(this, ClientINI::IT_FRIEND_REQUEST)) {
        socket->SendSystemMessage(SystemMessageId::TARGET_REFUSES_FRIEND_INVITATIONS);
        return false;
    }
    if (!weddingProposal && !target->m_bFriendAddEnabled) {
        socket->SendSystemMessage(SystemMessageId::TARGET_REFUSES_FRIEND_INVITATIONS);
        return false;
    }
    if (m_data->m_TrainingState == 1) {
        SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    if (target->m_data->m_TrainingState == 1) {
        SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
        return false;
    }

    CSPointer<CFriends> requesterFriends = GetFriends();
    if (!requesterFriends) {
        socket->SendSystemMessage(SystemMessageId::FRIEND_LIST_NOT_READY_FOR_USE);
        return false;
    }
    if (!weddingProposal) {
        if (requesterFriends->HasFriend(target->GetDBID())) {
            socket->SendSystemMessage(SystemMessageId::ALREADY_IN_FRIEND_LIST);
            return false;
        }
        if (requesterFriends->GetFriendCount() > 0x7f) {
            socket->SendSystemMessage(SystemMessageId::FRIEND_LIST_LIMIT_REACHED);
            return false;
        }
    }

    CSPointer<CFriends> targetFriends = target->GetFriends();
    if (!targetFriends) {
        socket->SendSystemMessage(SystemMessageId::FRIEND_LIST_NOT_READY_FOR_USE);
        return false;
    }

    if (!weddingProposal) {
        if (targetFriends->GetFriendCount() > 0x7f) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_ADD_FRIEND_TARGET_FRIEND_LIST_FULL);
            return false;
        }
        if (!CReplyChecker::AttachReplyChecker(5, this, target.get(), 0x3c)) {
            socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
            return false;
        }
        CUserSocket* const targetSocket = target->GetSocket();
        if (targetSocket != nullptr) {
            targetSocket->Send("ccS", 0x83, 1, GetName());
            socket->SendSystemMessage_S(SystemMessageId::FRIEND_REQUEST_SENT_TO_CHARACTER, target->GetName());
        }
        return false;
    }

    if (!CReplyChecker::AttachReplyChecker(0x0e, this, target.get(), 0x3c)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }
    CUserSocket* const targetSocket = target->GetSocket();
    if (targetSocket != nullptr) {
        std::array<char16_t, 1024> message{};
        static_cast<void>(FormatUtf16To(
            // Original: u"%ls님에게 청혼하였습니다. 그녀의 선택을 기다려주세요."
            message.data(), message.size(), u"You proposed to %ls. Please wait for her answer.", target->GetName()
        ));
        targetSocket->Send("ccS", 0x83, 4, GetName());
        socket->SendSystemMessage(u"SYS", message.data());
    }
    return false;
}

bool User::OnAddFriendReply(const int result, const bool weddingProposal) {
    // Current L2Server 0x00b729f4, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CUserSocket* const socket = GetSocket();
    CSPointer<User> requester = CReplyChecker::GetRequesterFromReplier(this);
    if (!requester) {
        socket->SendSystemMessage(
            weddingProposal ? SystemMessageId::TARGET_PLAYER_NOT_ONLINE : SystemMessageId::FRIEND_INVITATION_USER_NOT_FOUND
        );
        return false;
    }

    if (!weddingProposal) {
        if (result == 1) {
            db.RequestEstablishFriendship(requester.get(), this);
        } else if (result == -1) {
            requester->GetSocket()->SendSystemMessage_Char(SystemMessageId::CHARACTER_REFUSES_FRIEND_REQUESTS, GetName());
            GetSocket()->SendSystemMessage_Char(
                SystemMessageId::FRIEND_INVITATION_AUTOMATICALLY_REFUSED_BY_SETTINGS, requester->GetName()
            );
        } else {
            requester->GetSocket()->SendSystemMessage_Char(SystemMessageId::CHARACTER_REFUSED_FRIEND_INVITATION, GetName());
            GetSocket()->SendSystemMessage_Char(SystemMessageId::REFUSED_CHARACTER_FRIEND_INVITATION, requester->GetName());
        }
    } else if (result == 1) {
        db.RequestUpdateWedding(*this, static_cast<int>(requester->GetDBID()), 1);
    } else {
        // Original: u"청혼이 이루어지지 않았습니다."
        requester->GetSocket()->SendSystemMessage(u"SYS", u"The marriage proposal was not accepted.");
        // Original: u"청혼이 이루어지지 않았습니다."
        GetSocket()->SendSystemMessage(u"SYS", u"The marriage proposal was not accepted.");
    }

    CReplyChecker::FinishWaitingReplyByReplier(this);
    return false;
}

void User::UpdateMentorByLevelUp(bool graduate, int originalLevel) {
    // Current L2Server 0x00b72ea0.
    L2_GUARD_FUNCTION();
    if (IsAlreadyHaveMentor()) {
        m_Mentee.SendByLevelUp(graduate, originalLevel);
    }
    UpdateMentorMentee();
}

void User::OnRequestUpdateFriendMemo(const char16_t* const characterName, const char16_t* const memo) {
    // Current L2Server 0x00b739c0, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<CFriends> friends = GetFriends();
    if (friends && friends->HasFriend(characterName)) {
        const unsigned int characterId = friends->GetFriendDbId(characterName);
        if (characterId != 0) {
            db.RequestUpdateFriendMemo(this, static_cast<int>(characterId), memo);
        }
    }
}

int User::GetBlockCharList(CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x00b74188, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    const int count = [&] {
        L2_READ_GUARD(m_lockBlockListMap);
        for (const auto& entry : m_BlockListMap) {
            packet.Assemble("dS", entry.first, entry.second.szCharName.c_str());
        }
        return packet.GetAssembledCount();
    }();
    return count;
}

void User::BuildBlockListPacket(CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x00b742d4, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockBlockListMap);
        for (const auto& entry : m_BlockListMap) {
            if (packet.Assemble("S", entry.second.szCharName.c_str()) == 0) {
                const int count = packet.GetAssembledCount();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] My BlockList is overflow, char id[%d] count[%d]",
                //     L"user_friend.cpp",
                //     0x3be,
                //     entry.first,
                //     count)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] My BlockList is overflow, char id[%d] count[%d]",
                //     L"user_friend.cpp",
                //     0x3be,
                //     entry.first,
                //     count)
                L2_LOG_ERROR(
                    Log,
                    "Block-list encoding stopped before character %u after %d entries; only the encoded prefix will be "
                    "sent.",
                    static_cast<unsigned>(entry.first), count
                );
                break;
            }
        }
    }
}

void User::SendBlockList() {
    // Current L2Server 0x00b744dc, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> packet;
    BuildBlockListPacket(packet);
    GetSocket()->Send("cdb", 0xd5, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
}

std::u16string User::GetBlockCharInfo(const int characterId, const bool getCharacterName) {
    // Current L2Server 0x00b751b8, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    std::optional<std::u16string> resultStorage;
    {
        L2_READ_GUARD(m_lockBlockListMap);
        const auto position = m_BlockListMap.find(characterId);
        auto& result = resultStorage.emplace();
        if (position != m_BlockListMap.end()) {
            result = getCharacterName ? position->second.szCharName : position->second.szBlockMemo;
        }
    }
    return std::move(*resultStorage);
}

int User::GetBlockCharDBID(const char16_t* const characterName) {
    // Current L2Server 0x00b75728, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    const int characterId = [&] {
        L2_READ_GUARD(m_lockBlockListMap);
        const auto position = [&]() {
            const std::u16string name = ToBlockListName(characterName);
            return m_BlockListMapRev.find(name);
        }();
        return position == m_BlockListMapRev.end() ? 0 : position->second;
    }();
    return characterId;
}

void User::UpdateBlockMemo(int characterId, const char16_t* memo) {
    // Current L2Server 0x00b75378, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockBlockListMap);
        const auto position = m_BlockListMap.find(characterId);
        if (position != m_BlockListMap.end()) {
            BlockListInfo& blockInfo = position->second;
            blockInfo.szBlockMemo.assign(std::u16string_view(memo));
            GetSocket()->Send("chSS", 0xfe, 0xef, blockInfo.szCharName.c_str(), blockInfo.szBlockMemo.c_str());
        }
    }
}

void User::OnRequestUpdateBlockMemo(const char16_t* const characterName, const char16_t* const memo) {
    // Current L2Server 0x00b75874, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    if (IsInBlockList(ToBlockListName(characterName).c_str())) {
        const int characterId = GetBlockCharDBID(characterName);
        if (characterId != 0) {
            db.RequestUpdateBlockMemo(this, characterId, memo);
        }
    }
}

void User::OnRequestBlock(const int requestType, const unsigned char* const packet) {
    // Current L2Server 0x00b76fe4, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 24> characterName;
    characterName[0] = 0;
    std::memset(characterName.data() + 1, 0, 0x2e);

    bool flag = false;
    if (requestType == 1) {
        ::Disassemble(packet, "S", 0x30, characterName.data());
        if (IsInBlockList(std::u16string(characterName.begin(), characterName.end()).c_str())) {
            db.RequestDeleteBlockList(this, std::u16string(characterName.begin(), characterName.end()).c_str());
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"remove block list cannot find char. char name [%d][%s]",
            //     static_cast<int>(GetDBID()),
            //     hostCharacterName.c_str())
            // Noise: Deleting a name that is already absent from the block list is a routine stale request. No database
            // deletion is issued. L2_LOG_ERROR(Log,
            //     L"remove block list cannot find char. char name [%d][%s]",
            //     static_cast<int>(GetDBID()),
            //     hostCharacterName.c_str())
        }
        return;
    }
    if (requestType == 2) {
        SendBlockList();
        return;
    }
    if (requestType == 3) {
        m_bBlockAll = true;
        SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        SendETCBuffStatusInfo();
        return;
    }
    if (requestType == 4) {
        m_bBlockAll = false;
        SendSystemMessage(SystemMessageId::BLOCK_ALL_DISABLED);
        SendETCBuffStatusInfo();
        return;
    }
    if (requestType == 100) {
        flag = true;
    } else if (requestType != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user[%d] request blockpacket but unknown type [%d]",
        //     static_cast<int>(GetDBID()),
        //     requestType)
        // L2_LOG_ERROR(Log,
        //     L"user[%d] request blockpacket but unknown type [%d]",
        //     static_cast<int>(GetDBID()),
        //     requestType)
        L2_LOG_WARN(
            Log, "Character %u sent unsupported block-list request type %d; the request is ignored.",
            static_cast<unsigned>(GetDBID()), requestType
        );
        return;
    }

    if (static_cast<int>(m_BlockListMap.size()) >= 0x200) {
        SendSystemMessage_d(SystemMessageId::BLOCK_LIST_LIMIT_REACHED, 0x200);
        return;
    }

    ::Disassemble(packet, "S", 0x30, characterName.data());
    const std::u16string_view hostCharacterNameUtf16 = FixedUtf16View(characterName);
    CSPointer<User> localTarget = GetUserFromNameMap(m_data->m_nWorldId, SanitizeUtf16(hostCharacterNameUtf16));
    if (localTarget && localTarget->m_data->m_nBuilder >= 1 && localTarget->m_data->m_nBuilder <= 5) {
        SendSystemMessage(SystemMessageId::CANNOT_BLOCK_GM);
        return;
    }

    if (IsInFifthClassGroup(static_cast<ClassType>(GetClass()))) {
        SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return;
    }

    CSPointer<User> worldTarget = GetUserFromNameMap(g_nWorldId, SanitizeUtf16(hostCharacterNameUtf16));
    if (worldTarget && IsInFifthClassGroup(static_cast<ClassType>(worldTarget->GetClass()))) {
        GetSocket()->SendSystemMessage_Char(
            SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, worldTarget->GetName()
        );
        return;
    }

    if (CompareBlockNameCaseInsensitive(reinterpret_cast<const char16_t*>(characterName.data()), GetName()) == 0) {
        SendSystemMessage(SystemMessageId::CANNOT_BLOCK_SELF);
    } else if (!IsInBlockList(std::u16string(characterName.begin(), characterName.end()).c_str())) {
        db.RequestAddBlockList(this, std::u16string(characterName.begin(), characterName.end()).c_str(), flag);
    }
}

void User::AddWedding(const char16_t* const targetName) {
    // Current L2Server 0x00b7745c, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    const std::u16string lower = LowercaseGameName(targetName);
    std::array<char16_t, 25> loweredName{};
    std::copy_n(lower.data(), lower.size() + 1, loweredName.data());
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, std::u16string(loweredName.data()));
    if (!target) {
        GetSocket()->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        return;
    }
    if (target->m_nId == m_nId) {
        GetSocket()->SendSystemMessage(SystemMessageId::INVALID_TARGET);
        return;
    }

    CSPointer<CFriends> friends = GetFriends();
    if (!friends) {
        GetSocket()->SendSystemMessage(SystemMessageId::FRIEND_LIST_NOT_READY_FOR_USE);
        return;
    }

    auto sendWeddingText = [this](const char16_t* const message) { GetSocket()->SendSystemMessage(u"SYS", message); };

    if (m_nSpouseDbID >= 1 || m_data->m_Sex != ST_MALE) {
        // Original: u"결혼을 하지 않은 남자 캐릭터만 청혼할 수 있습니다."
        sendWeddingText(u"Only an unmarried male character can propose.");
        return;
    }
    if (target->m_nSpouseDbID >= 1 || target->m_data->m_Sex != ST_FEMALE) {
        // Original: u"결혼을 하지 않은 여자 캐릭터에게만 청혼할 수 있습니다."
        sendWeddingText(u"You can propose only to an unmarried female character.");
        return;
    }
    if (m_data->m_nLevel < 85 || target->m_data->m_nLevel < 85) {
        // Original: u"신랑 신부의 레벨이 모두 85 이상이어야 합니다."
        sendWeddingText(u"The groom and the bride must both be level 85 or higher.");
        return;
    }
    if (!friends->HasFriend(target->GetDBID())) {
        // Original: u"친구에게만 청혼할 수 있습니다."
        sendWeddingText(u"You can propose only to a friend.");
        return;
    }

    {
        L2_READ_GUARD(m_inv);
        if (!m_inv.HaveItemByClassId(0x1908, 1, false, false)) {
            // Original: u"신랑 인벤토리에 예복(6408)이 없습니다."
            sendWeddingText(u"The groom has no formal wear (6408) in the inventory.");
            return;
        }
        if (!target->m_inv.HaveItemByClassId(0x1908, 1, false, false)) {
            // Original: u"신부 인벤토리에 예복(6408)이 없습니다."
            sendWeddingText(u"The bride has no formal wear (6408) in the inventory.");
            return;
        }
        if (m_inv.HaveItemByClassId(0x56dd, 1, false, false)) {
            // Original: u"신랑이 이미 웨딩반지(22237)를 가지고 있습니다."
            sendWeddingText(u"The groom already has a wedding ring (22237).");
            return;
        }
        if (target->m_inv.HaveItemByClassId(0x56dd, 1, false, false)) {
            // Original: u"신부가 이미 웨딩반지(22237)를 가지고 있습니다."
            sendWeddingText(u"The bride already has a wedding ring (22237).");
            return;
        }
    }
    OnAddFriendRequest(target->GetName(), true);
}

void User::AddBlockList(const int characterId, const char16_t* const characterName, const char16_t* const memo) {
    // Current L2Server 0x00b78248, original owner user_friend.cpp. Both
    // maps use insert-without-overwrite. Duplicate keys keep their first
    // mapped value exactly as in the current MSVC tree helpers.
    L2_GUARD_FUNCTION();

    std::optional<std::u16string> targetNameStorage;
    std::optional<std::u16string> targetMemoStorage;
    std::optional<BlockListInfo> blockInfoStorage;
    {
        L2_WRITE_GUARD(m_lockBlockListMap);
        const auto& targetName = targetNameStorage.emplace(ToBlockListName(characterName));
        const auto& targetMemo = targetMemoStorage.emplace(ToBlockListName(memo));
        const auto& blockInfo = blockInfoStorage.emplace(BlockListInfo{targetName, targetMemo});
        m_BlockListMap.emplace(characterId, blockInfo);
        m_BlockListMapRev.emplace(targetName, characterId);
    }
    SendETCBuffStatusInfo();
}

void User::DeleteBlockList(const int characterId) {
    // Current L2Server 0x00b78408, original owner user_friend.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockBlockListMap);
        const auto position = m_BlockListMap.find(characterId);
        if (position == m_BlockListMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"cannot delete block list. user[%d] target[%d]",
            //     static_cast<int>(GetDBID()),
            //     characterId)
            // Noise: A block-list deletion can be repeated after the entry is gone. The existing status refresh still runs.
            // L2_LOG_ERROR(Log,
            //     L"cannot delete block list. user[%d] target[%d]",
            //     static_cast<int>(GetDBID()),
            //     characterId)
        } else {
            {
                const std::u16string characterName = position->second.szCharName;
                m_BlockListMapRev.erase(characterName);
            }
            m_BlockListMap.erase(position);
        }
    }
    SendETCBuffStatusInfo();
}

bool User::IsInBlockList(int characterId) {
    L2_GUARD_FUNCTION();

    const bool blocked = [&] {
        L2_READ_GUARD(m_lockBlockListMap);
        return m_BlockListMap.find(characterId) != m_BlockListMap.end();
    }();
    return blocked;
}

bool User::IsInBlockList(const char16_t* characterName) {
    L2_GUARD_FUNCTION();

    std::optional<std::u16string> nameStorage;
    const bool blocked = [&] {
        L2_READ_GUARD(m_lockBlockListMap);
        const auto& name = nameStorage.emplace(characterName != nullptr ? characterName : u"");
        return m_BlockListMapRev.find(name) != m_BlockListMapRev.end();
    }();
    return blocked;
}

bool User::IsInBlockList(User& user) {
    L2_GUARD_FUNCTION();

    if (m_data->m_nWorldId != user.m_data->m_nWorldId) {
        return false;
    }
    return IsInBlockList(static_cast<int>(user.GetDBID()));
}
