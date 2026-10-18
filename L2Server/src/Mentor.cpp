#include "Mentor.h"

#include "CConfig.h"
#include "DB.h"
#include "MentorSetting.h"
#include "ParseHtml.h"
#include "ParserExpData.h"
#include "Pet.h"
#include "Portable/ProcessRuntime.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>

namespace {

bool EqualMentorName(const char16_t* storedName, const wchar_t* requestedName) {
    if (storedName == nullptr || requestedName == nullptr) {
        return false;
    }

#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return ::_wcsicmp(reinterpret_cast<const wchar_t*>(storedName), requestedName) == 0;
#else
    const std::u16string requested = ::WideToUtf16(std::wstring_view(requestedName));
    const char16_t* left = storedName;
    const char16_t* right = requested.c_str();
    for (;;) {
        const auto foldedLeft = static_cast<char16_t>(std::towlower(static_cast<wint_t>(*left++)));
        const auto foldedRight = static_cast<char16_t>(std::towlower(static_cast<wint_t>(*right++)));
        if (foldedLeft != foldedRight) {
            return false;
        }
        if (foldedLeft == u'\0') {
            return true;
        }
    }
#endif
}

const wchar_t* MentorNameForHost(const char16_t* name, std::size_t capacity) {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return reinterpret_cast<const wchar_t*>(name);
#else
    thread_local std::wstring storage;
    std::size_t length = 0;
    while (length < capacity && name[length] != u'\0') {
        ++length;
    }
    storage = ::Utf16ToWide(std::u16string_view(name, length));
    return storage.c_str();
#endif
}

wchar_t* MutableMentorNameForHost(char16_t* name, std::size_t capacity) {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return reinterpret_cast<wchar_t*>(name);
#else
    return const_cast<wchar_t*>(MentorNameForHost(name, capacity));
#endif
}

User* ResolveUser(std::uint32_t smartId) {
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
    if (!creature || !creature->IsPC()) {
        return nullptr;
    }
    return creature->CastUser();
}


} // namespace

CMenteeWaitingList g_MenteeWaitingList;

bool CMenteeWaitingListFilter::IsBetweenLevel(User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    const int level = user->m_data->m_MainClassLevel;
    return m_nLevelMin <= level && level <= m_nLevelMax;
}

bool CMenteeWaitingListFilter::IsInPage(int index) {
    L2_GUARD_FUNCTION();
    const std::uint32_t pageEndBits = static_cast<std::uint32_t>(m_nPage) * 64u;
    const int pageEnd = runtime::Int32FromBits(pageEndBits);
    const int pageBegin = runtime::Int32FromBits(pageEndBits - 63u);
    return pageBegin <= index && index <= pageEnd;
}

bool CMenteeWaitingList::IsInWaitingList(User* user) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (!user) {
        return false;
    }

    const int userSId = static_cast<int>(user->GetServerId());
    for (const int waitingSId : m_waitingSidList) {
        if (waitingSId == userSId) {
            return true;
        }
    }
    return false;
}

bool CMenteeWaitingList::CanAddWaitingList(User& user, bool) {
    L2_GUARD_FUNCTION();
    return !user.IsHaveMentee() && !user.IsAlreadyHaveMentor() && user.m_data->m_MainClassLevel < 85 &&
        // Current L2Server 0x00834681 reads CSharedCreatureData+0x58,
        // m_nSubJobId. The prior recovery used the distinct alive lane at
        // +0x48 and so rejected every ordinary living user.
        user.m_data->m_nSubJobId == 0 && !IsInWaitingList(&user);
}

bool CMenteeWaitingList::AddWaitingList(User* user, bool unknown) {
    L2_GUARD_FUNCTION();
    if (!user || !CanAddWaitingList(*user, unknown)) {
        return false;
    }
    const bool isLiveTest = ServiceVersion::IsLiveTestVer();
    if (isLiveTest && (user->m_data->m_bGMInvisible || user->IsBuilder())) {
        return false;
    }

    L2_GUARD(m_lock);
    m_waitingSidList.push_back(static_cast<int>(user->GetServerId()));
    return true;
}

void CMenteeWaitingList::DeleteWaitingList(User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }

    L2_GUARD(m_lock);
    const int userSId = static_cast<int>(user->GetServerId());
    for (auto position = m_waitingSidList.begin(); position != m_waitingSidList.end(); ++position) {
        if (*position == userSId) {
            m_waitingSidList.erase(position);
            break;
        }
    }
}

void CMenteeWaitingList::MakeWaitingListForBuilder() {
    // Current L2Server 0x00836c24, original owner Mentor.cpp.
    L2_GUARD_FUNCTION();
    if (ServiceVersion::IsLiveTestVer()) {
        return;
    }

    L2_GUARD(m_lock);
    for (int id = 10000; id < 10200; ++id) {
        std::array<char16_t, 40> characterName{};
        std::array<char16_t, 16> accountName{};
        FormatUtf16To(characterName.data(), characterName.size(), u"na%d", id);
        FormatUtf16To(accountName.data(), accountName.size(), u"ac%d", id);

        const int level = static_cast<int>(genrand64_real2() * 85.0 + 1.0);
        const std::int64_t experience = ExpByLevel[level];
        _SYSTEMTIME systemTime = runtime::crt::LocalSystemTimeNow<_SYSTEMTIME>();
        std::array<char16_t, 24> nickname{};
        std::u16string_view(u"TestNick").copy(nickname.data(), 9);
        std::array<unsigned char, 32> opaqueCharacterData{};
        CVitalData vitalityData{};

        User* const user = new User(
            characterName.data(), accountName.data(), static_cast<unsigned int>(id), static_cast<unsigned int>(id), 0, 0, 1,
            1, 0, g_nWorldId, 0, 0, 0, 1.0, 1.0, 1, experience, level, 0, 0, 0, 0, 1, 1, 1, nickname.data(),
            opaqueCharacterData.data(), 0, 0, 0, 0, 0, -1, -1, -1, -1, 100, 0, 0, 0, 0, false, 0, 0x00ffffff,
            DEFAULT_NICKNAME_COLOR, 0, 0, 0, &systemTime, 0, 0, vitalityData, 0, 0, 0, 0x7080, 0, 0, 0, 0, &systemTime, 0, 0,
            0, 0, 0
        );
        user->OnCreate();
        user->m_data->m_MainClassLevel = level;
        m_waitingSidList.push_back(static_cast<int>(user->m_nId));
    }
}

void CMenteeWaitingList::SendWaitingList(CMenteeWaitingListFilter& filter) {
    L2_GUARD_FUNCTION();

    std::array<char, 4096> packet{};
    int packetLength = 0;
    int matchingCount = 0;
    int pageCount = 0;
    L2_GUARD(m_lock);
    for (auto position = m_waitingSidList.end(); position != m_waitingSidList.begin();) {
        --position;
        User* const user = ResolveUser(static_cast<std::uint32_t>(*position));
        if (!user || !filter.IsBetweenLevel(user)) {
            continue;
        }

        ++matchingCount;
        if (!filter.IsInPage(matchingCount)) {
            continue;
        }

        packetLength += ::Assemble(
            packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "Sdd", user->GetName(),
            static_cast<int>(user->m_data->m_nClass), user->m_data->m_MainClassLevel
        );
        ++pageCount;
    }

    filter.GetUser()->GetSocket()->Send(
        "chdddb", 0xfe, 0x11d, filter.GetPage(), matchingCount, pageCount, packetLength, packet.data()
    );
}

wchar_t* CMentee::GetMentorName() {
    // Current target accessor is header-inline and returns the stored
    // MentorInfo UTF-16 buffer directly on Win64.
    return MutableMentorNameForHost(m_Mentor.sCharName, std::size(m_Mentor.sCharName));
}

void CMentee::Init(std::uint32_t userSId) {
    L2_GUARD_FUNCTION();

    m_nMenteeSId = userSId;
    m_Mentor = MentorInfo();
    m_nSomethingChanged = 0;
    m_nInfectedSkillByMentor = 0;
    m_MenteeStartTime = 0;
    m_MentorLogoutTime = 0;
}

void CMentee::ActivateMenteeBuff() {
    // Current L2Server 0x00837298.
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr || mentee->IsOlympiadPlaying() ||
        runtime::raw_atomic::CompareExchangeI32(&m_nInfectedSkillByMentor, 1, 0) != 0) {
        return;
    }

    for (const auto& entry : CMentorSetting::inst().m_MenteeSkills) {
        const CSkillKey key(entry.first, entry.second.m_PrimaryKey, entry.second.m_SecondaryKey);
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, nullptr);
        if (skill != nullptr) {
            skill->ActivateSkill(mentee, mentee, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        }
    }
}

void CMentee::ActivateMenteeBuffWithoutMentor() {
    // Current L2Server 0x00837438.
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr || mentee->IsOlympiadPlaying() ||
        runtime::raw_atomic::CompareExchangeI32(&m_nInfectedSkillByMentor, 1, 0) != 0) {
        return;
    }

    for (const auto& entry : CMentorSetting::inst().m_MenteeSkills_withoutMentor) {
        const CSkillKey key(entry.first, entry.second.m_PrimaryKey, entry.second.m_SecondaryKey);
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, nullptr);
        if (skill != nullptr) {
            skill->ActivateSkill(mentee, mentee, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        }
    }
}

void CMentee::DeleteMenteeBuff() {
    // Current L2Server 0x008375d8, original owner Mentor.cpp.
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr || runtime::raw_atomic::CompareExchangeI32(&m_nInfectedSkillByMentor, 0, 1) != 1) {
        return;
    }

    std::vector<int> deleteIndices;
    {
        L2_GUARD(mentee->m_InfectedSkillsSect);
        for (std::size_t index = 0; index < mentee->m_InfectedSkills.size(); ++index) {
            const InfectedSkillType& infected = runtime::CheckedVectorAt(mentee->m_InfectedSkills, index);
            if (CMentorSetting::inst().IsMenteeBuff(infected.pSkillInfo->m_nSkillId)) {
                deleteIndices.push_back(static_cast<int>(index));
            }
        }
        for (std::size_t reverseIndex = deleteIndices.size(); reverseIndex > 0; --reverseIndex) {
            const int index = runtime::CheckedVectorAt(deleteIndices, reverseIndex - 1);
            const CSkillInfo* const skillInfo =
                runtime::CheckedVectorAt(mentee->m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
            mentee->_DeleteAbnormalStatus(index, mentee, skillInfo, true, SPET_DISPEL, true);
        }
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        mentee, summon, deleteIndices.clear(); L2_GUARD(summon->m_InfectedSkillsSect);
        for (std::size_t index = 0; index < summon->m_InfectedSkills.size(); ++index) {
            const InfectedSkillType& infected = runtime::CheckedVectorAt(summon->m_InfectedSkills, index);
            if (CMentorSetting::inst().IsMenteeBuff(infected.pSkillInfo->m_nSkillId)) {
                deleteIndices.push_back(static_cast<int>(index));
            }
        } for (std::size_t reverseIndex = deleteIndices.size(); reverseIndex > 0; --reverseIndex) {
            const int index = runtime::CheckedVectorAt(deleteIndices, reverseIndex - 1);
            const CSkillInfo* const skillInfo =
                runtime::CheckedVectorAt(summon->m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
            summon->_DeleteAbnormalStatus(index, summon, skillInfo, true, SPET_DISPEL, true);
        }
    );
}

void CMentee::OnTimerExpired() {
    // Current L2Server 0x00837a94.
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr || !mentee->m_data->m_bIsActive || mentee->m_data->m_bIsAlive == 0) {
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(&m_nSomethingChanged, 0) > 0) {
        mentee->SendMentorList();
        DeleteMenteeBuff();
    }

    if (m_MentorLogoutTime != 0) {
        const std::uint32_t elapsed = runtime::GetTickCount() - GetMentorLogoutTime();
        if (elapsed > 300000U) {
            m_MentorLogoutTime = 0;
            runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
            return;
        }
    }

    if (!CanBeBuffed()) {
        DeleteMenteeBuff();
        return;
    }

    if (!IsMentorLogIn()) {
        const std::uint32_t elapsed = runtime::GetTickCount() - GetMentorLogoutTime();
        if (elapsed >= 300000U) {
            ActivateMenteeBuffWithoutMentor();
            return;
        }
    }
    ActivateMenteeBuff();
}

bool CMentee::ResetMentor(bool notify) {
    // Current L2Server 0x008383bc, original owner Mentor.cpp.
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d], Cannot find Mentee, sid[%d]",
        //     L"Mentor.cpp",
        //     0x261,
        //     m_nMenteeSId)
        // L2_LOG_ERROR(Log,
        //     L"FILE[%s] LINE[%d], Cannot find Mentee, sid[%d]",
        //     L"Mentor.cpp",
        //     0x261,
        //     m_nMenteeSId)
        L2_LOG_WARN(
            Log, "Cannot reset mentor state: mentee object %u is unavailable; the reset is not performed.",
            static_cast<unsigned>(m_nMenteeSId)
        );
        return false;
    }

    if (notify) {
        CSPointer<User> mentor =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), m_Mentor.nCharDBId));
        if (mentor) {
            mentor->DeleteMentee(static_cast<int>(mentee->GetDBID()));
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot find Mentor[%d]",
            //     L"Mentor.cpp",
            //     0x26f,
            //     m_Mentor.nCharDBId)
            // Noise: The mentor may be offline when the mentee resets the relationship. Local cleanup continues without an
            // online notification. L2_LOG_ERROR(Log,
            //     L"[%s][%d] Cannot find Mentor[%d]",
            //     L"Mentor.cpp",
            //     0x26f,
            //     m_Mentor.nCharDBId)
            // Noise: An offline mentor during relationship reset does not require a stack dump.
            // runtime::process::ReportCallStack(2, false)
        }
    }

    {
        L2_GUARD(m_lockMentee);
        m_Mentor.nCharDBId = 0;
        std::memset(m_Mentor.sCharName, 0, sizeof(m_Mentor.sCharName));
        m_Mentor.nLevel = 0;
        m_Mentor.nClass = 0;
        m_Mentor.nLogin = MentorState::LOGOUT;
    }
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
    mentee->DeleteMentorMenteeSkills(ESAT_MENTEE);
    DeleteMenteeBuff();
    mentee->SendMentorList();
    return true;
}

void CMentee::SetMentorLoginStatus(MentorState::LoginStatus loginStatus) {
    L2_GUARD_FUNCTION();

    runtime::raw_atomic::ExchangeI32(
        reinterpret_cast<volatile std::int32_t*>(&m_Mentor.nLogin), static_cast<std::int32_t>(loginStatus)
    );
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
}

void CMentee::SetLoginStateToMentor(MentorState::LoginStatus loginStatus) {
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (!mentee) {
        return;
    }

    L2_GUARD(m_lockMentee);
    if (m_Mentor.nLogin == MentorState::LOGOUT) {
        return;
    }

    CSPointer<User> mentor =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentWorldId(), m_Mentor.nCharDBId));
    if (!mentor) {
        return;
    }

    mentor->SetMenteeLoginStatus(static_cast<int>(mentee->GetDBID()), static_cast<int>(loginStatus));
    if (mentor->m_Mentor.GetPlayingMenteeCount() < 1) {
        const std::uint32_t mentorStartTime = mentor->m_Mentor.GetMentorStartTime();
        const std::uint32_t now = runtime::GetTickCount();
        mentor->UpdateMentorPlaytime((now - mentorStartTime) / 1000);
        mentor->m_Mentor.SetMentorStartTime(0);
    }

    const std::uint32_t menteeStartTime = GetMenteeStartTime();
    const std::uint32_t now = runtime::GetTickCount();
    mentee->UpdateMenteePlaytime((now - menteeStartTime) / 1000);
    SetMenteeStartTime(0);
}

void CMentee::ShowMentorInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        return;
    }

    char16_t systemContext[] = u"SYS";
    socket->SendSystemMessage(systemContext, u"---------Mentor info----------");

    std::array<char16_t, 256> message{};
    FormatUtf16To(
        message.data(), message.size(), u"[%d]charName[%s] level[%d] class[%d] login[%d]", m_Mentor.nCharDBId,
        MentorNameForHost(m_Mentor.sCharName, std::size(m_Mentor.sCharName)), static_cast<int>(m_Mentor.nLevel),
        static_cast<int>(m_Mentor.nClass), static_cast<int>(m_Mentor.nLogin)
    );
    socket->SendSystemMessage(systemContext, message.data());
}

void CMentee::UpdateMentorStatus(User& user) {
    L2_GUARD_FUNCTION();

    m_Mentor.nLevel = static_cast<std::int16_t>(user.m_data->m_nLevel);
    m_Mentor.nClass = static_cast<std::int16_t>(static_cast<std::int32_t>(user.m_data->m_nClass));
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
}

void CMentee::SendByLevelUp(bool graduate, int originalLevel) {
    // Current L2Server 0x00839004.
    L2_GUARD_FUNCTION();
    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr || mentee->m_data->m_nSubJobId != 0) {
        return;
    }
    if (!graduate && mentee->m_data->m_nLevel < 86) {
        SendGiftByMentee(0, originalLevel);
        return;
    }
    mentee->EndMentoring(0, 4);
}

void CMentee::SendGiftByMentee(const int giftType, const int originalLevel) {
    // Current L2Server 0x00838600.
    L2_GUARD_FUNCTION();
    User* const mentee = ResolveUser(m_nMenteeSId);
    if (mentee == nullptr) {
        return;
    }

    CMentorSetting& setting = CMentorSetting::inst();
    CFHTML::StrStrMapType htmlParameters;
    std::u16string title;
    std::u16string contents;
    int rewardItemId = 0;
    int rewardItemCount = 0;
    int mailOwnerId = static_cast<int>(mentee->GetDBID());
    int mailReferenceId = 0;

    if (giftType == 0) {
        const int currentLevel = mentee->m_data->m_nLevel;
        const int levelCount = currentLevel - originalLevel;
        title = CFHTML::MakeHTMLFromStrMap(setting.m_sLevUpTitle, htmlParameters, 0);

        CParseHTML parser;
        parser.GetBodyFromFile(setting.m_sLevUpContents, static_cast<int>(mentee->m_data->m_CountryCode));
        parser.AddStringMap(u"whomymentee", std::u16string(mentee->GetName()));
        const int visibleLevel = mentee->Level();
        if (levelCount < 2) {
            parser.AddFormattedStringMap(u"mymenteelevel", u"%d", visibleLevel);
        } else {
            parser.AddFormattedStringMap(u"mymenteelevel", u"%d~%d", (1 - levelCount) + mentee->Level(), visibleLevel);
        }
        contents = parser.MakeHTMLFromStrTemplet(0, parser.m_mapParam);

        const bool academy = mentee->m_data->m_nPledgeType == -1;
        for (int offset = 0; offset < levelCount; ++offset) {
            const int level = currentLevel - offset;
            const int item = setting.GetLevUpRewardItem(level, academy);
            if (item > 0) {
                rewardItemId = item;
            }
            rewardItemCount += setting.GetLevUpRewardItemCount(level, academy);
        }
        mailOwnerId = m_Mentor.nCharDBId;
        mailReferenceId = static_cast<int>(mentee->GetDBID());
    } else if (giftType == 1) {
        title = CFHTML::MakeHTMLFromStrMap(setting.m_sGraduateTitle, htmlParameters, 0);
        contents = CFHTML::MakeHTMLFromStrMap(setting.m_sGraduateContents, htmlParameters, 0);
        rewardItemId = setting.m_nGraduateRewardItem;
        rewardItemCount = static_cast<int>(setting.m_nGraduateRewardItemCount);
    } else if (giftType == 2) {
        title = CFHTML::MakeHTMLFromStrMap(setting.m_sJoinTitle, htmlParameters, 0);
        contents = CFHTML::MakeHTMLFromStrMap(setting.m_sJoinContents, htmlParameters, 0);
        rewardItemId = setting.m_nJoinRewardItem;
        rewardItemCount = static_cast<int>(setting.m_nJoinRewardItemCount);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Gift Type[%d]",
        //     L"Mentor.cpp",
        //     0x32e,
        //     giftType)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Gift Type[%d]",
        //     L"Mentor.cpp",
        //     0x32e,
        //     giftType)
        L2_LOG_ERROR(Log, "Cannot send the mentee gift: type %d is unsupported; expected 0, 1 or 2.", giftType);
        return;
    }

    if (rewardItemId <= 0 || rewardItemCount <= 0) {
        return;
    }
    db.RequestWritePostBirth(
        mailOwnerId, title.c_str(), contents.c_str(), rewardItemId, 6, rewardItemCount, mailReferenceId, 0
    );

    CLogFormatter formatter{};
    formatter.WriteLog4(
        0x1f95, mentee, nullptr, WideToUtf16(MentorNameForHost(m_Mentor.sCharName, std::size(m_Mentor.sCharName))), nullptr,
        nullptr, m_Mentor.nCharDBId, rewardItemId, rewardItemCount, 0, 0, 0, 0, 0, 0, 0, 0
    );
}

void CMentee::UpdateMenteeStatus() {
    L2_GUARD_FUNCTION();

    User* const mentee = ResolveUser(m_nMenteeSId);
    if (!mentee) {
        return;
    }

    L2_GUARD(m_lockMentee);
    CSPointer<User> mentor =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentWorldId(), m_Mentor.nCharDBId));
    if (mentor) {
        mentor->m_Mentor.UpdateMenteeStatus(*mentee);
    }
}

void CMentee::SubjobChange() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentee);

    CSPointer<User> mentor =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentWorldId(), m_Mentor.nCharDBId));
    if (mentor) {
        mentor->m_Mentor.SomethingChange();
    }
    SomethingChange();
}

void CMentee::MakeMentorList(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentee);

    packet.Assemble(
        "dSddd", m_Mentor.nCharDBId, m_Mentor.sCharName, static_cast<int>(m_Mentor.nClass),
        static_cast<int>(m_Mentor.nLevel), static_cast<int>(m_Mentor.nLogin)
    );
}

bool CMentee::IsMentorLogIn() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentee);
    return m_Mentor.nLogin == MentorState::LOGIN;
}

bool CMentee::CanBeBuffed() {
    L2_GUARD_FUNCTION();

    User* const user = ResolveUser(m_nMenteeSId);
    return user != nullptr && user->m_data->m_bIsAlive == 0 && m_Mentor.nCharDBId > 0;
}

void CMentee::SetMenteeStartTime(std::uint32_t time) {
    L2_GUARD_FUNCTION();
    m_MenteeStartTime = time;
}

std::uint32_t CMentee::GetMenteeStartTime() {
    L2_GUARD_FUNCTION();
    return m_MenteeStartTime;
}

void CMentee::SetMentorLogoutTime(std::uint32_t time) {
    L2_GUARD_FUNCTION();
    m_MentorLogoutTime = time;
}

std::uint32_t CMentee::GetMentorLogoutTime() {
    L2_GUARD_FUNCTION();
    return m_MentorLogoutTime;
}

CMentor::CMentor() = default;

CMentor::~CMentor() = default;

void CMentor::Init(std::uint32_t mentorSId) {
    L2_GUARD_FUNCTION();

    m_nMentorSId = mentorSId;
    m_mapMentees.clear();
    m_nSomethingChanged = 0;
    m_tPenaltyEnd = 0;
    m_nCanGetBuff = 0;
    m_nInfectedSkillByMentee = 0;
}

void CMentor::DeleteMentorBuff() {
    // Current L2Server 0x00837050, original owner Mentor.cpp.
    L2_GUARD_FUNCTION();

    User* const mentor = ResolveUser(m_nMentorSId);
    if (mentor == nullptr || m_nInfectedSkillByMentee <= 0) {
        return;
    }
    runtime::raw_atomic::ExchangeI32(&m_nInfectedSkillByMentee, 0);

    std::vector<int> deleteIndices;
    {
        L2_GUARD(mentor->m_InfectedSkillsSect);
        for (std::size_t index = 0; index < mentor->m_InfectedSkills.size(); ++index) {
            const InfectedSkillType& infected = runtime::CheckedVectorAt(mentor->m_InfectedSkills, index);
            if (CMentorSetting::inst().IsMentorBuff(infected.pSkillInfo->m_nSkillId)) {
                deleteIndices.push_back(static_cast<int>(index));
            }
        }
        for (std::size_t reverseIndex = deleteIndices.size(); reverseIndex > 0; --reverseIndex) {
            const int index = runtime::CheckedVectorAt(deleteIndices, reverseIndex - 1);
            const CSkillInfo* const skillInfo =
                runtime::CheckedVectorAt(mentor->m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
            mentor->_DeleteAbnormalStatus(index, mentor, skillInfo, true, SPET_DISPEL, true);
        }
    }
    mentor->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
}

void CMentor::ActivateMentorBuff(const int buffLevel) {
    // Current L2Server 0x00837fa4.
    L2_GUARD_FUNCTION();

    User* const mentor = ResolveUser(m_nMentorSId);
    if (mentor == nullptr || m_nInfectedSkillByMentee == buffLevel || buffLevel <= 0) {
        return;
    }

    for (const auto& entry : CMentorSetting::inst().m_MentorSkills) {
        const int level = g_bIsMentorBuffed ? buffLevel : entry.second.m_PrimaryKey;
        const CSkillKey key(entry.first, level, entry.second.m_SecondaryKey);
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, nullptr);
        if (skill != nullptr) {
            DeleteMentorBuff();
            skill->ActivateSkill(mentor, mentor, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            m_nInfectedSkillByMentee = level;
        }
    }
}

void CMentor::OnTimerExpired() {
    // Current L2Server 0x00838174.
    L2_GUARD_FUNCTION();

    User* const mentor = ResolveUser(m_nMentorSId);
    if (mentor == nullptr || !mentor->m_data->m_bIsActive || mentor->m_data->m_bIsAlive == 0) {
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(&m_nSomethingChanged, 0) > 0) {
        mentor->SendMentorList();

        L2_GUARD(m_lockMentor);
        m_nCanGetBuff = 0;
        for (const auto& entry : m_mapMentees) {
            const MentorInfo mentorInfo = entry.second;
            CSPointer<User> mentee = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentWorldId(), mentorInfo.nCharDBId)
            );
            if (!mentee) {
                continue;
            }
            if (!g_bIsMentorBuffed) {
                m_nCanGetBuff = 1;
                break;
            }
            ++m_nCanGetBuff;
        }
    }

    if (m_nCanGetBuff > 0) {
        ActivateMentorBuff(m_nCanGetBuff);
    } else {
        DeleteMentorBuff();
    }
}

bool CMentor::AddMentee(std::uint32_t menteeSId, MentorInfo& mentorInfo) {
    // Current L2Server 0x00835688, original owner Mentor.cpp.
    L2_GUARD_FUNCTION();

    if (m_mapMentees.size() >= 3) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Overflow Mentee Count[%d]",
        //     L"Mentor.cpp",
        //     0x21,
        //     menteeSId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Overflow Mentee Count[%d]",
        //     L"Mentor.cpp",
        //     0x21,
        //     menteeSId)
        L2_LOG_WARN(
            Log, "Mentor object %u cannot register mentee %u: the three-mentee limit has been reached.",
            static_cast<unsigned>(m_nMentorSId), static_cast<unsigned>(menteeSId)
        );
        return false;
    }

    {
        L2_GUARD(m_lockMentor);
        static_cast<void>(m_mapMentees.emplace(menteeSId, mentorInfo));
    }
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);

    if (User* const mentor = ResolveUser(m_nMentorSId)) {
        mentor->AcquireMentorMenteeSkills(ESAT_MENTOR);
    }
    return true;
}

bool CMentor::DeleteMentee(std::uint32_t menteeDatabaseId, bool notify) {
    // Current L2Server 0x00837c18, original owner Mentor.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<User> onlineMentee;
    bool foundMentee = false;
    {
        L2_GUARD(m_lockMentor);
        const auto found = m_mapMentees.find(menteeDatabaseId);
        if (found != m_mapMentees.end()) {
            foundMentee = true;
            const MentorInfo mentorInfo = found->second;
            onlineMentee = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), mentorInfo.nCharDBId)
            );
            if (onlineMentee) {
                const std::uint32_t now = runtime::GetTickCount();
                onlineMentee->UpdateMenteePlaytime((now - onlineMentee->m_Mentee.GetMenteeStartTime()) / 1000);
                onlineMentee->m_Mentee.SetMenteeStartTime(0);
            }
            m_mapMentees.erase(found);
        }
    }
    if (!foundMentee) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot delete Mentee[%d]",
        //     L"Mentor.cpp",
        //     0x76,
        //     menteeDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot delete Mentee[%d]",
        //     L"Mentor.cpp",
        //     0x76,
        //     menteeDatabaseId)
        L2_LOG_WARN(
            Log,
            "Mentor object %u cannot remove mentee %u: no matching relationship is stored; remaining cleanup is not "
            "performed.",
            static_cast<unsigned>(m_nMentorSId), static_cast<unsigned>(menteeDatabaseId)
        );
        return false;
    }

    User* const mentor = ResolveUser(m_nMentorSId);
    if (mentor != nullptr) {
        if (GetPlayingMenteeCount() < 1 && GetMentorStartTime() != 0) {
            const std::uint32_t now = runtime::GetTickCount();
            mentor->UpdateMentorPlaytime((now - GetMentorStartTime()) / 1000);
            SetMentorStartTime(0);
        }
        if (m_nInfectedSkillByMentee == 0) {
            mentor->DeleteMentorMenteeSkills(ESAT_MENTOR);
            DeleteMentorBuff();
        }
        mentor->SendMentorList();
    }

    if (notify) {
        CSPointer<User> mentee = g_UserDBIDMap.FindObject(
            std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), static_cast<int>(menteeDatabaseId))
        );
        if (mentee) {
            mentee->ResetMentor();
        }
    }
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
    return true;
}

int CMentor::GetPlayingMenteeCount() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    int count = 0;
    for (const auto& entry : m_mapMentees) {
        const MentorInfo mentorInfo = entry.second;
        if (mentorInfo.nLogin != MentorState::LOGOUT) {
            ++count;
        }
    }
    return count;
}

int CMentor::GetMenteeDBId(wchar_t* name) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    for (const auto& entry : m_mapMentees) {
        if (EqualMentorName(entry.second.sCharName, name)) {
            return static_cast<int>(entry.first);
        }
    }
    return 0;
}

void CMentor::MakeMenteeList(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    for (const auto& entry : m_mapMentees) {
        const MentorInfo mentorInfo = entry.second;
        if (!packet.Assemble(
                "dSddd", mentorInfo.nCharDBId, mentorInfo.sCharName, static_cast<int>(mentorInfo.nClass),
                static_cast<int>(mentorInfo.nLevel), static_cast<int>(mentorInfo.nLogin)
            )) {
            break;
        }
    }
}

void CMentor::ShowMenteeList(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        return;
    }

    char16_t systemContext[] = u"SYS";
    socket->SendSystemMessage(systemContext, u"---------Mentee List----------");

    L2_GUARD(m_lockMentor);
    int index = 1;
    for (const auto& entry : m_mapMentees) {
        const MentorInfo mentorInfo = entry.second;
        std::array<char16_t, 256> message{};
        FormatUtf16To(
            message.data(), message.size(), u"[%d] [%d]charName[%s] level[%d] class[%d] login[%d]", index,
            mentorInfo.nCharDBId, MentorNameForHost(mentorInfo.sCharName, std::size(mentorInfo.sCharName)),
            static_cast<int>(mentorInfo.nLevel), static_cast<int>(mentorInfo.nClass), static_cast<int>(mentorInfo.nLogin)
        );
        socket->SendSystemMessage(systemContext, message.data());
        ++index;
    }
}

void CMentor::SetPenaltyEnd(std::int32_t penaltyEnd) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);
    m_tPenaltyEnd = penaltyEnd;
}

void CMentor::SetMenteeLoginStatus(int menteeSId, MentorState::LoginStatus loginStatus) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    const auto found = m_mapMentees.find(static_cast<std::uint32_t>(menteeSId));
    if (found != m_mapMentees.end()) {
        found->second.nLogin = loginStatus;
    }
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
}

void CMentor::SetLoginStateToMentee(MentorState::LoginStatus loginStatus) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    if (GetMentorStartTime() != 0) {
        if (User* const mentor = ResolveUser(m_nMentorSId)) {
            const std::uint32_t mentorStartTime = GetMentorStartTime();
            const std::uint32_t now = runtime::GetTickCount();
            mentor->UpdateMentorPlaytime((now - mentorStartTime) / 1000);
            SetMentorStartTime(0);
        }
    }

    for (auto& entry : m_mapMentees) {
        const MentorInfo mentorInfo = entry.second;
        if (mentorInfo.nLogin == MentorState::LOGOUT) {
            continue;
        }

        CSPointer<User> mentee =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentWorldId(), mentorInfo.nCharDBId));
        if (!mentee) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot find Mentee[%d]",
            //     L"Mentor.cpp",
            //     0xf1,
            //     mentorInfo.nCharDBId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Cannot find Mentee[%d]",
            //     L"Mentor.cpp",
            //     0xf1,
            //     mentorInfo.nCharDBId)
            L2_LOG_WARN(
                Log,
                "Mentee character %u is marked online but is unavailable; marking the relationship offline and continuing "
                "with other mentees.",
                static_cast<unsigned>(mentorInfo.nCharDBId)
            );
            entry.second.nLogin = MentorState::LOGOUT;
            SomethingChange();
            continue;
        }

        mentee->SetMentorLoginStatus(static_cast<int>(loginStatus));
        mentee->m_Mentee.SetMentorLogoutTime(runtime::GetTickCount());
        const std::uint32_t menteeStartTime = mentee->m_Mentee.GetMenteeStartTime();
        const std::uint32_t now = runtime::GetTickCount();
        mentee->UpdateMenteePlaytime((now - menteeStartTime) / 1000);
        mentee->m_Mentee.SetMenteeStartTime(0);
    }
}

void CMentor::UpdateMentorStatus() {
    L2_GUARD_FUNCTION();

    User* const mentor = ResolveUser(m_nMentorSId);
    if (!mentor) {
        return;
    }

    L2_GUARD(m_lockMentor);
    for (const auto& entry : m_mapMentees) {
        const MentorInfo mentorInfo = entry.second;
        if (mentorInfo.nLogin == MentorState::LOGOUT) {
            continue;
        }

        CSPointer<User> mentee =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentWorldId(), mentorInfo.nCharDBId));
        if (mentee) {
            mentee->m_Mentee.UpdateMentorStatus(*mentor);
        }
    }
}

void CMentor::UpdateMenteeStatus(User& user) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    const std::uint32_t menteeSId = user.GetDBID();
    const auto found = m_mapMentees.find(menteeSId);
    if (found != m_mapMentees.end()) {
        found->second.nLevel = static_cast<std::int16_t>(user.m_data->m_nLevel);
        found->second.nClass = static_cast<std::int16_t>(static_cast<std::int32_t>(user.m_data->m_nClass));
    } else if (User* const mentor = ResolveUser(m_nMentorSId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [%s]Cannot find Mentee[%s]",
        //     L"Mentor.cpp",
        //     0x138,
        //     mentor->GetName(),
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [%s]Cannot find Mentee[%s]",
        //     L"Mentor.cpp",
        //     0x138,
        //     mentor->GetName(),
        //     user.GetName())
        // L2_LOG_WARN(Log,
        //     "Mentor '%s' has no stored relationship for mentee '%s' (character %u). Level/class updates are skipped but
        //     the change counter is still incremented.", WideToUtf8(mentor->GetName()).c_str(),
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned>(menteeSId))
        L2_LOG_WARN(
            Log,
            "Mentor '%s' has no stored relationship for mentee '%s' (character %u); level/class updates are skipped but the "
            "change counter is still incremented.",
            Utf16ToUtf8(mentor->GetName()).c_str(), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned>(menteeSId)
        );
    }
    runtime::raw_atomic::IncrementI32(&m_nSomethingChanged);
}

const char16_t* CMentor::GetMenteeName(int menteeSId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lockMentor);

    const auto found = m_mapMentees.find(static_cast<std::uint32_t>(menteeSId));
    if (found == m_mapMentees.end()) {
        return u"";
    }
    // The original returns the stored UTF-16 name itself.
    return found->second.sCharName;
}

void CMentor::SetMentorStartTime(std::uint32_t mentorStartTime) {
    L2_GUARD_FUNCTION();
    m_mentorStartTime = mentorStartTime;
}

std::uint32_t CMentor::GetMentorStartTime() {
    L2_GUARD_FUNCTION();
    return m_mentorStartTime;
}

bool CMentor::IsMyMentee(int menteeSId) {
    L2_GUARD(m_lockMentor);
    return m_mapMentees.find(static_cast<std::uint32_t>(menteeSId)) != m_mapMentees.end();
}
