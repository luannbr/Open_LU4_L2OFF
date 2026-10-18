#include "InstantZone.h"
#include "ParserInstantZone.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"

#include "Area.h"
#include "CWorld.h"
#include "Chair.h"
#include "Creature.h"
#include "DB.h"
#include "DailyMission.h"
#include "Default.h"
#include "Door.h"
#include "EventController.h"
#include "HTMLCache.h"
#include "Item.h"
#include "L2Time.h"
#include "MakerEvent.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeRaid.h"
#include "Setting.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SharedContainer.h"
#include "StaticObject.h"
#include "ToDoList.h"
#include "Trap.h"
#include "User.h"
#include "UserSocket.h"
#include "WorldCollision.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {


std::u16string TargetIntegerString(const int value) {
    const std::string digits = std::to_string(value);
    return std::u16string(digits.begin(), digits.end());
}


template <std::size_t Capacity>
void CopyTargetWide(char16_t (&destination)[Capacity], const wchar_t* const source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        return;
    }

    const std::u16string target = WideToUtf16(std::wstring_view(source));
    const std::size_t count = std::min(target.size(), Capacity - 1);
    std::copy_n(target.data(), count, destination);
}

template <std::size_t Capacity>
void CopyTargetWide(char16_t (&destination)[Capacity], const char16_t* const source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        return;
    }
    const std::u16string_view target(source);
    std::copy_n(target.data(), std::min(target.size(), Capacity - 1), destination);
}

void AppendKamarokaMember(
    CPacketHelperEx<20480>& packet, std::u16string& memberList, User* const user, const bool prependSeparator
) {
    packet.Assemble("d", user != nullptr ? static_cast<int>(user->GetDBID()) : 0);
    if (prependSeparator) {
        memberList += u", ";
    }
    if (user == nullptr) {
        memberList += u"UNKNOWN";
        return;
    }
    memberList += std::u16string(user->GetName());
}

void SetKamarokaHtmlParameter(CFHTML::StrStrMapType& parameters, const wchar_t* const key, const std::u16string& value) {
    parameters[WideToUtf16(key)] = value;
}

void ShowKamarokaHtml(User& user, const wchar_t* const fileName, const CFHTML::StrStrMapType& parameters) {
    const std::u16string html =
        CFHTML::MakeHTMLFromStrMap(WideToUtf16(fileName).c_str(), parameters, user.m_data->m_CountryCode);
    const std::u16string hostHtml(html);
    user.ShowHTML(nullptr, hostHtml.c_str(), 0, HTML::LAUNCH, true);
}

void CopyInstantZoneFailUser(char16_t (&destination)[24], const wchar_t* const source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        return;
    }

    const std::u16string target = WideToUtf16(std::wstring_view(source));
    const std::size_t count = std::min<std::size_t>(target.size(), std::size(destination) - 1);
    std::copy_n(target.data(), count, destination);
}

void ClearForcedSunRise(User* const user, volatile std::int32_t* const forceFlag) {
    if ((runtime::raw_atomic::LoadI32Relaxed(forceFlag) & 0xff) != 1) {
        return;
    }

    runtime::raw_atomic::ExchangeI32(forceFlag, 0);
    const bool isNight = g_L2Time.m_bIsNight;
    user->GetSocket()->Send("c", isNight ? 0x13 : 0x12);
}

void WriteInstantZoneLeaveLog(
    User* const user, const int zoneTypeId, const std::uint32_t zoneId, const std::int32_t partyId, const FVector& position
) {
    CLogFormatter formatter;
    const std::int32_t z = runtime::Win64CvttDoubleToInt32(position.z);
    const std::int32_t y = runtime::Win64CvttDoubleToInt32(position.y);
    const std::int32_t x = runtime::Win64CvttDoubleToInt32(position.x);
    const CSharedCreatureData* const data = user->m_data;
    const char16_t* const name = user->GetName();
    const int inZone = g_ServerContext.InZone.load(std::memory_order_seq_cst);
    const int level = data->m_nLevel;
    const int classType = static_cast<int>(data->m_nClass);
    const int sex = static_cast<int>(data->m_Sex);
    const int race = static_cast<int>(data->m_Race);
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t actorId = user->GetDBID();

    // Preserve the existing host-wide to target-UTF16 logging boundary.
    std::u16string actorName;
    if (name != nullptr) {
        actorName = std::u16string(name);
    }
    formatter.WriteLog(
        0x379, actorId, accountId, 0, 0, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
        static_cast<std::int64_t>(z), nullptr, nullptr, nullptr, race, sex, classType, level, zoneTypeId, zoneId, partyId,
        inZone, 0, 0, actorName.empty() ? nullptr : actorName.c_str(), user->m_sAccountName.data(), nullptr, nullptr, 0
    );
}

} // namespace

namespace InstantZoneLogHelper {

void Log(
    CInstantZone* const zone,
    User* const user,
    const int logId,
    const InZoneEnterType enterType,
    const int timeLimit,
    const int reason
) {
    L2_GUARD_FUNCTION();
    if (zone == nullptr || user == nullptr) {
        return;
    }

    int groupId = 0;
    int memberCount = 0;
    if (enterType == IZET_MPCC || enterType == IZET_MAX) {
        if (CMultiPartyCommandChannel* const channel = user->GetMPCC()) {
            groupId = static_cast<int>(channel->Id());
            memberCount = channel->GetMemberCount();
        }
    } else if (CParty* const party = user->GetParty()) {
        groupId = static_cast<int>(party->Id());
        memberCount = party->GetMemberCount();
    }

    CLogFormatter formatter;
    formatter.WriteLog1_1(
        logId, user, nullptr, nullptr, nullptr, nullptr, zone->GetKey().typeId, zone->Id(), groupId, memberCount, timeLimit,
        reason, 0
    );
}

} // namespace InstantZoneLogHelper

bool CInstantZoneEntranceCondition_check_level::CanEnter(
    CObject* const target, const CInstantZone*, const bool reenter, int
) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }
    if (reenter && !bCheckReEnter) {
        return true;
    }

    CCreature* const creature = target->CastCreature();
    if (creature == nullptr || creature->m_data == nullptr) {
        return false;
    }
    const int minimum = nMinLevel - (reenter ? 1 : 0);
    const int maximum = nMaxLevel + (reenter ? 1 : 0);
    const int level = creature->m_data->m_nLevel;
    return minimum <= level && level <= maximum;
}

bool CInstantZoneEntranceCondition_check_quest::CanEnter(
    CObject* const target, const CInstantZone*, const bool reenter, int
) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }
    if (reenter && !bCheckReEnter) {
        return true;
    }

    User* const user = target->CastUser();
    return user != nullptr && user->m_data != nullptr &&
        (user->m_data->GetOneTimeFlag(nQuestID) || user->m_data->HaveMemo(nQuestID));
}

bool CInstantZoneEntranceCondition_check_pledge::CanEnter(
    CObject* const target, const CInstantZone*, const bool reenter, const int bindingId
) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }
    if (reenter && !bCheckReEnter) {
        return true;
    }

    User* const user = target->CastUser();
    if (user == nullptr || user->m_data == nullptr) {
        return false;
    }
    const int pledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    return pledgeId != 0 && pledgeId == bindingId;
}

bool CInstantZoneEntranceCondition_check_party_chair::CanEnter(
    CObject* const target, const CInstantZone*, const bool reenter, int
) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }
    if (reenter && !bCheckReEnter) {
        return true;
    }

    User* const user = target->CastUser();
    if (user == nullptr || user->m_data == nullptr) {
        return false;
    }
    CStaticObject* const staticObject = user->m_spSittingChair.GetStaticObject();
    if (staticObject == nullptr || !staticObject->IsChair() || user->m_data->m_nPrivateStoreType != 0) {
        return false;
    }
    CChair* const chair = staticObject->CastChair();
    if (chair == nullptr) {
        return false;
    }
    for (const int groupId : m_vChairGroupList) {
        if (groupId == chair->m_nGroupId) {
            return g_ChairManager.CheckInzoneCondition(user, chair, 1);
        }
    }
    return false;
}

CInstantZoneEntranceCondition_check_exclusive_point_inzone::CInstantZoneEntranceCondition_check_exclusive_point_inzone(
    const bool checkReEnter
) :
    CInstantZoneEntranceCondition(EFR_EXCLUSIVE_POINT_INZONE_FAIL),
    m_itCycle(Weekly),
    m_itDayOfWeek(Wed),
    m_itHour(6),
    m_itMin(30),
    m_nCycleTime(0),
    m_padding054{} {
    bCheckReEnter = checkReEnter;
    reason = EFR_EXCLUSIVE_POINT_INZONE_FAIL;
}

int CInstantZoneEntranceCondition_check_exclusive_point_inzone::CalcIterationTime() {
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return m_nCycleTime;
    }
    if (m_itCycle != Weekly || now <= m_nCycleTime + 0x93a80) {
        return m_nCycleTime;
    }

    L2_GUARD(m_lockCycleTime);
    if (now <= m_nCycleTime + 0x93a80) {
        return m_nCycleTime;
    }

    local.tm_hour = m_itHour;
    local.tm_min = m_itMin;
    local.tm_sec = 0;
    m_nCycleTime = runtime::crt::Mktime32(&local);
    int days = local.tm_wday - static_cast<int>(m_itDayOfWeek);
    if (days < 0) {
        days += 7;
    }
    if (days > 0) {
        m_nCycleTime -= days * 0x15180;
    }
    return m_nCycleTime;
}

bool CInstantZoneEntranceCondition_check_exclusive_point_inzone::CanEnter(
    CObject* const target, const CInstantZone* const instantZone, const bool reenter, int
) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsPC()) {
        return false;
    }
    if (reenter && !bCheckReEnter) {
        return true;
    }

    User* const user = target->CastUser();
    if (user == nullptr || instantZone == nullptr) {
        return false;
    }
    for (const int typeId : m_vExclusiveList) {
        const User::UserInzonePoint point = user->GetInzonePointInfo(typeId);
        if (point.m_nTypeId != instantZone->m_nTypeId && CalcIterationTime() < point.m_nTimeStamp) {
            return false;
        }
    }
    return true;
}

CInstantZoneDB g_InstantZoneDB;
CInstantZoneManager g_InstantZoneMgr;
volatile std::int32_t g_nInZoneNPCCount = 0;

CPremiumInstantRankingSystem* CPremiumInstantRankingSystem::m_RankingSystem = nullptr;

CPremiumInstantRankingSystem::CPremiumInstantRankingSystem() :
    m_nStatus(KS_NONE), m_nMaxRank(50) {
    for (int day = 0; day < 7; ++day) {
        SharedKamarokaInfo::GetInfo()->m_bKamaCycle[day] = false;
    }
    SharedKamarokaInfo::GetInfo()->m_bKamaCycle[6] = true;
    SharedKamarokaInfo::GetInfo()->m_bKamaCycle[0] = true;
    AddTimer(10000, 0);
}

CPremiumInstantRankingSystem::~CPremiumInstantRankingSystem() = default;

CPremiumInstantRankingSystem& CPremiumInstantRankingSystem::Inst() {
    L2_GUARD_FUNCTION();
    if (m_RankingSystem == nullptr) {
        m_RankingSystem = new CPremiumInstantRankingSystem;
    }
    return *m_RankingSystem;
}

void CPremiumInstantRankingSystem::Init() {
    L2_GUARD_FUNCTION();
    for (int rankPage = 1; rankPage <= 10; ++rankPage) {
        char16_t fileName[260]{};
        FormatUtf16To(fileName, std::size(fileName), u"g_kamaroka_defender_rank00%d.htm", rankPage);
        CopyTargetWide(m_pszHTMLFileName[rankPage - 1], fileName);
    }
    CopyTargetWide(m_pszHTMLFileName[9], u"g_kamaroka_defender_rank010.htm");
    m_nRound = -1;
    db.RequestPremiumKamarokaRound();
}

void CPremiumInstantRankingSystem::LoadFromDB() {
    L2_GUARD_FUNCTION();
    int round = 0;
    if (m_nStatus == KS_RAID) {
        round = m_nRound;
    } else if (m_nStatus == KS_GIVE_REWARD) {
        round = m_nRound - 1;
        if (round < 1) {
            return;
        }
    } else {
        return;
    }
    db.RequestPremiumKamarokaRank(round, 50);
}

void CPremiumInstantRankingSystem::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (m_nStatus == KS_NONE) {
        return;
    }

    std::tm current{};
    if (runtime::crt::LocalTime32(runtime::crt::Time32Now(), current) && current.tm_hour == 0 && current.tm_min == 0) {
        if (m_nStatus == KS_RAID && IsRewardDay()) {
            SetStatusGiveReward();
            AddNextRound();
        } else if (m_nStatus == KS_GIVE_REWARD && IsRaidDay()) {
            SetStatusRaid();
        }
    }
    AddTimer(1000, 0);
}

bool CPremiumInstantRankingSystem::IsRewardDay() {
    L2_GUARD_FUNCTION();
    const runtime::SystemTime currentSystemTime = runtime::GetLocalTimeNow();
    const std::int32_t currentTime = runtime::crt::Time32Now();
    std::tm current{};
    if (!runtime::crt::LocalTime32(currentTime, current) || current.tm_hour < 0 || current.tm_min < 0) {
        return false;
    }
    std::tm previous{};
    if (!runtime::crt::LocalTime32(currentTime - TEN_MINUTES, previous)) {
        return false;
    }
    return IsRaidTime(static_cast<unsigned short>(previous.tm_wday)) && !IsRaidTime(currentSystemTime.wDayOfWeek);
}

bool CPremiumInstantRankingSystem::IsRaidDay() {
    L2_GUARD_FUNCTION();
    const runtime::SystemTime currentSystemTime = runtime::GetLocalTimeNow();
    const std::int32_t currentTime = runtime::crt::Time32Now();
    std::tm current{};
    if (!runtime::crt::LocalTime32(currentTime, current) || current.tm_hour < 0 || current.tm_min < 0) {
        return false;
    }
    std::tm previous{};
    if (!runtime::crt::LocalTime32(currentTime - TEN_MINUTES, previous)) {
        return false;
    }
    return !IsRaidTime(static_cast<unsigned short>(previous.tm_wday)) && IsRaidTime(currentSystemTime.wDayOfWeek);
}

void CPremiumInstantRankingSystem::RequestRegisterNewRecord(const int score, CParty* const party) {
    L2_GUARD_FUNCTION();
    if (party == nullptr || party->GetMaster() == nullptr || m_nStatus != KS_RAID) {
        return;
    }

    CPacketHelperEx<20480> packet;
    std::u16string memberList;
    std::vector<User*> members;
    party->GetAllMember(members);
    bool prependSeparator = false;
    for (User* const member : members) {
        AppendKamarokaMember(packet, memberList, member, prependSeparator);
        prependSeparator = true;
    }

    db.RequestPremiumKamarokaAddNewRecord(
        m_nRound, score, static_cast<int>(party->GetMaster()->GetDBID()), memberList, packet
    );
}

void CPremiumInstantRankingSystem::RequestRegisterNewRecord(const int score, User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || !user->IsBuilder()) {
        return;
    }

    CPacketHelperEx<20480> packet;
    std::u16string memberList;
    int leaderCharacterId = static_cast<int>(user->GetDBID());
    if (CParty* const party = user->GetParty()) {
        leaderCharacterId = static_cast<int>(party->GetMaster()->GetDBID());
        std::vector<User*> members;
        party->GetAllMember(members);
        bool prependSeparator = false;
        for (User* const member : members) {
            AppendKamarokaMember(packet, memberList, member, prependSeparator);
            prependSeparator = true;
        }
    } else {
        AppendKamarokaMember(packet, memberList, user, false);
    }

    db.RequestPremiumKamarokaAddNewRecord(m_nRound, score, leaderCharacterId, memberList, packet);
}

void CPremiumInstantRankingSystem::RequestUserRankingInfo(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const int previousRound = m_nRound - 1;
    if (previousRound < 1) {
        CFHTML::StrStrMapType parameters;
        ShowKamarokaHtml(*user, L"g_kamaroka_defender_no_result.htm", parameters);
        return;
    }
    db.RequestPremiumKamarokaPersonalRankInfo(previousRound, user);
}

void CPremiumInstantRankingSystem::ReplyRankingList(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    m_vRankList.clear();

    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &count);
    if (count < 0) {
        return;
    }

    {
        L2_WRITE_GUARD(mRankingLock);
        for (int index = 0; index < count; ++index) {
            int rank = 0;
            int partyId = 0;
            int score = 0;
            char16_t memberList[128]{};
            cursor = ::Disassemble(cursor, "dddS", &rank, &partyId, &score, 0x100, memberList);

            PremiumInstanceRankInfo info;
            info.nPartyId = partyId;
            info.nRank = rank;
            info.nScore = score;
            info.wsMemberList = std::u16string(memberList);
            m_vRankList.push_back(info);
        }
    }
    LineUpRanking();
}

void CPremiumInstantRankingSystem::ReplyAddNewRecord(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int leaderCharacterId = 0;
    int partyId = 0;
    int score = 0;
    char16_t memberList[128]{};
    ::Disassemble(packet, "dddS", &leaderCharacterId, &partyId, &score, 0x100, memberList);
    AddNewRecord(partyId, 0, score, memberList);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), leaderCharacterId));
    if (user) {
        std::wstring hostMemberList = FixedUtf16ToWide(memberList);
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x1fb2, user.get(), nullptr, WideToUtf16(hostMemberList.data()), nullptr, nullptr, score, partyId, 0, 0, 0, 0, 0
        );
    }
    LineUpRanking();
}

void CPremiumInstantRankingSystem::LineUpRanking() {
    L2_GUARD_FUNCTION();
    if (m_vRankList.empty()) {
        return;
    }

    {
        L2_WRITE_GUARD(mRankingLock);
        if (!m_vRankList.empty()) {
            std::stable_sort(m_vRankList.begin(), m_vRankList.end(), SortRankByScore);
        }
    }

    int currentRank = 1;
    int ordinal = 1;
    int currentScore = m_vRankList.front().nScore;
    {
        L2_WRITE_GUARD(mRankingLock);
        for (PremiumInstanceRankInfo& info : m_vRankList) {
            if (info.nScore == currentScore) {
                info.nRank = currentRank;
            } else if (currentScore < info.nScore) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] Kamaroka Rank Sorting error..? Score[%d > %d]",
                //     "InstantZone.cpp",
                //     0x162e,
                //     info.nScore,
                //     currentScore)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] Kamaroka Rank Sorting error..? Score[%d > %d]",
                //     "InstantZone.cpp",
                //     0x162e,
                //     info.nScore,
                //     currentScore)
                L2_LOG_ERROR(
                    Log,
                    "Kamaroka ranking is out of descending score order: score %d exceeds %d; retaining rank %d for this "
                    "entry.",
                    info.nScore, currentScore, currentRank
                );
                info.nRank = currentRank;
            } else {
                info.nRank = ordinal;
                currentRank = ordinal;
                currentScore = info.nScore;
            }
            ++ordinal;
        }
    }
}

void CPremiumInstantRankingSystem::ShowRankingList(User* const user, const int page) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CFHTML::StrStrMapType parameters;
    for (int index = 1; index <= 5; ++index) {
        std::u16string score;
        std::u16string party;
        GetRankString(page * 5 - 5 + index, score, party);
        const std::wstring scoreKey = L"score" + std::to_wstring(index);
        const std::wstring partyKey = L"party" + std::to_wstring(index);
        SetKamarokaHtmlParameter(parameters, scoreKey.c_str(), score);
        SetKamarokaHtmlParameter(parameters, partyKey.c_str(), party);
    }

    const char16_t* const targetFileName = m_pszHTMLFileName[page - 1];
    const std::u16string html = CFHTML::MakeHTMLFromStrMap(targetFileName, parameters, user->m_data->m_CountryCode);
    user->GetSocket()->Send("cdSdd", 0x19, 0, html.c_str(), 0, 1);
}

void CPremiumInstantRankingSystem::ShowUserRankingInfo(User* const user, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    int partyId = 0;
    int score = 0;
    char16_t memberList[128]{};
    ::Disassemble(packet, "ddS", &partyId, &score, 0x100, memberList);

    CFHTML::StrStrMapType parameters;
    const int rank = GetRank(partyId);
    const wchar_t* fileName = nullptr;
    if (rank == -1) {
        if (score < 0) {
            fileName = L"g_kamaroka_defender_no_result.htm";
        } else {
            SetKamarokaHtmlParameter(parameters, L"myrank1", std::u16string(u"-"));
            SetKamarokaHtmlParameter(parameters, L"score1", TargetIntegerString(score));
            SetKamarokaHtmlParameter(parameters, L"party1", std::u16string(memberList));
            fileName = L"g_kamaroka_defender_rank_me.htm";
        }
    } else if (rank == 0) {
        fileName = L"g_kamaroka_defender_no_rank_me.htm";
    } else {
        SetKamarokaHtmlParameter(parameters, L"myrank1", TargetIntegerString(rank));
        SetKamarokaHtmlParameter(parameters, L"score1", TargetIntegerString(score));
        SetKamarokaHtmlParameter(parameters, L"party1", std::u16string(memberList));
        fileName = L"g_kamaroka_defender_rank_me.htm";
    }
    ShowKamarokaHtml(*user, fileName, parameters);
}

void CPremiumInstantRankingSystem::InitStatus() {
    L2_GUARD_FUNCTION();
    CheckKamarokaCycle();
}

void CPremiumInstantRankingSystem::SetRound(const int round) {
    L2_GUARD_FUNCTION();
    m_nRound = round < 1 ? 1 : round;
    SharedKamarokaInfo::GetInfo()->m_nRound = m_nRound;
}

void CPremiumInstantRankingSystem::AddNewRecord(const int partyId, int, const int score, const char16_t* const memberList) {
    L2_GUARD_FUNCTION();
    for (PremiumInstanceRankInfo& info : m_vRankList) {
        if (info.nPartyId == partyId) {
            info.nScore = score;
            return;
        }
    }

    PremiumInstanceRankInfo info;
    info.nPartyId = partyId;
    info.nRank = 0;
    info.nScore = score;
    info.wsMemberList = std::u16string(memberList);
    {
        L2_WRITE_GUARD(mRankingLock);
        m_vRankList.push_back(info);
    }
}

int CPremiumInstantRankingSystem::GetRankListIndex(const int rank) {
    L2_GUARD_FUNCTION();
    int index = 0;
    for (const PremiumInstanceRankInfo& info : m_vRankList) {
        if (info.nRank == rank) {
            return index;
        }
        ++index;
    }
    return -1;
}

int CPremiumInstantRankingSystem::GetRank(const int partyId) {
    L2_GUARD_FUNCTION();
    for (const PremiumInstanceRankInfo& info : m_vRankList) {
        if (info.nPartyId == partyId) {
            return info.nRank > m_nMaxRank ? 0 : info.nRank;
        }
    }
    return -1;
}

void CPremiumInstantRankingSystem::GetRankString(const int rank, std::u16string& score, std::u16string& party) {
    L2_GUARD_FUNCTION();
    score = std::u16string{};
    party = std::u16string{};
    const int rankIndex = GetRankListIndex(rank);
    if (rankIndex == -1) {
        score = std::u16string(u"-");
        party = std::u16string(u"-");
        return;
    }

    score = TargetIntegerString(runtime::CheckedVectorAt(m_vRankList, static_cast<std::size_t>(rankIndex)).nScore);
    for (const PremiumInstanceRankInfo& info : m_vRankList) {
        if (rank < info.nRank) {
            break;
        }
        if (info.nRank == rank) {
            party += info.wsMemberList;
            party += u"<br1>";
        }
    }
}

bool CPremiumInstantRankingSystem::SortRankByScore(PremiumInstanceRankInfo left, PremiumInstanceRankInfo right) {
    return right.nScore < left.nScore;
}

bool CPremiumInstantRankingSystem::IsRaidTime(const unsigned short day) {
    L2_GUARD_FUNCTION();
    return day < 7 && SharedKamarokaInfo::GetInfo()->m_bKamaCycle[day];
}

bool CPremiumInstantRankingSystem::IsRaidTime() { return m_nStatus == KS_RAID; }

bool CPremiumInstantRankingSystem::IsRewardTime() { return m_nStatus == KS_GIVE_REWARD; }

void CPremiumInstantRankingSystem::SetStatusRaid() {
    L2_GUARD_FUNCTION();
    m_nStatus = KS_RAID;
    SharedKamarokaInfo::GetInfo()->m_nState = KS_RAID;
    ClearRankingList();
}

void CPremiumInstantRankingSystem::SetStatusGiveReward() {
    L2_GUARD_FUNCTION();
    m_nStatus = KS_GIVE_REWARD;
    SharedKamarokaInfo::GetInfo()->m_nState = KS_GIVE_REWARD;
}

void CPremiumInstantRankingSystem::ClearRankingList() {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(mRankingLock);
        m_vRankList.clear();
    }
}

void CPremiumInstantRankingSystem::AddNextRound() {
    L2_GUARD_FUNCTION();
    ++m_nRound;
    SharedKamarokaInfo::GetInfo()->m_nRound = m_nRound;
    CPacketHelperEx<20480> packet;
    db.RequestPremiumKamarokaAddNewRecord(m_nRound, 0, 0, std::u16string(u"new round"), packet);
}

void CPremiumInstantRankingSystem::CheckKamarokaCycle() {
    L2_GUARD_FUNCTION();
    const runtime::SystemTime local = runtime::GetLocalTimeNow();
    if (local.wDayOfWeek < 7) {
        if (!SharedKamarokaInfo::GetInfo()->m_bKamaCycle[local.wDayOfWeek]) {
            SetStatusGiveReward();
        } else {
            SetStatusRaid();
        }
    }
}

void CPremiumInstantRankingSystem::SetKamarokaCycle(int cycle[7]) {
    L2_GUARD_FUNCTION();
    SharedKamarokaInfo* const shared = SharedKamarokaInfo::GetInfo();
    for (int day = 0; day < 7; ++day) {
        shared->m_bKamaCycle[day] = cycle[day] == 1;
    }
    CheckKamarokaCycle();
}

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(InfoForEntered)

InfoForEntered::InfoForEntered(
    const bool isAssociated,
    const InZoneEnterReasonType reason,
    const EntranceInfo& myEntrance,
    const EntranceInfo& groupEntrance,
    User* const user
) :
    associated(isAssociated),
    binded(false),
    padding01a{},
    eReason(reason),
    usedTime(0),
    myEntranceInfo(myEntrance),
    groupEntranceInfo(groupEntrance),
    logInfo{} {
    if (user != nullptr) {
        logInfo.accountId = static_cast<std::int32_t>(user->GetAccountID());
        logInfo.accountName.assign(user->m_sAccountName.data());
        const char16_t* const name = user->GetName();
        if (name != nullptr) {
            const std::u16string targetName = std::u16string(name);
            logInfo.charName.assign(targetName);
        }
    }
}

L2_DEFINE_OBJECT_RELEASE(InfoForEntered)

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(InfoForExisting)

InfoForExisting::InfoForExisting() : useTime(0), padding01c{} {}

L2_DEFINE_OBJECT_RELEASE(InfoForExisting)

L2_DEFINE_OBJECT_CLASS_ID(CInstantZone)

L2_DEFINE_OBJECT_CLASS_NAME(CInstantZone, m_pClassName_CInstantZone)

L2_DEFINE_OBJECT_ALLOCATOR(CInstantZone, __GetClassName())

L2_DEFINE_OBJECT_NEW(CInstantZone)

CInstantZone::CInstantZone() :
    m_nTypeId(0),
    m_nContentFinishTick(0),
    m_eStatus(IZS_INIT),
    m_paddingAfterStaticObjectList{},
    m_nTickCount(0),
    m_nExpelTickByNPCDown(0),
    m_nDeleteNoUserTick(0),
    MAX_ZONE_COUNT(0),
    m_nCurrentZoneCount(0),
    m_nInstantZoneDeleteCount(0),
    m_const(nullptr),
    m_extraDuration(0),
    m_Expiration(0),
    m_nInzoneDBId(0),
    m_pContainer(nullptr) {}

CInstantZone::CInstantZone(const CInstantZone& other) : CGeneralObject(other) {
    L2_GUARD_FUNCTION();
    m_nTypeId = other.m_nTypeId;
    m_nContentFinishTick = 0;
    m_eStatus = IZS_INIT;
    m_mapInstantZoneCreature.clear();
    m_mapInstantZoneItem.clear();
    m_mapInstantZoneDoor.clear();
    m_InstantZoneStaticObjectList.clear();
    _ClearAllUserMap();
    m_nTickCount = 0;
    m_nExpelTickByNPCDown = 0;
    m_nDeleteNoUserTick = 0;
    MAX_ZONE_COUNT = other.MAX_ZONE_COUNT;
    m_nCurrentZoneCount = other.m_nCurrentZoneCount;
    m_mapNameToCollisionMap = other.m_mapNameToCollisionMap;
    m_mapAreaOnOffFlag = other.m_mapAreaOnOffFlag;
    m_mapAreaIndex = other.m_mapAreaIndex;
    m_const = other.m_const;
    m_nInstantZoneDeleteCount = 0;
    m_extraDuration = 0;
}

CInstantZone::~CInstantZone() {
    if (m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        g_ServerContext.InZone.fetch_sub(1, std::memory_order_seq_cst);
    }
    m_const = nullptr;
}

L2_DEFINE_OBJECT_TYPED_RELEASE(CInstantZone)

void CInstantZone::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();
    if (IsDeleting() || m_Deleted != 0 || m_const == nullptr) {
        return;
    }

    if (id != 0) {
        const int areaIndex = id % 1000;
        CArea* const area = GetAreaByIndex(areaIndex);
        if (area == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid Area Index[%d]",
            //     L"InstantZone.cpp",
            //     0xb40,
            //     areaIndex)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid Area Index[%d]",
            //     L"InstantZone.cpp",
            //     0xb40,
            //     areaIndex)
            L2_LOG_ERROR(
                Log, "Cannot update instant zone type %d area timer %d: area index %d is missing.", m_nTypeId, id, areaIndex
            );
            return;
        }

        const auto flag = m_mapAreaOnOffFlag.find(area);
        if (flag == m_mapAreaOnOffFlag.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid Area flag[%s]",
            //     L"InstantZone.cpp",
            //     0xb3b,
            //     area->m_FieldTerritory.sName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid Area flag[%s]",
            //     L"InstantZone.cpp",
            //     0xb3b,
            //     area->m_FieldTerritory.sName.c_str())
            L2_LOG_ERROR(
                Log, "Cannot update instant zone type %d area %s: on/off state is not registered.", m_nTypeId,
                Utf16ToUtf8(runtime::StringView(area->m_FieldTerritory.sName)).c_str()
            );
            return;
        }

        int randomDelay = 0;
        if (area->m_nRandomTime > 0) {
            randomDelay = static_cast<int>(::genrand64_real2() * static_cast<double>(area->m_nRandomTime) + 1.0);
        }
        const bool turnOn = !flag->second;
        const int delay = randomDelay + (flag->second ? area->m_nOnTime : area->m_nOffTime);
        SetAreaOnOff(area, turnOn);
        AddTimer(delay, areaIndex + 10000000);
        return;
    }

    ++m_nTickCount;
    if (ExpelByNPCDownOnTimer()) {
        return;
    }
    if (m_const->m_bindingTiming > 0) {
        BindUserOnTimerExpired();
    }

    if (GetUserCount() == 0 && m_nTickCount > 60) {
        if (m_const->m_nDeleteNoUser == 0 || m_nDeleteNoUserTick == m_nTickCount) {
            g_InstantZoneMgr.DeleteInstantZone(*this, IZDI_NO_USER);
            return;
        }
        if (m_nDeleteNoUserTick == 0) {
            m_nDeleteNoUserTick = m_nTickCount + m_const->m_nDeleteNoUser;
        }
    } else {
        m_nDeleteNoUserTick = 0;
    }

    if (m_eStatus == IZS_CONTENT_FINISH) {
        if (m_nContentFinishTick == 0) {
            m_nContentFinishTick = m_nTickCount + m_Expiration * 60;
            SendRemainTimeMessage(0x83b, m_Expiration);
        } else if (m_nTickCount == m_nContentFinishTick - 60) {
            SendRemainTimeMessage(0x83b, 1);
        } else if (m_nContentFinishTick <= m_nTickCount) {
            ExpelAllUser(false);
            g_InstantZoneMgr.DeleteInstantZone(*this, IZDI_CONTENTS_FINISHED);
            return;
        }
    } else {
        const int expiration = m_const->m_nDuration + m_extraDuration;
        if (m_nTickCount == expiration - 600) {
            SendRemainTimeMessage(0x83a, 10);
        } else if (m_nTickCount == expiration - 300) {
            SendRemainTimeMessage(0x83a, 5);
        } else if (m_nTickCount == expiration - 60) {
            SendRemainTimeMessage(0x83a, 1);
        } else if (expiration <= m_nTickCount) {
            ExpelAllUser(true);
            g_InstantZoneMgr.DeleteInstantZone(*this, IZDI_DURATION_EXPIRED);
            return;
        }
    }
    AddTimer(1000, 0);
}

CObject* CInstantZone::Copy() {
    L2_GUARD_FUNCTION();
    CInstantZone* const copy = new CInstantZone(*this);
    if (copy != nullptr) {
        if (copy->m_nTypeId != 1 &&
            (copy->m_const->m_InZoneType == IZT_NORMAL || copy->m_const->m_InZoneType == IZT_PLEDGE_RAID)) {
            copy->AddTimer(1000, 0);
        }
        copy->m_eStatus = IZS_READY;
        copy->OnCreate();
    }
    return copy;
}

void CInstantZone::OnCreate() {
    L2_GUARD_FUNCTION();
    if (m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        g_ServerContext.InZone.fetch_add(1, std::memory_order_seq_cst);
    }
    CGeneralObject::OnCreate();
    m_pContainer = CContainerDB::Inst().AllocPrivateContainer(static_cast<int>(Id()));
}

CInstantZoneReleaseVerifier::CInstantZoneReleaseVerifier(CInstantZone* instantZone) : m_pInZone(instantZone) {
    L2_ADD_REF(m_pInZone, ObjectRefType::VerifierRetain);
    AddTimer(10000, 1);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CInstantZoneReleaseVerifier::TimerExpired(int) {
    m_pInZone->OnRelease();
    const std::int32_t remaining = L2_RELEASE(m_pInZone, ObjectRefType::VerifierRetain);
    if (remaining > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ORT - InZone(%d) Release failed. "
        //     L"refcount[%d] typeid[%d]",
        //     L"InstantZone.cpp",
        //     0xa3f,
        //     static_cast<int>(m_pInZone->Id()),
        //     m_pInZone->RefCount(),
        //     m_pInZone->m_nTypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ORT - InZone(%d) Release failed. "
        //     L"refcount[%d] typeid[%d]",
        //     L"InstantZone.cpp",
        //     0xa3f,
        //     static_cast<int>(m_pInZone->Id()),
        //     m_pInZone->RefCount(),
        //     m_pInZone->m_nTypeId)
        L2_LOG_ERROR(
            Log,
            "Instant zone object %u (type %d) still has references after verifier release: release result %d, current count "
            "%d.",
            static_cast<unsigned>(m_pInZone->Id()), m_pInZone->m_nTypeId, remaining, m_pInZone->RefCount()
        );
        m_pInZone->ReportOrt("InstantZone.cpp", 0xa41);
        g_ServerContext.NotReleasedInZone.fetch_add(1, std::memory_order_seq_cst);
    }
}

bool CInstantZoneConst::CheckEndPosType(const EndPositionType type, const bool disorder) {
    L2_GUARD_FUNCTION();
    return (disorder ? m_eDisorderEndPosType : m_eOrderEndPosType) == type;
}

int CInstantZoneConst::GetLatesetResetBindingDay(const int weekDay, const int minuteOfDay) {
    L2_GUARD_FUNCTION();
    return ResetBindingDayHelper::FindLatestResetBindingDay(m_resetBindingDay, m_resetBindingTime, weekDay, minuteOfDay);
}

int CInstantZoneConst::GetNextResetBindingDay(const int weekDay, const int minuteOfDay) {
    L2_GUARD_FUNCTION();
    return ResetBindingDayHelper::FindNextResetBindingDay(m_resetBindingDay, m_resetBindingTime, weekDay, minuteOfDay);
}

CInstantZoneConst::CInstantZoneConst() :
    m_InZoneType(IZT_NORMAL),
    m_nTypeId(0),
    m_nMaxUser(0),
    m_nMinUser(1),
    m_nDuration(0),
    m_nTimeLimit(0),
    m_nDeleteNoUser(0),
    m_bSpawnNpc(false),
    m_bIsAssociative(false),
    m_bToDoListCheck(false),
    m_padding0bf{},
    m_ToDayPriority(0),
    m_eStartPosType(SPT_MAX),
    m_eOrderEndPosType(EPT_MAX),
    m_eDisorderEndPosType(EPT_MAX),
    m_bindingTiming(0),
    m_resetBindingTime(0),
    m_resetBindingDay(0),
    m_pccafePoint(0),
    m_removeBuff(0),
    m_padding144{},
    m_instantZoneGroup(nullptr),
    m_clusterId(0),
    m_padding154{},
    m_instantZoneCluster(nullptr),
    m_MaxParty(0),
    m_LevelMin(0),
    m_LevelMax(0),
    m_padding16c{} {
    L2_GUARD_FUNCTION();
}

CInstantZoneConst::CInstantZoneConst(const CInstantZoneConst& other) = default;

CInstantZoneConst::~CInstantZoneConst() = default;

FVector CInstantZoneConst::GetStartPos(int index) const {
    L2_GUARD_FUNCTION();
    FVector result{};
    const int randomX = static_cast<int>(::genrand64_real2() * 21.0);
    const int randomY = static_cast<int>(::genrand64_real2() * 21.0);

    if (m_eStartPosType == SPT_DESIGNATORY) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_vStartPos.size()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] invalid instantzone start position.. inzone[%d]",
            //     L"InstantZone.cpp",
            //     0x91b,
            //     m_nTypeId)
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] invalid instantzone start position.. inzone[%d]",
            //     L"InstantZone.cpp",
            //     0x91b,
            //     m_nTypeId)
            L2_LOG_ERROR(
                Log, "Invalid start position index %d for instant zone type %d (%zu positions); returning %s.", index,
                m_nTypeId, m_vStartPos.size(), m_vStartPos.empty() ? "zero position" : "first configured position"
            );
            if (!m_vStartPos.empty()) {
                return m_vStartPos.front();
            }
            return result;
        }
        result.x = static_cast<double>(static_cast<int>(runtime::CheckedVectorAt(m_vStartPos, index).x) + randomX);
        result.y = static_cast<double>(static_cast<int>(runtime::CheckedVectorAt(m_vStartPos, index).y) + randomY);
        result.z = static_cast<double>(static_cast<int>(runtime::CheckedVectorAt(m_vStartPos, index).z));
        return result;
    }

    if (m_eStartPosType == SPT_RANDOM && m_vStartPos.size() > 1) {
        const std::size_t selected = static_cast<std::size_t>(::genrand64_real2() * static_cast<double>(m_vStartPos.size()));
        const FVector& source = m_vStartPos.at(selected);
        result.x = static_cast<double>(static_cast<int>(source.x) + randomX);
        result.y = static_cast<double>(static_cast<int>(source.y) + randomY);
        result.z = static_cast<double>(static_cast<int>(source.z));
    }
    return result;
}

FVector CInstantZoneConst::GetEndPos(User* const user, const FVector& origin, const bool disorder) const {
    // Current L2Server 0x007abc5c.
    L2_GUARD_FUNCTION();

    FVector result = origin;
    const EndPositionType type = disorder ? m_eDisorderEndPosType : m_eOrderEndPosType;
    const VectorListType& positions = disorder ? m_vDisorderEndPos : m_vOrderEndPos;

    if (type == EPT_ORIGIN_RESTART) {
        if (user == nullptr || user->m_data == nullptr) {
            return result;
        }
        return Singleton<CSetting>::inst().GetRestartPoint(
            user->m_data->m_Race, user->m_data->m_Pos, user->m_data->m_nKarma < 0, user->m_data->m_nPKCount
        );
    }

    if (type == EPT_RANDOM) {
        const FVector* selected = positions.data();
        if (positions.size() > 1) {
            const std::size_t index = static_cast<std::size_t>(::genrand64_real2() * static_cast<double>(positions.size()));
            selected = positions.data() + index;
        }
        result = *selected;
        return result;
    }

    if (type == EPT_DESIGNATORY) {
        result = *positions.data();
    }
    return result;
}

CInstantZoneGroup::CInstantZoneGroup(const int groupId, const int maxEntrance) :
    m_groupId(groupId),
    m_maxEntrance(maxEntrance),
    m_extraEntrance(0),
    m_padding00c{},
    m_timeLimit(0),
    m_resetBindingTime(0),
    m_resetBindingDay(0),
    m_padding03c{} {}

int CInstantZoneGroup::GetLatesetResetBindingDay(const int weekDay, const int minuteOfDay) {
    L2_GUARD_FUNCTION();
    return ResetBindingDayHelper::FindLatestResetBindingDay(m_resetBindingDay, m_resetBindingTime, weekDay, minuteOfDay);
}

int CInstantZoneGroup::GetNextResetBindingDay(const int weekDay, const int minuteOfDay) {
    L2_GUARD_FUNCTION();
    return ResetBindingDayHelper::FindNextResetBindingDay(m_resetBindingDay, m_resetBindingTime, weekDay, minuteOfDay);
}

int CInstantZoneGroup::GetLatestBindingTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return 0;
    }

    std::tm midnight = local;
    midnight.tm_hour = 0;
    midnight.tm_min = 0;
    midnight.tm_sec = 0;
    std::time_t binding = std::mktime(&midnight);
    if (local.tm_hour * 60 + local.tm_min < m_resetBindingTime) {
        binding -= 24 * 60 * 60;
    }
    binding += static_cast<std::time_t>(m_resetBindingTime) * 60;
    return static_cast<std::int32_t>(binding);
}

InstantZoneBindingLevel CInstantZoneGroup::IsAlreadyBoundUser(User* user, const std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return IZBL_NOT_BINDED;
    }
    const InstantZoneBindingLevel result = [&] {
        L2_GUARD(*user, (scoped_lock_detail::OwnerMethods<&User::EnterInZoneLock, &User::LeaveInZoneLock>{}));
        return _IsAlreadyBoundUser(user, currentTime);
    }();
    return result;
}

InstantZoneBindingLevel CInstantZoneGroup::_IsAlreadyBoundUser(User* user, const std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    std::tm local{};
    if (user == nullptr || !runtime::crt::LocalTime32(currentTime, local)) {
        return IZBL_NOT_BINDED;
    }

    int latestBindingTime = 0;
    int entranceCount = 0;
    int maxEntranceCount = 0;
    if (!user->_GetInZoneRestriction(m_groupId, true, latestBindingTime, entranceCount, maxEntranceCount)) {
        return IZBL_NOT_BINDED;
    }

    if (currentTime < m_timeLimit + latestBindingTime) {
        const int latestResetMinutes = GetLatesetResetBindingDay(local.tm_wday, local.tm_hour * 60 + local.tm_min);
        if (currentTime - latestResetMinutes * 60 < latestBindingTime) {
            if (m_maxEntrance <= entranceCount) {
                if (entranceCount == m_maxEntrance && maxEntranceCount == m_maxEntrance) {
                    return IZBL_BINDED;
                }
                return maxEntranceCount > entranceCount ? IZBL_EXTRA_ENTRANCE : IZBL_ABS_BINDED;
            }
            return IZBL_NOT_BINDED;
        }
    }

    user->_ResetInZoneRestriction(m_groupId, true);
    return IZBL_NOT_BINDED;
}

void CInstantZoneGroup::AddZone(const int typeId) {
    L2_GUARD_FUNCTION();
    CInstantZone* zone = g_InstantZoneDB.GetTypicalInstantZone(typeId);
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid InstantZone(%d) in Group(%d)",
        //     L"InstantZone.cpp",
        //     0x82,
        //     typeId,
        //     m_groupId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid InstantZone(%d) in Group(%d)",
        //     L"InstantZone.cpp",
        //     0x82,
        //     typeId,
        //     m_groupId)
        L2_LOG_ERROR(Log, "Cannot add instant zone type %d to group %d: zone template is missing.", typeId, m_groupId);
        return;
    }

    if (m_groupMap.find(typeId) != m_groupMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated InstantZone(%d) in Group(%d)",
        //     L"InstantZone.cpp",
        //     0x89,
        //     typeId,
        //     m_groupId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated InstantZone(%d) in Group(%d)",
        //     L"InstantZone.cpp",
        //     0x89,
        //     typeId,
        //     m_groupId)
        L2_LOG_ERROR(Log, "Ignoring duplicate instant zone type %d in group %d.", typeId, m_groupId);
        return;
    }

    m_groupMap[typeId] = zone;
    if (zone->m_const->m_instantZoneCluster != nullptr) {
        zone->m_const->m_instantZoneCluster->SetGroup(this);
    } else {
        zone->m_const->m_instantZoneGroup = this;
    }
}

void CInstantZoneGroup::GetInstantZoneGroupTypeIdList(TypeIdSet& typeIds) {
    for (const auto& entry : m_groupMap) {
        typeIds.insert(entry.first);
    }
}

CInstantZoneCluster::CInstantZoneCluster(const int zoneTypeId) :
    m_zoneTypeId(zoneTypeId), m_padding004{} {}

void CInstantZoneCluster::AddZone(const int clusterId, CInstantZone* zone) {
    L2_GUARD_FUNCTION();
    if (m_clusteredMap.find(clusterId) == m_clusteredMap.end()) {
        m_clusteredMap[clusterId] = InstantZoneDensityType(zone, 0);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Duplicated Cluster(%d) in InstantZone(%d).",
    //     __WFILE__,
    //     __LINE__,
    //     clusterId,
    //     zone->GetInstantZoneTypeID())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Duplicated Cluster(%d) in InstantZone(%d).",
    //     __WFILE__,
    //     __LINE__,
    //     clusterId,
    //     zone->GetInstantZoneTypeID())
    L2_LOG_ERROR(Log, "Ignoring duplicate cluster ID %d for instant zone type %d.", clusterId, zone->GetInstantZoneTypeID());
}

void CInstantZoneCluster::SetGroup(CInstantZoneGroup* group) {
    L2_GUARD_FUNCTION();
    for (auto& entry : m_clusteredMap) {
        if (entry.second.first != nullptr && entry.second.first->m_const != nullptr) {
            entry.second.first->m_const->m_instantZoneGroup = group;
        }
    }
}

CInstantZone* CInstantZoneCluster::GetZone(const int clusterId) {
    L2_GUARD_FUNCTION();
    const auto position = m_clusteredMap.find(clusterId);
    return position == m_clusteredMap.end() ? nullptr : position->second.first;
}

CInstantZone* CInstantZoneCluster::GetLowestDensityZone() {
    L2_GUARD_FUNCTION();
    CInstantZone* result = nullptr;
    int lowestDensity = std::numeric_limits<int>::max();
    for (const auto& entry : m_clusteredMap) {
        if (entry.second.second < lowestDensity) {
            lowestDensity = entry.second.second;
            result = entry.second.first;
        }
    }
    return result;
}

void CInstantZoneCluster::AddDensity(const int clusterId, const int density) {
    L2_GUARD_FUNCTION();
    const auto position = m_clusteredMap.find(clusterId);
    if (position != m_clusteredMap.end()) {
        position->second.second += density;
    }
}

CInstantZoneDB::CInstantZoneDB() {}

CInstantZoneDB::~CInstantZoneDB() {
    L2_GUARD_FUNCTION();
    m_mapInstantZones.clear();
    m_mapInstantZoneStrMap.clear();
    m_InstantZoneToDoListMap.clear();
}

void CInstantZoneDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\instantzonedata.txt";
#else
    constexpr const wchar_t* kDataPath = L"../script/instantzonedata.txt";
#endif
    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"InstantZone.cpp",
        //     0x1ab,
        //     kDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"InstantZone.cpp",
        //     0x1ab,
        //     kDataPath)
        L2_LOG_ERROR(Log, "Cannot read instant zone data from %s.", WideToUtf8(kDataPath).c_str());
        return;
    }

    CParserForInstantZone parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForInstantZone::create failed",
        //     L"InstantZone.cpp",
        //     0x1b3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForInstantZone::create failed",
        //     L"InstantZone.cpp",
        //     0x1b3)
        L2_LOG_ERROR(Log, "Cannot create parser for instant zone data %s.", WideToUtf8(kDataPath).c_str());
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing InstantZone Data file failed "
        //     L"after line[%d]",
        //     L"InstantZone.cpp",
        //     0x1be,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing InstantZone Data file failed "
        //     L"after line[%d]",
        //     L"InstantZone.cpp",
        //     0x1be,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse instant zone data %s after line %d.", WideToUtf8(kDataPath).c_str(), parser.m_lexer.line
        );
        return;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L" === Max InstantZone : %d ===",
    //     parser.m_nMaximumZoneCount)
    // L2_LOG_INFO(Log,
    //     L" === Max InstantZone : %d ===",
    //     parser.m_nMaximumZoneCount)
    L2_LOG_INFO(Log, "Instant zone data parsed; initializing container capacity %d.", parser.m_nMaximumZoneCount);
    CContainerDB::Inst().Init(parser.m_nMaximumZoneCount);
}

void CInstantZoneDB::PostLoad() {
    L2_GUARD_FUNCTION();

    for (const auto& zoneEntry : m_mapInstantZones) {
        CInstantZone* const zone = zoneEntry.second;
        if (zone == nullptr || zone->m_const == nullptr) {
            continue;
        }

        for (char16_t* const collisionName : zone->m_const->m_vDoorNameList) {
            if (collisionName == nullptr) {
                continue;
            }

            const ::std::u16string name(collisionName);
            CWorldPlaneCollision::CollisionVector collisions;
            g_WorldPlaneCollision.GetPlaneCollisionVector(name, collisions);

            CInstantZone::CollisionCheckMap collisionChecks;
            for (CPlaneCollision* const collision : collisions) {
                collisionChecks.emplace(collision, true);
            }
            zone->m_mapNameToCollisionMap.emplace(name, std::move(collisionChecks));
        }

        int areaIndex = 0;
        for (char16_t* const areaName : zone->m_const->m_vAreaNameList) {
            if (areaName != nullptr) {
                CArea* const area = CAreaDB::inst().FindArea(areaName);
                if (area == nullptr) {
                    const std::string hostName = Utf16ToUtf8(areaName);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"invalid area[%s] for instant zone[%d]",
                    //     hostName.c_str(),
                    //     zoneEntry.first)
                    // L2_LOG_ERROR(Log,
                    //     L"invalid area[%s] for instant zone[%d]",
                    //     hostName.c_str(),
                    //     zoneEntry.first)
                    L2_LOG_ERROR(
                        Log, "Instant zone type %d references missing area %s; area registration skipped.", zoneEntry.first,
                        hostName.c_str()
                    );
                } else {
                    zone->m_mapAreaOnOffFlag.emplace(area, area->IsOn(0));
                    zone->m_mapAreaIndex.emplace(area, areaIndex);
                }
            }
            ++areaIndex;
        }
    }
}

CInstantZoneCluster* CInstantZoneDB::GetCluster(const int zoneTypeId) {
    L2_GUARD_FUNCTION();
    if (zoneTypeId < 1) {
        return nullptr;
    }
    const auto position = m_mapInstantZoneCluster.find(zoneTypeId);
    return position == m_mapInstantZoneCluster.end() ? nullptr : position->second;
}

CInstantZoneGroup* CInstantZoneDB::GetInstantZoneGroup(const int groupId) {
    L2_GUARD_FUNCTION();
    if (groupId < 1) {
        return nullptr;
    }
    const auto position = m_mapInstantZoneGroup.find(groupId);
    if (position != m_mapInstantZoneGroup.end()) {
        return position->second;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid InZoneGroup [%d]",
    //     L"InstantZone.cpp",
    //     0x2a1,
    //     groupId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid InZoneGroup [%d]",
    //     L"InstantZone.cpp",
    //     0x2a1,
    //     groupId)
    L2_LOG_ERROR(Log, "Instant zone group %d is not registered.", groupId);
    return nullptr;
}

CInstantZone* CInstantZoneDB::GetTypicalInstantZone(const int typeId) {
    L2_GUARD_FUNCTION();
    if (typeId < 1) {
        return nullptr;
    }
    const auto position = m_mapInstantZones.find(typeId);
    if (position != m_mapInstantZones.end()) {
        return position->second;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid InZoneType [%d]",
    //     L"InstantZone.cpp",
    //     0x2b6,
    //     typeId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid InZoneType [%d]",
    //     L"InstantZone.cpp",
    //     0x2b6,
    //     typeId)
    L2_LOG_ERROR(Log, "Instant zone template type %d is not registered.", typeId);
    return nullptr;
}

int CInstantZoneDB::GetInstantZoneTypeId(const wchar_t* name) {
    L2_GUARD_FUNCTION();
    if (name == nullptr) {
        return 0;
    }
    const auto position = m_mapInstantZoneStrMap.find(WideToUtf16(name));
    return position == m_mapInstantZoneStrMap.end() ? 0 : position->second;
}

CInstantZone* CInstantZoneDB::GetInstantZone(const wchar_t* name) { return GetInstantZone(GetInstantZoneTypeId(name)); }

CInstantZone* CInstantZoneDB::GetInstantZone(const int typeId) {
    L2_GUARD_FUNCTION();
    CInstantZone* zone = GetTypicalInstantZone(typeId);
    if (zone != nullptr && zone->m_const != nullptr && zone->m_const->m_instantZoneCluster != nullptr) {
        zone = zone->m_const->m_instantZoneCluster->GetLowestDensityZone();
    }
    return zone;
}

CInstantZone* CInstantZoneDB::GetInstantZone(const int typeId, const int clusterId) {
    L2_GUARD_FUNCTION();
    CInstantZone* zone = GetTypicalInstantZone(typeId);
    if (zone == nullptr || zone->m_const == nullptr) {
        return nullptr;
    }
    if (zone->m_const->m_clusterId == clusterId) {
        return zone;
    }
    return zone->m_const->m_instantZoneCluster == nullptr ? nullptr
                                                          : zone->m_const->m_instantZoneCluster->GetZone(clusterId);
}

CInstantZone* CInstantZoneDB::GetPledgeRaidInstantZone() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapInstantZones) {
        CInstantZone* zone = entry.second;
        if (zone != nullptr && zone->m_const != nullptr && zone->m_const->m_InZoneType == IZT_PLEDGE_RAID) {
            return zone;
        }
    }
    return nullptr;
}

void CInstantZoneDB::SendToToList(User* user, bool entering) {
    // Current L2Server 0x007b12ec.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    short count = 0;
    std::array<char, 0x5000> buffer{};
    unsigned int length = 0;
    ToDoListInstantZoneInfo info;
    for (const auto& todo : m_InstantZoneToDoListMap) {
        info.m_ToDoListType = E_TODOLIST_TYPE_INSTANTZONE;
        info.m_ToDoListID = todo.first;

        const auto found = m_mapInstantZones.find(todo.second);
        if (found == m_mapInstantZones.end()) {
            continue;
        }

        CInstantZone* const zone = found->second;
        InstantZoneEnterFailInfo failInfo;
        const InZoneEnterReasonType enterReason = zone->CheckEnterConditionCommon(user, failInfo, 0);
        CInstantZoneConst* const zoneConst = zone->m_const;
        if (!entering && (zoneConst->m_LevelMin != 0 || zoneConst->m_LevelMax != 0) &&
            (user->m_data->m_nLevel < zoneConst->m_LevelMin || zoneConst->m_LevelMax < user->m_data->m_nLevel)) {
            continue;
        }

        g_HTMLCache.GetToDoListNameFromHTML(
            info.m_Name, info.m_ToDoListType, info.m_ToDoListID, user->m_data->m_CountryCode
        );
        info.m_LevelMin = static_cast<std::int16_t>(zoneConst->m_LevelMin);
        info.m_LevelMax = static_cast<std::int16_t>(zoneConst->m_LevelMax);
        info.m_UserCountMin = static_cast<std::int16_t>(zoneConst->m_nMinUser);
        info.m_UserCountMax = static_cast<std::int16_t>(zoneConst->m_nMaxUser);
        info.m_Priority = zoneConst->m_ToDayPriority;
        info.m_EnterType = E_TODOLIST_ENTER_TYPE_POSSIBLE;
        if (enterReason == IZERT_FAILED) {
            info.m_EnterType = E_TODOLIST_ENTER_TYPE_IMPOSSIBLE;
        } else if (zoneConst->m_vEntranceCond.empty()) {
            info.m_EnterType = E_TODOLIST_ENTER_TYPE_ALLTIME;
        }
        info.AssembleToDoInfo(buffer.data(), static_cast<int>(buffer.size()), length, count);
    }

    if (g_bUseOneDayReward) {
        user->GetSocket()->Send("chhb", 0xfe, 0x1ab, static_cast<int>(count), length, buffer.data());
    }
}

bool CInstantZoneDB::CheckAreaEnterInzone(const int typeId) {
    L2_GUARD_FUNCTION();
    CInstantZone* zone = GetTypicalInstantZone(typeId);
    return zone != nullptr && zone->m_const != nullptr && zone->m_const->CheckEndPosType(EPT_DESIGNATORY, false) &&
        zone->m_const->CheckEndPosType(EPT_DESIGNATORY, true);
}

void CInstantZoneDB::AddCluster(CInstantZoneCluster* cluster) {
    L2_GUARD_FUNCTION();
    if (cluster == nullptr) {
        return;
    }
    const int typeId = cluster->m_zoneTypeId;
    if (m_mapInstantZoneCluster.find(typeId) == m_mapInstantZoneCluster.end()) {
        m_mapInstantZoneCluster[typeId] = cluster;
    }
}

void CInstantZoneDB::AddGroup(CInstantZoneGroup* group) {
    L2_GUARD_FUNCTION();
    const int groupId = group->m_groupId;
    if (m_mapInstantZoneGroup.find(groupId) == m_mapInstantZoneGroup.end()) {
        m_mapInstantZoneGroup[groupId] = group;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Duplicated InstantZoneGroup (%d)",
    //     __WFILE__,
    //     __LINE__,
    //     groupId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Duplicated InstantZoneGroup (%d)",
    //     __WFILE__,
    //     __LINE__,
    //     groupId)
    L2_LOG_ERROR(Log, "Ignoring duplicate instant zone group %d; existing registration retained.", groupId);
}

void CInstantZoneDB::AddZone(CInstantZone* zone, const wchar_t* name) {
    L2_GUARD_FUNCTION();

    const int typeId = zone->m_nTypeId;
    const auto existing = m_mapInstantZones.find(typeId);
    if (existing != m_mapInstantZones.end()) {
        CInstantZoneConst* const zoneConst = zone->m_const;
        if (zoneConst->m_instantZoneCluster != nullptr) {
            if (zoneConst->m_MaxParty != 0) {
                g_InstantZoneMgr.AddInzonePartyInfo(typeId, zoneConst->m_clusterId);
            }
            return;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated InstantZone (%d)",
        //     L"InstantZone.cpp",
        //     0x214,
        //     typeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated InstantZone (%d)",
        //     L"InstantZone.cpp",
        //     0x214,
        //     typeId)
        L2_LOG_ERROR(
            Log, "Duplicate instant zone type %d; registration continues for cluster ID %d.", typeId,
            zone->m_const->m_clusterId
        );
    }

    if (zone->m_const->m_clusterId < 2) {
        m_mapInstantZones[typeId] = zone;
        {
            const std::u16string key = WideToUtf16(std::wstring_view(name));
            m_mapInstantZoneStrMap[key] = typeId;
        }
        if (zone->m_const->m_bToDoListCheck) {
            const std::u16string key = WideToUtf16(std::wstring_view(name));
            m_InstantZoneToDoListMap[key] = typeId;
        }
        if (zone->m_const->m_MaxParty != 0) {
            g_InstantZoneMgr.AddInzonePartyInfo(typeId, zone->m_const->m_clusterId);
        }
    }
}

void CInstantZoneDB::GetInstantZoneGroupTypeIdList(const int groupId, TypeIdSet& typeIds) {
    CInstantZoneGroup* group = GetInstantZoneGroup(groupId);
    if (group != nullptr) {
        group->GetInstantZoneGroupTypeIdList(typeIds);
    }
}

void CInstantZoneDB::ReportInstantZoneStats() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapInstantZones) {
        const CInstantZone* const zone = entry.second;
        const int currentZoneCount = zone->m_nCurrentZoneCount;
        if (currentZoneCount > 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[INZONE_COUNT] Type[%d] Count[%d]",
            //     zone->m_nTypeId,
            //     currentZoneCount)
            // L2_LOG_ERROR(Log,
            //     L"[INZONE_COUNT] Type[%d] Count[%d]",
            //     zone->m_nTypeId,
            //     currentZoneCount)
            L2_LOG_INFO(Log, "Instant zone type %d has %d active instances.", zone->m_nTypeId, currentZoneCount);
        }
    }
}

void CInstantZoneDB::ReportInstantZoneStats(void*) { ReportInstantZoneStats(); }

CInstantZoneManager::CInstantZoneManager() {
    // The current constructor leaves +0x88 untouched. The
    // process-global instance is zero-initialized before this constructor.
}

CInstantZoneManager::~CInstantZoneManager() = default;

void CInstantZoneManager::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    DeleteInzoneAgit();
    GetRimRank();
}

void CInstantZoneManager::LoadInzoneMaxDBID() {
    L2_GUARD_FUNCTION();
    db.RequestLoadInzoneMaxDBID();
}

void CInstantZoneManager::LoadInzoneFlag(const int userDbId, const int zoneTypeId) {
    L2_GUARD_FUNCTION();
    if (m_InzoneFlagMap.find(InzoneFlagKey{userDbId, zoneTypeId}) == m_InzoneFlagMap.end()) {
        db.RequestLoadInstantZoneFlag(userDbId, zoneTypeId);
    }
}

void CInstantZoneManager::InitInZoneFlag(const int userDbId, int& count, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto position = m_InzoneFlagMap.find(InzoneFlagKey{userDbId, 0});
    if (position == m_InzoneFlagMap.end()) {
        return;
    }
    for (const auto& entry : position->second) {
        packet.Assemble("dd", entry.first, entry.second);
        ++count;
    }
}

void CInstantZoneManager::BuilderViewFlag(const int userDbId, User* const builder) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (builder == nullptr) {
        return;
    }

    // Current L2Server 0x007add19..0x007add4f performs lower_bound for
    // {userDbId, 0} and does not add an equality check before
    // traversing the selected entry's flag map.
    const auto position = m_InzoneFlagMap.lower_bound(InzoneFlagKey{userDbId, 0});
    if (position == m_InzoneFlagMap.end()) {
        return;
    }

    for (const auto& flag : position->second) {
        char16_t message[512]{};
        FormatUtf16To(message, std::size(message), u"[index] %d [value] %d", flag.first, flag.second);
        builder->GetSocket()->SendSystemMessage(u"SYS", message);
    }
}

void CInstantZoneManager::SetInzoneDBIndex(CInstantZone* zone) {
    L2_GUARD_FUNCTION();
    if (zone != nullptr) {
        zone->m_nInzoneDBId = runtime::raw_atomic::IncrementI32(&m_nMaxInzoneDBID);
    }
}

bool CInstantZoneManager::BeInstantZoneCreate(User* user) {
    L2_GUARD_FUNCTION();
    return user != nullptr && user->m_nAssociatedInstantZoneId == 0;
}

void CInstantZoneManager::FinishInstantZone(CInstantZone* zone) {
    L2_GUARD_FUNCTION();
    if (zone != nullptr) {
        zone->m_eStatus = IZS_CONTENT_FINISH;
    }
}

void CInstantZoneManager::AddExtraDuration(CInstantZone* zone, const int duration) {
    L2_GUARD_FUNCTION();
    if (zone != nullptr) {
        zone->m_extraDuration += duration;
    }
}

void CInstantZoneManager::GetRimRank() {
    L2_GUARD_FUNCTION();
    CInstantZoneGroup* group = g_InstantZoneDB.GetInstantZoneGroup(3);
    if (group != nullptr) {
        db.GetRimPointTop10(group->GetLatestBindingTime());
    }
    AddTimer(60000, 0);
}

void CInstantZoneManager::DeleteInstantZone(CInstantZone& zone, const InstantZoneDeleteInfo info) {
    L2_GUARD_FUNCTION();
    zone.Delete(info);
    if (zone.m_const != nullptr && zone.m_const->m_instantZoneCluster != nullptr) {
        zone.m_const->m_instantZoneCluster->AddDensity(zone.m_const->m_clusterId, -1);
    }
}

void CInstantZoneManager::SendErrMessageForGettingZone(std::vector<User*> users, const CreateFailReason reason) {
    L2_GUARD_FUNCTION();
    for (User* const user : users) {
        if (user != nullptr &&
            (reason == CFR_LIMIT_ZONE_COUNT || reason == CFR_LIMIT_MULTIFUL_ZONE || reason == CFR_NPC_SERVER_DOWN ||
                reason == CFR_LIMIT_DESTROYED_ZONE)) {
            user->SendSystemMessage(static_cast<unsigned int>(reason));
        }
    }
}

void CInstantZoneManager::SendErrMessageForEnteringZone(std::vector<User*> users, InstantZoneEnterFailInfo& info) {
    L2_GUARD_FUNCTION();
    const int reason = static_cast<int>(info.reason);
    for (User* const user : users) {
        if (user == nullptr) {
            continue;
        }

        bool sendPlain = false;
        bool sendCharacter = false;
        if (reason < 0x898) {
            if (reason == EFR_MAKER_NOT_LOADED) {
                sendPlain = true;
            } else if (reason > 0x82f) {
                if (reason < 0x835) {
                    sendCharacter = true;
                } else if (reason < 0x838 || reason == EFR_LIMIT_MULTIFUL_ZONE || reason == EFR_NOT_PARTY_MASTER) {
                    sendPlain = true;
                }
            }
        } else if (reason == EFR_LIMIT_PCCAFE_POINT) {
            sendCharacter = true;
        } else if (reason > 0xacb) {
            if (reason < 0xace) {
                sendPlain = true;
            } else if (reason == EFR_LIMIT_MIN_MEMBER_COUNT) {
                user->SendSystemMessage_d(static_cast<unsigned int>(reason), info.intParam);
                continue;
            } else if (reason == EFR_PARTY_CHAIR_FAIL) {
                sendCharacter = true;
            }
        }

        if (sendPlain) {
            user->SendSystemMessage(static_cast<unsigned int>(reason));
        } else if (sendCharacter) {
            user->GetSocket()->SendSystemMessage_Char(
                static_cast<unsigned int>(reason), SanitizeUtf16(info.efUser).c_str()
            );
        }
    }
}

void CInstantZoneManager::DeleteInzoneAgit() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const int zoneSid : m_nDeleteInzoneList) {
        CGeneralObject* object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(zoneSid));
        CInstantZone* zone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (zone != nullptr) {
            DeleteInstantZone(*zone, IZDI_CONTENTS_FINISHED);
        }
    }
    m_nDeleteInzoneList.clear();
}

CInstantZone* CInstantZoneManager::GetInstantZoneTemplate(const int typeId) {
    L2_GUARD_FUNCTION();
    return g_InstantZoneDB.GetInstantZone(typeId);
}

bool CInstantZoneManager::SetMaxZoneCount(const int typeId, const int maxZoneCount) {
    L2_GUARD_FUNCTION();
    CInstantZone* zone = GetInstantZoneTemplate(typeId);
    const bool changed = zone->m_nCurrentZoneCount < maxZoneCount;
    if (changed) {
        zone->MAX_ZONE_COUNT = maxZoneCount;
    }
    return changed;
}

CInstantZone* CInstantZoneManager::CreateInstantZone(const int typeId, CreateFailReason& reason) {
    L2_GUARD_FUNCTION();
    CInstantZone* const templateZone = GetInstantZoneTemplate(typeId);
    if (templateZone == nullptr || !templateZone->CanBeCreated(reason)) {
        return nullptr;
    }

    CInstantZone* const zone = static_cast<CInstantZone*>(templateZone->Copy());
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to copy instant zone[%d]",
        //     L"InstantZone.cpp",
        //     0x400,
        //     typeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to copy instant zone[%d]",
        //     L"InstantZone.cpp",
        //     0x400,
        //     typeId)
        L2_LOG_ERROR(Log, "Cannot create instant zone type %d: copying the template failed.", typeId);
        reason = CFR_INTERNAL_ERROR;
        return nullptr;
    }
    if (zone->m_pContainer != nullptr) {
        return zone;
    }

    zone->Delete(IZDI_INSUFFICIENT_CONTAINER);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] insufficient instant container [%d]",
    //     L"InstantZone.cpp",
    //     0x408,
    //     typeId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] insufficient instant container [%d]",
    //     L"InstantZone.cpp",
    //     0x408,
    //     typeId)
    L2_LOG_ERROR(
        Log, "Cannot create instant zone type %d: no private container was allocated; zone deletion requested.", typeId
    );
    reason = CFR_LIMIT_ZONE_COUNT;
    return nullptr;
}

CInstantZone* CInstantZoneManager::CreateInstantZone(wchar_t* const name, CreateFailReason& reason) {
    L2_GUARD_FUNCTION();
    return CreateInstantZone(g_InstantZoneDB.GetInstantZoneTypeId(name), reason);
}

void CInstantZoneManager::AddInzonePartyInfo(const int typeId, const int clusterId) {
    L2_GUARD_FUNCTION();
    const InstantZoneKey key{typeId, clusterId};
    if (m_PartyMap.find(key) == m_PartyMap.end()) {
        m_PartyMap[key] = 0;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] duplicated inzone type[%d]",
    //     __WFILE__,
    //     __LINE__,
    //     typeId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] duplicated inzone type[%d]",
    //     __WFILE__,
    //     __LINE__,
    //     typeId)
    L2_LOG_ERROR(Log, "Ignoring duplicate party tracking entry for instant zone type %d, cluster %d.", typeId, clusterId);
}

void CInstantZoneManager::SetDeleteInzoneAgit(const int zoneSid) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_nDeleteInzoneList.push_back(zoneSid);
}

CInstantZone* CInstantZoneManager::GetInstantZoneToEnter(
    User* const user, const InZoneEnterType enterType, const int typeId, CreateFailReason& reason
) {
    L2_GUARD_FUNCTION();
    if (!BeInstantZoneCreate(user)) {
        CGeneralObject* const object =
            CGeneralObject::GetObjectW(static_cast<std::uint32_t>(user->m_nAssociatedInstantZoneId));
        CInstantZone* const associated = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (associated == nullptr) {
            user->SetAssociatedInstantZoneId(0, 0, 0, true);
            reason = CFR_LIMIT_DESTROYED_ZONE;
            return nullptr;
        }

        if (associated->IsAssociatedUser(user) && associated->m_nTypeId == typeId) {
            return associated;
        }
        if (!associated->IsAssociatedUser(user)) {
            user->SetAssociatedInstantZoneId(0, 0, 0, true);
        }
        reason = CFR_LIMIT_MULTIFUL_ZONE;
        return nullptr;
    }

    CInstantZone* const zone = CreateInstantZone(typeId, reason);
    if (zone != nullptr) {
        InstantZoneLogHelper::Log(zone, user, 0x1f6e, enterType, 0, 0);
        runtime::raw_atomic::IncrementI32(&user->m_nCreateInstantZoneCount);
    }
    return zone;
}

bool CInstantZoneManager::ChangeInstantZone(
    User* const user, CInstantZone* const targetZone, const InZoneEnterType enterType, const int reason, const bool force
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || targetZone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User* pUser || CInstantZone* pTargetInZone] "
        //     L"is nullptr",
        //     L"InstantZone.cpp",
        //     0x539)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User* pUser || CInstantZone* pTargetInZone] "
        //     L"is nullptr",
        //     L"InstantZone.cpp",
        //     0x539)
        L2_LOG_ERROR(
            Log, "Cannot change instant zone: user present=%d, target zone present=%d.", static_cast<int>(user != nullptr),
            static_cast<int>(targetZone != nullptr)
        );
        return false;
    }

    CInstantZone* const origin = user->GetInstantZone();
    if (origin == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CInstantZone* pOriginInZone = "
        //     L"pUser->GetInstantZone()] is nullptr",
        //     L"InstantZone.cpp",
        //     0x540)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CInstantZone* pOriginInZone = "
        //     L"pUser->GetInstantZone()] is nullptr",
        //     L"InstantZone.cpp",
        //     0x540)
        // L2_LOG_ERROR(Log,
        //     "Cannot change instant zone for %s: origin zone is missing.",
        //     WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Cannot change instant zone for %s: origin zone is missing.", Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }

    FVector returnPosition{};
    if (!origin->GetRetPos(*user, true, returnPosition)) {
        return false;
    }

    InstantZoneEnterFailInfo failInfo{};
    if (!force &&
        targetZone->CheckEnterConditionCommon(user, failInfo, user->m_nAssociatedInstantZoneDBId) != IZERT_NORMAL) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CheckEnterConditionCommon Fail UserName: %s bForce: %d "
        //     L"reason: %d",
        //     user->GetName(),
        //     0,
        //     static_cast<int>(failInfo.reason))
        // L2_LOG_ERROR(Log,
        //     L"CheckEnterConditionCommon Fail UserName: %s bForce: %d "
        //     L"reason: %d",
        //     user->GetName(),
        //     0,
        //     static_cast<int>(failInfo.reason))
        // L2_LOG_WARN(Log,
        //     "Cannot change instant zone for %s: entry conditions rejected the request for type %d (reason %d).",
        //     WideToUtf8(user->GetName()).c_str(), targetZone->m_nTypeId, static_cast<int>(failInfo.reason))
        L2_LOG_WARN(
            Log, "Cannot change instant zone for %s: entry conditions rejected the request for type %d (reason %d).",
            Utf16ToUtf8(user->GetName()).c_str(), targetZone->m_nTypeId, static_cast<int>(failInfo.reason)
        );
        return false;
    }

    const FVector destination = targetZone->m_const->GetStartPos(reason);
    if (!targetZone->EnterZone(
            user, returnPosition, destination, enterType, IZERT_NORMAL, TeleportSourceType::InstantAgit
        )) {
        return false;
    }
    if (targetZone->m_const->m_instantZoneCluster != nullptr) {
        targetZone->m_const->m_instantZoneCluster->AddDensity(targetZone->m_const->m_clusterId, 1);
    }
    return true;
}

void CInstantZoneManager::LeaveInstantZone(
    User* const user, CInstantZone* const zone, const int x, const int y, const int z
) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && zone != nullptr) {
        zone->LeaveZone(user, false, x, y, z, 0);
    }
}

void CInstantZoneManager::SetInstantZoneBinding(CInstantZone* const zone, const InZoneBindingType bindingType) {
    L2_GUARD_FUNCTION();
    if (zone == nullptr || zone->m_const == nullptr || zone->m_const->m_nTimeLimit == 0) {
        return;
    }
    if (zone->m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        zone->SetBindingAllUserInPledge();
    } else {
        zone->SetBindingAllUser(bindingType);
    }
}

bool CInstantZoneManager::EnterInstantZone(
    User* const user,
    CInstantZone* const zone,
    const InZoneEnterType enterType,
    const int reason,
    const bool force,
    const int bindingId,
    const TeleportSourceType::Enum source
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || zone == nullptr) {
        return false;
    }
    if (user->GetInZoneID() != 0) {
        return ChangeInstantZone(user, zone, enterType, reason, force);
    }

    InstantZoneEnterFailInfo failInfo{};
    if (!force && zone->CheckEnterConditionCommon(user, failInfo, bindingId) != IZERT_NORMAL) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CheckEnterConditionCommon Fail UserName: %s bForce: %d "
        //     L"reason: %d",
        //     user->GetName(),
        //     0,
        //     static_cast<int>(failInfo.reason))
        // L2_LOG_ERROR(Log,
        //     L"CheckEnterConditionCommon Fail UserName: %s bForce: %d "
        //     L"reason: %d",
        //     user->GetName(),
        //     0,
        //     static_cast<int>(failInfo.reason))
        // L2_LOG_WARN(Log,
        //     "Cannot enter instant zone for %s: entry conditions rejected the request for type %d (reason %d).",
        //     WideToUtf8(user->GetName()).c_str(), zone->m_nTypeId, static_cast<int>(failInfo.reason))
        L2_LOG_WARN(
            Log, "Cannot enter instant zone for %s: entry conditions rejected the request for type %d (reason %d).",
            Utf16ToUtf8(user->GetName()).c_str(), zone->m_nTypeId, static_cast<int>(failInfo.reason)
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"EnterInstantZone Fail UserName: %s bForce: %d",
        //     user->GetName(),
        //     0)
        // Noise: the preceding entry-condition warning already identifies this failure.
        // L2_LOG_ERROR(Log,
        //     L"EnterInstantZone Fail UserName: %s bForce: %d",
        //     user->GetName(),
        //     0)
        return false;
    }

    const FVector destination = zone->m_const->GetStartPos(reason);
    const FVector sourcePosition = user->GetPos();
    if (!zone->EnterZone(user, sourcePosition, destination, enterType, IZERT_NORMAL, source)) {
        return false;
    }
    if (zone->m_const->m_instantZoneCluster != nullptr) {
        zone->m_const->m_instantZoneCluster->AddDensity(zone->m_const->m_clusterId, 1);
    }
    return true;
}

bool CInstantZoneManager::EnterInstantZone(
    User* const user, const InZoneEnterType enterType, const int typeId, int& result, const int, const bool force
) {
    L2_GUARD_FUNCTION();
    CreateFailReason createReason = CFR_INTERNAL_ERROR;
    if (g_nNPCCount >= 90001) {
        std::vector<User*> members;
        GetRelatedMember(user, enterType, members);
        SendErrMessageForGettingZone(std::move(members), CFR_INTERNAL_ERROR);
        return false;
    }

    CInstantZone* const zone = GetInstantZoneToEnter(user, enterType, typeId, createReason);
    if (zone == nullptr) {
        std::vector<User*> members;
        GetRelatedMember(user, enterType, members);
        SendErrMessageForGettingZone(std::move(members), createReason);
        return false;
    }

    SetInzoneDBIndex(zone);
    db.RequestCreateInstantZone(static_cast<int>(zone->Id()), typeId, zone->m_nInzoneDBId, runtime::crt::Time32Now());
    SetValidFlagList(user, zone);

    InstantZoneEnterFailInfo failInfo{};
    if (zone->EnterZoneWithCheck(user, enterType, failInfo, force)) {
        if (zone->m_const->m_instantZoneCluster != nullptr) {
            result = zone->m_const->m_clusterId;
            zone->m_const->m_instantZoneCluster->AddDensity(result, 1);
        }
        return true;
    }

    if (user != nullptr && user->m_nAssociatedInstantZoneId == 0) {
        runtime::raw_atomic::AddI32(&user->m_nCreateInstantZoneCount, -1);
        DeleteInstantZone(*zone, IZDI_ENTER_FAILED);
    }
    std::vector<User*> members;
    GetRelatedMember(user, enterType, members);
    SendErrMessageForEnteringZone(std::move(members), failInfo);
    return false;
}

void CInstantZoneNpcAIEventHandler::Receive(const unsigned char* const packet) {
    // Current L2Server 0x007bf32c.
    L2_GUARD_FUNCTION();

    std::uint32_t eventId = 0;
    std::int32_t instantZoneTypeId = 0;
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t instantZoneSidOrEnterType = 0;
    std::int32_t argument1 = 0;
    std::int32_t argument2 = 0;
    std::int32_t argument3 = 0;
    std::int32_t argument4 = 0;
    const unsigned char* const cursor = ::Disassemble(
        packet, "ddddddd", &eventId, &instantZoneTypeId, &npcSid, &userSid, &instantZoneSidOrEnterType, &argument1,
        &argument2
    );
    if (eventId > 9) {
        return;
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CNPC* npc = nullptr;
    if (npcObject != nullptr && npcObject->IsNPC()) {
        npc = npcObject->CastNPC();
    }
    if (npc == nullptr) {
        return;
    }

    unsigned char userPacket[0x5000];
    userPacket[0] = 0;
    std::memset(userPacket + 1, 0, 0x4fff);

    switch (eventId) {
        case 0: {
            CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
            User* user = nullptr;
            if (userObject != nullptr && userObject->IsPC()) {
                user = userObject->CastUser();
            }
            if (user == nullptr || instantZoneSidOrEnterType < IZET_INDIVIDUAL || instantZoneSidOrEnterType >= IZET_MAX) {
                return;
            }

            const bool associated = user->m_nAssociatedInstantZoneId != 0;
            const InZoneEnterType enterType =
                associated ? IZET_INDIVIDUAL : static_cast<InZoneEnterType>(instantZoneSidOrEnterType);
            int result = 0;
            const bool entered = g_InstantZoneMgr.EnterInstantZone(user, enterType, instantZoneTypeId, result, 0, false);
            g_npcserver.Send(
                "cdddddddd", 0x6b, 0, instantZoneTypeId, result, static_cast<int>(npc->Id()), static_cast<int>(user->Id()),
                entered, associated, 0
            );
            return;
        }
        case 1: {
            CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
            User* user = nullptr;
            if (userObject != nullptr && userObject->IsPC()) {
                user = userObject->CastUser();
            }
            if (user == nullptr) {
                return;
            }

            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone == nullptr) {
                return;
            }

            ::Disassemble(cursor, "dd", &argument3, &argument4);
            g_InstantZoneMgr.LeaveInstantZone(user, zone, argument2, argument3, argument4);
            return;
        }
        case 2: {
            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone != nullptr) {
                zone->m_Expiration = argument1;
                g_InstantZoneMgr.FinishInstantZone(zone);
            }
            return;
        }
        case 3: {
            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone != nullptr) {
                g_InstantZoneMgr.SetInstantZoneBinding(zone, IZBT_CONTENTS);
            }
            return;
        }
        case 4: {
            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone != nullptr) {
                g_InstantZoneMgr.AddExtraDuration(zone, argument1);
            }
            return;
        }
        case 5: {
            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone != nullptr) {
                const int timeLeft = zone->m_extraDuration - zone->m_nTickCount + zone->m_const->m_nDuration;
                g_npcserver.Send("cddddddd", 0x6b, 1, instantZoneTypeId, 0, static_cast<int>(npc->Id()), -1, timeLeft, 0);
            }
            return;
        }
        case 6: {
            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone != nullptr) {
                zone->SetInzoneFlag(argument1, argument2);
            }
            return;
        }
        case 7: {
            CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSidOrEnterType));
            CInstantZone* zone = nullptr;
            if (object != nullptr && object->IsInZone()) {
                zone = object->CastInZone();
            }
            if (zone == nullptr) {
                return;
            }

            std::vector<User*> users;
            zone->GetAllActiveUser(users);
            int packetLength = 0;
            std::uint32_t userCount = 0;
            for (User* const user : users) {
                const int assembled = ::Assemble(
                    reinterpret_cast<char*>(userPacket) + packetLength, 0x5000 - packetLength, "d", user->GetServerId()
                );
                if (assembled > 0) {
                    packetLength += assembled;
                }
                ++userCount;
            }
            if (userCount == 0) {
                g_npcserver.Send("cddddddd", 0x6b, 2, instantZoneTypeId, 0, static_cast<int>(npc->Id()), -1, 0, 0);
                return;
            }
            g_npcserver.Send(
                "cdddddddb", 0x6b, 2, instantZoneTypeId, 0, static_cast<int>(npc->Id()), -1, 0, userCount, packetLength,
                userPacket
            );
            return;
        }
        case 8: {
            CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
            User* user = nullptr;
            if (userObject != nullptr && userObject->IsPC()) {
                user = userObject->CastUser();
            }
            if (user == nullptr) {
                return;
            }
            CInstantZone* const zone = user->GetInstantZone();
            if (zone != nullptr) {
                zone->m_Expiration = argument1;
                g_InstantZoneMgr.FinishInstantZone(zone);
            }
            return;
        }
        case 9: {
            CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
            User* user = nullptr;
            if (userObject != nullptr && userObject->IsPC()) {
                user = userObject->CastUser();
            }
            if (user == nullptr) {
                return;
            }
            CInstantZone* const zone = user->GetInstantZone();
            if (zone == nullptr) {
                return;
            }
            ::Disassemble(cursor, "dd", &argument3, &argument4);
            g_InstantZoneMgr.LeaveInstantZone(user, zone, argument2, argument3, argument4);
            return;
        }
        default:
            return;
    }
}

void CInstantZoneManager::GetRelatedMember(User* const user, const InZoneEnterType enterType, std::vector<User*>& members) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (enterType == IZET_PARTY) {
        if (CParty* const party = user->GetParty()) {
            party->GetAllMember(members);
            return;
        }
    } else if (enterType == IZET_MPCC) {
        if (CMultiPartyCommandChannel* const channel = user->GetMPCC()) {
            channel->GetAllMember(members);
            return;
        }
    }
    members.push_back(user);
}

void CInstantZoneManager::DeleteInstantZoneByBuilder(CInstantZone& zone) {
    L2_GUARD_FUNCTION();
    zone.ExpelAllUser(true);
    DeleteInstantZone(zone, IZDI_BY_BUILDER);
}

void CInstantZoneManager::SetFlagList(
    const int userDbId, const int typeId, const int flagId, const int value, const bool save
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_InzoneFlagMap[InzoneFlagKey{userDbId, typeId}][flagId] = value;
    if (save) {
        db.RequestSaveInstantZoneFlag(userDbId, flagId, value);
    }
}

void CInstantZoneManager::SetInstantZoneFlag(
    const int userDbId, const int typeId, const int count, const unsigned char* packet
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const InzoneFlagKey key{userDbId, typeId};
    if (m_InzoneFlagMap.find(key) != m_InzoneFlagMap.end()) {
        return;
    }

    FlagMap flags;
    for (int index = 0; index < count; ++index) {
        int flagId = 0;
        int value = 0;
        packet = ::Disassemble(packet, "dd", &flagId, &value);
        flags.emplace(flagId, value);
    }
    m_InzoneFlagMap.emplace(key, std::move(flags));
}

void CInstantZoneManager::SetValidFlagList(User* const user, CInstantZone* const zone) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || zone == nullptr || user->m_nAssociatedInstantZoneDBId == 0) {
        return;
    }

    const int sourceTypeId = zone->GetKey().typeId;
    const auto position = m_InzoneFlagMap.find(InzoneFlagKey{user->m_nAssociatedInstantZoneDBId, sourceTypeId});
    if (position == m_InzoneFlagMap.end()) {
        return;
    }

    const FlagMap flags = position->second;
    for (const auto& flag : flags) {
        SetFlagList(zone->m_nInzoneDBId, zone->m_nTypeId, flag.first, flag.second, true);
    }
}

bool CInstantZone::IsInZone() { return true; }

CInstantZone* CInstantZone::CastInZone() { return this; }

int CInstantZone::GetInstantZoneTypeID() { return -1; }

bool CInstantZone::CanBeCreated(CreateFailReason& reason) {
    L2_GUARD_FUNCTION();
    if (m_const == nullptr) {
        reason = CFR_INTERNAL_ERROR;
        return false;
    }

    if ((m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) && m_const->m_bSpawnNpc &&
        !g_PledgeTimer.IsAllNpcLoaded()) {
        reason = CFR_NPC_SERVER_DOWN;
        return false;
    }

    const int current = runtime::raw_atomic::IncrementI32(&m_nCurrentZoneCount);
    if (current <= MAX_ZONE_COUNT) {
        return true;
    }

    runtime::raw_atomic::AddI32(&m_nCurrentZoneCount, -1);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[%s][%d] overflow instantzone[%d] count[%d]",
    //     L"InstantZone.cpp",
    //     0xd15,
    //     m_nTypeId,
    //     static_cast<int>(m_nCurrentZoneCount))
    L2_LOG_INFO(
        Log, u"[%s][%d] overflow instantzone[%d] count[%d]", u"InstantZone.cpp", 0xd15, m_nTypeId,
        static_cast<int>(m_nCurrentZoneCount)
    );
    reason = CFR_LIMIT_ZONE_COUNT;
    return false;
}

void CInstantZone::InitNPC() {
    L2_GUARD_FUNCTION();
    if (g_L2ServerType == L2ServerType::WORLDSERVER || m_const == nullptr || !m_const->m_bSpawnNpc) {
        return;
    }

    CPacketHelperEx<20480> packet;
    int count = 0;
    g_InstantZoneMgr.InitInZoneFlag(m_nInzoneDBId, count, packet);
    g_npcserver.Send(
        "cdddddb", 0x6a, -2, m_nTypeId, m_const->m_clusterId, static_cast<int>(Id()), count, packet.GetLength(),
        packet.GetPacket()
    );
}

bool CInstantZone::IsAssociatedUser(User* const user) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    const UserKey key(user->m_data->GetWorldId(), user->GetDBID());
    const auto position = m_EnteredUserMap.find(key);
    return position != m_EnteredUserMap.end() && position->second != nullptr && position->second->associated;
}

void CInstantZone::EnterZone(CStaticObject& object) {
    L2_GUARD_FUNCTION();
    if (IsDeleting() || m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] StaticObject is Entering in deleted InstantZone",
        //     L"InstantZone.cpp",
        //     0xeae)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] StaticObject is Entering in deleted InstantZone",
        //     L"InstantZone.cpp",
        //     0xeae)
        L2_LOG_ERROR(Log, "Cannot add static object to instant zone type %d: zone is deleted or being deleted.", m_nTypeId);
        return;
    }

    L2_GUARD(m_InstantZoneLock);
    m_InstantZoneStaticObjectList.push_back(&object);
    object.SetInZoneID(Id());
}

void CInstantZone::Delete(const InstantZoneDeleteInfo info) {
    L2_GUARD_FUNCTION();
    const int deleteCount = runtime::raw_atomic::IncrementI32(&m_nInstantZoneDeleteCount);
    if (deleteCount != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid CInstantZone::Delete",
        //     L"InstantZone.cpp",
        //     0xa7a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid CInstantZone::Delete",
        //     L"InstantZone.cpp",
        //     0xa7a)
        L2_LOG_ERROR(Log, "Ignoring instant zone type %d deletion: delete count is %d, expected 1.", m_nTypeId, deleteCount);
        runtime::process::ReportCallStack(2, true);
        return;
    }

    L2_GUARD(m_InstantZoneLock);
    _ClearInstantZoneUserMap();
    SetDeleting(true);
    if (m_const != nullptr && m_const->m_bSpawnNpc) {
        g_npcserver.Send("cdddd", 0x6a, -1, m_nTypeId, m_const->m_clusterId, static_cast<int>(Id()));
    }
    new CInstantZoneReleaseVerifier(this);
    CGeneralObject::Delete();
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,,,,,,,,,,,%I64d,%I64d,%I64d,,,,,,,,,,,,",
    //     static_cast<std::int64_t>(0x382),
    //     static_cast<std::int64_t>(m_nTypeId),
    //     static_cast<std::int64_t>(Id()),
    //     static_cast<std::int64_t>(info))
    // Portable: %I64d becomes %lld. Int64_t can be long on LP64 hosts.
    Log.Add(
        LOG_BOTH, u"%I64d,,,,,,,,,,,%I64d,%I64d,%I64d,,,,,,,,,,,,", static_cast<long long>(0x382),
        static_cast<long long>(m_nTypeId), static_cast<long long>(Id()), static_cast<long long>(info)
    );
}

void CInstantZone::_ClearInstantZoneUserMap() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_ExistingUserMap) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(entry.first);
        if (entry.second != nullptr) {
            L2_RELEASE(entry.second, ObjectRefType::CreateDelete);
        }
    }
    m_ExistingUserMap.clear();

    for (const auto& entry : m_EnteredUserMap) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(entry.first);
        InfoForEntered* const info = entry.second;
        if (user) {
            user->SetAssociatedInstantZoneId(0, 0, 0, false);
        }

        const int point = m_const->GetPCCafePoint();
        if (point > 0 && info != nullptr && !info->associated) {
            CLogFormatter formatter;
            if (!user) {
                db.CreateUpdateItemsWithPoint(
                    entry.first.m_PrimaryKey, static_cast<std::uint32_t>(entry.first.m_SecondaryKey),
                    static_cast<std::uint32_t>(info->logInfo.accountId), 0, point, PT_PCCAFE_POINT, 0, 0, nullptr, nullptr,
                    9999
                );
                formatter.WriteLog(
                    0x1f6f, entry.first.m_SecondaryKey, info->logInfo.accountId, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, 0,
                    0, 0, 0, m_nTypeId, Id(), point, 0, 0, 0, info->logInfo.charName.c_str(),
                    info->logInfo.accountName.c_str(), nullptr, nullptr, 0
                );
            } else {
                user->UpdatePoint(point, PT_PCCAFE_POINT, DJWP_ADD, 0xbbe);
                formatter.WriteLog2(
                    0x1f6f, user.get(), nullptr, m_nTypeId, Id(), point, user->m_data->m_nPCCafePoint + point, 0, 0, 0
                );
            }
        }

        if (info != nullptr) {
            L2_RELEASE(info, ObjectRefType::CreateDelete);
        }
    }
    m_EnteredUserMap.clear();
}

void CInstantZone::_ClearAllUserMap() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_EnteredUserMap) {
        if (entry.second != nullptr) {
            L2_RELEASE(entry.second, ObjectRefType::CreateDelete);
        }
    }
    for (const auto& entry : m_ExistingUserMap) {
        if (entry.second != nullptr) {
            L2_RELEASE(entry.second, ObjectRefType::CreateDelete);
        }
    }
    m_EnteredUserMap.clear();
    m_ExistingUserMap.clear();
}

void CInstantZone::_DeleteNPCNotRelatedToPCFromCreatureMap() {
    L2_GUARD_FUNCTION();

    for (auto position = m_mapInstantZoneCreature.begin(); position != m_mapInstantZoneCreature.end();) {
        const auto current = position++;
        CSPointer<CCreature> creature = CCreature::FindObjectSP(current->first);
        if (!creature || !creature->IsNPC()) {
            continue;
        }

        if (creature->CastNPC()->IsSummonOrPet()) {
            continue;
        }

        m_mapInstantZoneCreature.erase(current);
        runtime::raw_atomic::AddI32(&g_nInZoneNPCCount, -1);
        creature->CastNPC()->Despawn(true, false);
    }
}

void CInstantZone::_DeleteNPCFromCreatureMap() {
    L2_GUARD_FUNCTION();

    for (auto position = m_mapInstantZoneCreature.begin(); position != m_mapInstantZoneCreature.end();) {
        const auto current = position++;
        CSPointer<CCreature> creature = CCreature::FindObjectSP(current->first);
        if (!creature || !creature->IsNPC()) {
            continue;
        }

        CNPC* const npc = creature->CastNPC();
        m_mapInstantZoneCreature.erase(current);
        runtime::raw_atomic::AddI32(&g_nInZoneNPCCount, -1);
        if (npc != nullptr) {
            npc->Despawn(true, false);
        }
    }
}

void CInstantZone::DeleteNPCNotRelatedToPCFromCreatureMap() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    _DeleteNPCNotRelatedToPCFromCreatureMap();
}

void CInstantZone::DeleteNPCFromCreatureMap() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    _DeleteNPCFromCreatureMap();
}

void CInstantZone::_ClearInstantZoneCreatureMap() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapInstantZoneCreature) {
        CSPointer<CCreature> creature = CCreature::FindObjectSP(entry.first);
        if (!creature || !creature->IsNPC()) {
            continue;
        }

        if (!creature->IsPet() || creature->IsAlive()) {
            if (!creature->IsSummon()) {
                static_cast<void>(creature->IsPet());
            }
            creature->CastNPC()->Despawn(true, false);
        }
        runtime::raw_atomic::AddI32(&g_nInZoneNPCCount, -1);
    }
    m_mapInstantZoneCreature.clear();
}

void CInstantZone::_ClearInstantZoneItemMap() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapInstantZoneItem) {
        CItem* const item = ItemSmartIdManager()->GetObjectW(entry.first);
        if (item) {
            CWorld::Inst().DeleteInZoneItem(item);
        }
    }
    m_mapInstantZoneItem.clear();
}

void CInstantZone::_ClearInstantZoneDoorMap() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapInstantZoneDoor) {
        CDoor* const door = entry.second;
        if (door == nullptr) {
            continue;
        }
        CWorld::Inst().StaticObjectDisappear(door);
        door->Delete();
    }
    m_mapInstantZoneDoor.clear();
}

void CInstantZone::_ClearAreaEventControllerMap() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapEventControllerByArea) {
        if (entry.second != nullptr) {
            entry.second->Delete();
        }
    }
    m_mapEventControllerByArea.clear();
}

void CInstantZone::_ClearInstantZoneStaticObject() {
    L2_GUARD_FUNCTION();
    for (CStaticObject* const object : m_InstantZoneStaticObjectList) {
        if (object == nullptr) {
            continue;
        }
        CWorld::Inst().StaticObjectDisappear(object);
        object->Delete();
    }
    m_InstantZoneStaticObjectList.clear();
}

void CInstantZone::OnRelease() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);

    _ClearInstantZoneCreatureMap();
    _ClearInstantZoneItemMap();
    _ClearInstantZoneDoorMap();
    _ClearAreaEventControllerMap();
    _ClearInstantZoneStaticObject();
    g_WorldTrapDB.OnInZoneDelete(static_cast<int>(Id()));
    CContainerDB::Inst().FreePrivateContainer(m_pContainer);
    m_pContainer = nullptr;

    CInstantZone* const templateZone = g_InstantZoneDB.GetInstantZone(m_nTypeId, m_const->m_clusterId);
    if (templateZone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid inzone template in inzone db. type id[%d]",
        //     L"InstantZone.cpp",
        //     0xa66,
        //     m_nTypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid inzone template in inzone db. type id[%d]",
        //     L"InstantZone.cpp",
        //     0xa66,
        //     m_nTypeId)
        L2_LOG_ERROR(
            Log, "Cannot decrement active zone count for type %d, cluster %d: template is missing after zone cleanup.",
            m_nTypeId, m_const->m_clusterId
        );
        return;
    }
    runtime::raw_atomic::AddI32(&templateZone->m_nCurrentZoneCount, -1);
}

bool CInstantZone::EnterZone(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }

    if (!IsDeleting() && m_Deleted == 0) {
        L2_GUARD(m_InstantZoneLock);

        const auto position = m_mapInstantZoneItem.find(item->Id());
        if (position == m_mapInstantZoneItem.end()) {
            item->SetInZoneID(Id());
            m_mapInstantZoneItem[item->Id()] = item->GetPos();
            return true;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] try hack? how to enter item? [%d]",
        //     L"InstantZone.cpp",
        //     0xe94,
        //     item->m_data->m_nClassID)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] try hack? how to enter item? [%d]",
        //     L"InstantZone.cpp",
        //     0xe94,
        //     item->m_data->m_nClassID)
        L2_LOG_ERROR(
            Log, "Cannot add item class %d to instant zone type %d: item is already registered.", item->m_data->m_nClassID,
            m_nTypeId
        );
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] ITEM[%d] is Entering in deleted InstantZone",
    //     L"InstantZone.cpp",
    //     0xe8b,
    //     item->m_data->m_nClassID)
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] ITEM[%d] is Entering in deleted InstantZone",
    //     L"InstantZone.cpp",
    //     0xe8b,
    //     item->m_data->m_nClassID)
    L2_LOG_ERROR(
        Log, "Cannot add item class %d to instant zone type %d: zone is deleted or being deleted.", item->m_data->m_nClassID,
        m_nTypeId
    );
    return false;
}

bool CInstantZone::LeaveZone(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }

    L2_GUARD(m_InstantZoneLock);
    const auto position = m_mapInstantZoneItem.find(item->Id());
    if (position == m_mapInstantZoneItem.end()) {
        return false;
    }
    m_mapInstantZoneItem.erase(position);
    return true;
}

bool CInstantZone::EnterZone(CNPC* npc, const bool setInZoneId) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr || IsDeleting() || m_Deleted != 0) {
        return false;
    }

    L2_GUARD(m_InstantZoneLock);
    if (!npc->IsSummonOrPet() && m_mapInstantZoneCreature.find(npc->Id()) != m_mapInstantZoneCreature.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] try hack? how to enter NPC? [%d]",
        //     L"InstantZone.cpp",
        //     0xe5c,
        //     npc->GetNPCId())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] try hack? how to enter NPC? [%d]",
        //     L"InstantZone.cpp",
        //     0xe5c,
        //     npc->GetNPCId())
        L2_LOG_ERROR(
            Log, "Cannot add NPC %d to instant zone type %d: NPC is already registered.", npc->GetNPCId(), m_nTypeId
        );
        runtime::process::ReportCallStack(2, true);
        return false;
    }

    const FVector position = npc->GetPos();
    if (setInZoneId) {
        npc->SetInZoneID(Id());
    }
    m_mapInstantZoneCreature[npc->Id()] = position;
    if (m_nTypeId == 1) {
        npc->InitializeAreaMod();
    }
    runtime::raw_atomic::IncrementI32(&g_nInZoneNPCCount);
    return true;
}

bool CInstantZone::LeaveZone(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return false;
    }

    L2_GUARD(m_InstantZoneLock);
    const auto position = m_mapInstantZoneCreature.find(npc->Id());
    if (position == m_mapInstantZoneCreature.end()) {
        return false;
    }
    m_mapInstantZoneCreature.erase(position);
    runtime::raw_atomic::AddI32(&g_nInZoneNPCCount, -1);
    if (npc->IsSummonOrPet()) {
        npc->SetInZoneID(0);
        npc->TakeOffItems();
    }
    return true;
}

bool CInstantZone::EnterZoneWithNoCheck(
    User* user,
    const FVector currentPosition,
    const FVector destination,
    const InZoneEnterType enterType,
    const InZoneEnterReasonType enterReason
) {
    L2_GUARD_FUNCTION();
    // Current 0x007b4860 has no defensive m_const gate here.
    if (user == nullptr || IsDeleting() || m_Deleted != 0) {
        return false;
    }

    user->DoNothing();
    // Current 0x007b4961..0x007b4969 passes this instant-zone object's ID
    // as inZoneId (stack +0x20) and 0x10000 as heading (stack +0x28).
    if (!user->TeleportToLocation(
            static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z),
            static_cast<int>(Id()), 0x10000, TeleportSourceType::BuilderCmd, nullptr, 0
        )) {
        return false;
    }

    int latestBindingTime = 0;
    EntranceInfo ownEntrance{};
    EntranceInfo groupEntrance{};
    user->GetInZoneRestriction(m_nTypeId, false, latestBindingTime, ownEntrance.count, ownEntrance.maxCount);
    if (m_const->m_instantZoneGroup != nullptr) {
        user->GetInZoneRestriction(
            m_const->m_instantZoneGroup->GetGroupId(), true, latestBindingTime, groupEntrance.count, groupEntrance.maxCount
        );
    }

    {
        L2_GUARD(m_InstantZoneLock);
        m_mapInstantZoneCreature.emplace(user->Id(), currentPosition);

        InfoForExisting* const existingInfo = new InfoForExisting;
        const auto entered = m_EnteredUserMap.find(UserKey(user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())));
        if (entered == m_EnteredUserMap.end()) {
            m_EnteredUserMap.emplace(
                UserKey(user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())),
                new InfoForEntered(m_const->m_bIsAssociative, enterReason, ownEntrance, groupEntrance, user)
            );
            if (m_const->m_bindingTiming > 0) {
                existingInfo->useTime = 0;
            }
        } else if (m_const->m_bindingTiming > 0 && entered->second != nullptr) {
            existingInfo->useTime = entered->second->usedTime;
        }
        m_ExistingUserMap.emplace(UserKey(user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())), existingInfo);
    }

    if (m_nTypeId == 1) {
        user->InitializeAreaMod();
    }
    if (m_const->m_removeBuff > 0) {
        user->DispelByBuffProtectionFlag(1);
    }

    if (user->HavePet() || user->HaveSummon()) {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            user, summon,
            if (m_const->m_removeBuff > 0) { summon->DispelByBuffProtectionFlag(1); } summon->m_data->SetPos(destination);
            EnterZone(summon, false);
        );
    }

    if (m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        InstantZoneLogHelper::Log(this, user, 0x378, enterType, enterReason, 0);
    }
    return true;
}

bool CInstantZone::EnterZone(
    User* user,
    const FVector currentPosition,
    const FVector destination,
    const InZoneEnterType enterType,
    const InZoneEnterReasonType enterReason,
    TeleportSourceType::Enum source
) {
    L2_GUARD_FUNCTION();
    // Current 0x007b4e18 has no defensive m_const/user->m_data gates here.
    if (user == nullptr || IsDeleting() || m_Deleted != 0) {
        return false;
    }

    if (user->m_nAssociatedInstantZoneId == 0) {
        for (const auto& required : m_const->m_RequiredItemList) {
            if (user->HasItem(static_cast<unsigned int>(required.first)) < required.second) {
                return false;
            }
        }
        for (const auto& removed : m_const->m_DeletedItemList) {
            if (removed.second < 0) {
                if (!user->DeleteItemInInventory(removed.first)) {
                    return false;
                }
            } else if (!user->DeleteItemInInventory(removed.first, removed.second, nullptr, false, false)) {
                return false;
            }
        }
    }

    user->DoNothing();
    if (static_cast<int>(enterType) == static_cast<int>(TeleportSourceType::InstantAgit)) {
        source = TeleportSourceType::InzoneEnterZone;
    }
    // Current 0x007b4fc1..0x007b500b keeps the same inZoneId/heading
    // order for both source branches of the checked entry path.
    if (!user->TeleportToLocation(
            static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z),
            static_cast<int>(Id()), 0x10000, source, nullptr, 0
        )) {
        return false;
    }

    int latestBindingTime = 0;
    EntranceInfo ownEntrance{};
    EntranceInfo groupEntrance{};
    user->GetInZoneRestriction(m_nTypeId, false, latestBindingTime, ownEntrance.count, ownEntrance.maxCount);
    if (m_const->m_instantZoneGroup != nullptr) {
        user->GetInZoneRestriction(
            m_const->m_instantZoneGroup->GetGroupId(), true, latestBindingTime, groupEntrance.count, groupEntrance.maxCount
        );
    }

    bool reentered = true;
    {
        L2_GUARD(m_InstantZoneLock);
        m_mapInstantZoneCreature.emplace(user->Id(), currentPosition);

        InfoForExisting* const existingInfo = new InfoForExisting;
        const UserKey key(user->m_data->GetWorldId(), static_cast<int>(user->GetDBID()));
        const auto entered = m_EnteredUserMap.find(key);
        if (entered == m_EnteredUserMap.end()) {
            m_EnteredUserMap.emplace(
                key, new InfoForEntered(m_const->m_bIsAssociative, enterReason, ownEntrance, groupEntrance, user)
            );
            reentered = false;
            if (m_const->m_bindingTiming > 0) {
                existingInfo->useTime = 0;
            }
        } else if (m_const->m_bindingTiming > 0 && entered->second != nullptr) {
            existingInfo->useTime = entered->second->usedTime;
        }
        m_ExistingUserMap.emplace(key, existingInfo);
    }

    if (m_const->m_bIsAssociative && user->m_nAssociatedInstantZoneId == 0) {
        SetAssociate(user);
    }
    if (m_const->m_pccafePoint > 0 && !reentered) {
        user->UpdatePoint(-m_const->m_pccafePoint, PT_PCCAFE_POINT, DJWP_DECREASE, 0);
    }
    if (m_nTypeId == 1) {
        user->InitializeAreaMod();
    }
    if (m_const->m_removeBuff > 0) {
        user->DispelByBuffProtectionFlag(1);
    }

    if (user->HavePet() || user->HaveSummon()) {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            user, summon,
            if (m_const->m_removeBuff > 0) { summon->DispelByBuffProtectionFlag(1); } summon->m_data->SetPos(destination);
            EnterZone(summon->CastNPC(), false);
        );
    }

    if (m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        InstantZoneLogHelper::Log(this, user, 0x378, enterType, enterReason, 0);
    }
    return true;
}

bool CInstantZone::LeaveZoneWithoutTeleport(User* const user, FVector& returnPosition, const bool disorder) {
    L2_GUARD_FUNCTION();
    returnPosition = FVector{};
    if (user == nullptr) {
        return false;
    }

    {
        L2_GUARD(m_InstantZoneLock);
        // The current body deliberately requests the disorder return route
        // here regardless of the caller's summon-handling flag.
        if (!_GetRetPos(user, true, returnPosition)) {
            return false;
        }
        _DeleteUserFromMap(*user);
    }

    if (user->HavePet() || user->HaveSummon()) {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            user, summon, if (!summon->IsAlive() && !disorder) { LeaveZone(summon->CastNPC()); }
        );
    }

    if (m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        CParty* const party = user->GetParty();
        const std::int32_t partyId = party != nullptr ? runtime::Int32FromBits(party->Id()) : 0;
        ClearForcedSunRise(user, &user->m_nSunRiseForce);
        if (m_nTypeId == 0x88) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[NO_ERROR] solasys Leave inzone check")
            // Noise: routine zone 0x88 exit trace has no failure condition.
            // the following accounting call already records the exit.
            // L2_LOG_ERROR(Log,
            //     "[NO_ERROR] solasys Leave inzone check")
            // runtime::process::ReportCallStack(2, false)
        }
        WriteInstantZoneLeaveLog(user, m_nTypeId, Id(), partyId, FVector{});
    }
    return true;
}

bool CInstantZone::LeaveZone(User* const user, bool sendMessage, const int x, const int y, const int z, const int heading) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_nInstantZoneTypeID != m_nTypeId) {
        return false;
    }

    if (m_nContentFinishTick != 0) {
        sendMessage = false;
    }

    const FVector oldPosition = user->GetPos();
    FVector returnPosition{};
    if (!GetRetPos(*user, sendMessage, returnPosition)) {
        return false;
    }
    if (x != 0 || y != 0 || z != 0) {
        returnPosition = FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    }

    user->StopMove();
    user->DoNothing();
    // Current 0x007b5ccf/0x007b5cc7 places destination in-zone 0 and
    // heading 0x10000 in the fifth and sixth explicit argument lanes.
    if (!user->TeleportToLocation(
            static_cast<int>(returnPosition.x), static_cast<int>(returnPosition.y), static_cast<int>(returnPosition.z), 0,
            0x10000, TeleportSourceType::InzoneLeaveZone, nullptr, heading
        )) {
        return false;
    }

    if (m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        CParty* const party = user->GetParty();
        const std::int32_t partyId = party != nullptr ? runtime::Int32FromBits(party->Id()) : 0;
        ClearForcedSunRise(user, &user->m_nSunRiseForce);
        WriteInstantZoneLeaveLog(user, m_nTypeId, Id(), partyId, oldPosition);
    }
    return true;
}

void CInstantZone::GetAllActiveUser(std::vector<User*>& users) {
    L2_GUARD_FUNCTION();
    users.clear();

    L2_GUARD(m_InstantZoneLock);
    for (const auto& entry : m_ExistingUserMap) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(entry.first);
        if (user) {
            users.push_back(user.get());
        }
    }
}

void CInstantZone::ExpelAllUser(bool sendMessage) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllActiveUser(users);
    for (User* const user : users) {
        if (user != nullptr) {
            g_InstantZoneMgr.LeaveInstantZone(user, this, sendMessage, 0, 0);
        }
    }
}

int CInstantZone::GetUserCount() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    return static_cast<int>(m_ExistingUserMap.size());
}

int CInstantZone::GetTimeLimitLeft(User* user, const int currentTime, const int bindingTime) {
    L2_GUARD_FUNCTION();

    std::tm local{};
    if (m_const == nullptr || !runtime::crt::LocalTime32(currentTime, local)) {
        return 0;
    }

    const int weekDay = local.tm_wday;
    const int minuteOfDay = local.tm_hour * 60 + local.tm_min;
    if (m_const->GetZoneType() == IZT_PLEDGE_RAID) {
        PledgeRaidSystem::inst();
        const SharedPledgeRaidInfo* const raidInfo = SharedPledgeRaidInfo::GetInfo();
        if (raidInfo == nullptr || bindingTime < raidInfo->tSeasonStartTime_) {
            return 0;
        }
        return raidInfo->tNextSeasonStartTime_ - currentTime;
    }

    int remaining = 0;
    const int timeLimit = m_const->GetTimeLimit();
    if (currentTime < bindingTime + timeLimit &&
        currentTime - m_const->GetLatesetResetBindingDay(weekDay, minuteOfDay) * 60 < bindingTime) {
        remaining = timeLimit - currentTime + bindingTime;
        remaining = std::min(remaining, m_const->GetNextResetBindingDay(weekDay, minuteOfDay) * 60);
    }

    CInstantZoneGroup* const group = m_const->GetGroup();
    if (group != nullptr) {
        int latestBindingTime = bindingTime;
        int entranceCount = 0;
        int maxEntranceCount = 0;
        if (user->_GetInZoneRestriction(group->GetGroupId(), true, latestBindingTime, entranceCount, maxEntranceCount) &&
            currentTime - group->GetLatesetResetBindingDay(weekDay, minuteOfDay) * 60 < latestBindingTime &&
            entranceCount <= maxEntranceCount) {
            remaining = group->GetTimeLimit() - currentTime + latestBindingTime;
            remaining = std::min(remaining, group->GetNextResetBindingDay(weekDay, minuteOfDay) * 60);
        }
    }
    return remaining;
}

FVector CInstantZone::GetSavingPos(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_const == nullptr) {
        return FVector{};
    }

    L2_GUARD(m_InstantZoneLock);
    const auto position = m_mapInstantZoneCreature.find(user->Id());
    if (position == m_mapInstantZoneCreature.end()) {
        return FVector{};
    }
    return m_const->GetEndPos(user, position->second, true);
}

int CInstantZone::GetInstantZoneDoorSize() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    return static_cast<int>(m_mapInstantZoneDoor.size());
}

CDoor* CInstantZone::GetDoor(const char16_t* name) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);

    const ::std::u16string key = std::u16string(name != nullptr ? name : u"");
    const auto position = m_mapInstantZoneDoor.find(key);
    return position == m_mapInstantZoneDoor.end() ? nullptr : position->second;
}

void CInstantZone::SetCollisionEnabled(::std::u16string name, const bool enabled) {
    L2_GUARD_FUNCTION();

    const auto position = m_mapNameToCollisionMap.find(name);
    if (position == m_mapNameToCollisionMap.end()) {
        const std::string collisionName = Utf16ToUtf8(runtime::StringView(name));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetCollisionEnabled failed. undefined collision[%s]",
        //     L"InstantZone.cpp",
        //     0x13ea,
        //     collisionName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SetCollisionEnabled failed. undefined collision[%s]",
        //     L"InstantZone.cpp",
        //     0x13ea,
        //     collisionName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot set collision %s enabled=%d in instant zone type %d: collision is not registered.",
            collisionName.c_str(), static_cast<int>(enabled), m_nTypeId
        );
        return;
    }

    for (auto& entry : position->second) {
        entry.second = enabled;
    }
}

bool CInstantZone::GetCollisionCheck(CPlaneCollision* collision) {
    L2_GUARD_FUNCTION();

    for (const auto& collisionGroup : m_mapNameToCollisionMap) {
        const auto position = collisionGroup.second.find(collision);
        if (position != collisionGroup.second.end()) {
            return position->second;
        }
    }
    return false;
}

bool CInstantZone::GetAreaIsOn(CArea* area) {
    L2_GUARD_FUNCTION();
    if (area == nullptr) {
        return false;
    }

    const auto position = m_mapAreaOnOffFlag.find(area);
    return position == m_mapAreaOnOffFlag.end() ? area->IsOn(0) : position->second;
}

bool CInstantZone::SetBannedTerritoryOnOff(CArea* area, const int index, const bool on) {
    L2_GUARD_FUNCTION();
    if (area == nullptr || !GetAreaIsOn(area)) {
        return false;
    }

    const auto position = m_mapAreaOnOffFlag.find(area);
    return position != m_mapAreaOnOffFlag.end() && position->first->SetBannedTerritoryOnOff(index, on);
}

void CInstantZone::SetAllDoorOpenClose(const bool open) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);

    for (const auto& entry : m_mapInstantZoneDoor) {
        CDoor* const door = entry.second;
        if (door == nullptr) {
            continue;
        }
        if (open) {
            door->Open(false, false);
        } else {
            door->Close(false, false);
        }
        if (door->m_pEventController != nullptr) {
            door->m_pEventController->SetInZoneID(Id());
            const bool controllerOn = door->m_data != nullptr && door->m_data->m_nStealth > 0 ? !open : open;
            door->m_pEventController->SetOnOff(controllerOn);
        }
    }
}

CArea* CInstantZone::GetAreaByIndex(const int index) {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapAreaIndex) {
        if (entry.second == index) {
            return entry.first;
        }
    }
    return nullptr;
}

bool CInstantZone::SetAreaOnOff(CArea* area, const bool on) {
    L2_GUARD_FUNCTION();
    if (area == nullptr) {
        return false;
    }

    const auto flag = m_mapAreaOnOffFlag.find(area);
    if (flag == m_mapAreaOnOffFlag.end()) {
        return false;
    }
    flag->second = on;

    const auto controller = m_mapEventControllerByArea.find(area);
    if (controller != m_mapEventControllerByArea.end() && controller->second != nullptr) {
        controller->second->SetOnOff(on);
    }
    AreaStatusMap::inst().SetEvent(area->m_FieldTerritory.sName.c_str(), static_cast<int>(GetInZoneID()));
    return true;
}

void CInstantZone::InitArea() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapAreaOnOffFlag) {
        CArea* const area = entry.first;
        if (area == nullptr) {
            continue;
        }

        if (area->m_pEventController != nullptr) {
            CEventController* const controller = static_cast<CEventController*>(area->m_pEventController->Copy());
            if (controller != nullptr) {
                controller->SetInZoneID(Id());
                m_mapEventControllerByArea.emplace(area, controller);
            }
        }

        if (area->m_nInitialDelay > 0) {
            const auto inserted = m_mapAreaIndex.emplace(area, 0);
            AddTimer(area->m_nInitialDelay, inserted.first->second + 10000000);
        }
    }
}

void CInstantZone::InitDoor() {
    L2_GUARD_FUNCTION();

    for (const auto& collision : m_mapNameToCollisionMap) {
        const std::u16string_view doorName = runtime::StringView(collision.first);
        if (g_DoorDB.GetDoor(doorName.data(), 0) == nullptr) {
            continue;
        }

        CDoor* const door = static_cast<CDoor*>(g_DoorDB.GetDoor(doorName.data(), 0)->Copy());
        if (door == nullptr) {
            continue;
        }

        L2_GUARD(m_InstantZoneLock);
        if (m_mapInstantZoneDoor.find(door->GetDoorName()) != m_mapInstantZoneDoor.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] duplicated door[%s] at instant zone[%d]",
            //     L"InstantZone.cpp",
            //     0xc9d,
            //     door->GetDoorName(),
            //     static_cast<int>(Id()))
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] duplicated door[%s] at instant zone[%d]",
            //     L"InstantZone.cpp",
            //     0xc9d,
            //     door->GetDoorName(),
            //     static_cast<int>(Id()))
            L2_LOG_ERROR(
                Log, "Duplicate door %s in instant zone object %u; deleting the copy and stopping door initialization.",
                Utf16ToUtf8(door->GetDoorName()).c_str(), static_cast<unsigned>(Id())
            );
            door->Delete();
            break;
        }

        door->SetInZoneID(Id());
        m_mapInstantZoneDoor.emplace(door->GetDoorName(), door);
        CWorld::Inst().StaticObjectAppear(door, door->GetPos());
        if (door->m_pEventController != nullptr) {
            door->m_pEventController->SetInZoneID(Id());
            door->m_pEventController->SetOnOff(door->m_data->m_nStealth > 0 && !door->m_data->m_bIsOpen);
        }
    }
}

void CInstantZone::Activate() {
    L2_GUARD_FUNCTION();
    if (m_eStatus != IZS_READY) {
        return;
    }
    InitArea();
    InitDoor();
    InitNPC();
    m_eStatus = IZS_DOING;
}

void CInstantZone::BindUserOnTimerExpired() {
    L2_GUARD_FUNCTION();
    const int now = runtime::crt::Time32Now();
    std::vector<std::uint32_t> expiredUsers;
    for (auto& entry : m_ExistingUserMap) {
        InfoForExisting* const info = entry.second;
        if (info == nullptr) {
            continue;
        }
        if (info->useTime < 1 || info->useTime < m_const->m_bindingTiming) {
            ++info->useTime;
        } else {
            expiredUsers.push_back(static_cast<std::uint32_t>(entry.first.m_SecondaryKey));
        }
    }

    for (const std::uint32_t dbId : expiredUsers) {
        const UserKey key{g_nWorldId, static_cast<std::int32_t>(dbId)};
        CSPointer<User> user = g_UserDBIDMap.FindObject(key);
        if (user) {
            SetBinding(user.get(), now, IZBT_TIME);
        }
    }
}

bool CInstantZone::ExpelByNPCDownOnTimer() {
    L2_GUARD_FUNCTION();
    if (m_const->m_InZoneType != IZT_NORMAL && m_const->m_InZoneType != IZT_PLEDGE_RAID) {
        return false;
    }

    if (m_nExpelTickByNPCDown > 0 && m_nTickCount == m_nExpelTickByNPCDown) {
        ExpelAllUser(true);
        g_InstantZoneMgr.DeleteInstantZone(*this, IZDI_NPCSERVER_DOWN);
        return true;
    }

    if (m_const->m_bSpawnNpc && m_nExpelTickByNPCDown == 0 && !g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_60)) {
        std::vector<User*> users;
        GetAllActiveUser(users);
        for (User* const user : users) {
            if (user != nullptr) {
                user->SendSystemMessage(SystemMessageId::INSTANCE_CLOSING_NPC_SERVER_UNAVAILABLE);
            }
        }
        m_nExpelTickByNPCDown = m_nTickCount + 60;
    }
    return false;
}

void CInstantZone::SendRemainTimeMessage(const int messageId, const int minutes) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllActiveUser(users);
    for (User* const user : users) {
        if (user == nullptr || user->GetSocket() == nullptr) {
            continue;
        }
        user->GetSocket()->Send("chccd", 0x62, static_cast<int>(static_cast<std::int16_t>(messageId)), 1, 1, minutes);
    }
}

void CInstantZone::SetBinding(User* const user, int bindingTime, const InZoneBindingType bindingType) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (bindingTime == 0) {
        bindingTime = runtime::crt::Time32Now();
    }

    const UserKey key(user->m_data->m_nWorldId, static_cast<std::int32_t>(user->GetDBID()));
    L2_GUARD(m_InstantZoneLock);
    const auto position = m_EnteredUserMap.find(key);
    if (position != m_EnteredUserMap.end()) {
        _SetBinding(position->first, position->second, bindingTime, bindingType);
    }
}

void CInstantZone::SetBindingAllUser(const InZoneBindingType bindingType) {
    L2_GUARD_FUNCTION();
    const int bindingTime = runtime::crt::Time32Now();
    L2_GUARD(m_InstantZoneLock);
    for (const auto& entry : m_EnteredUserMap) {
        _SetBinding(entry.first, entry.second, bindingTime, bindingType);
    }
}

void CInstantZone::SetBindingAllUserInPledge() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    if (m_EnteredUserMap.empty()) {
        return;
    }

    CSPointer<User> user = g_UserDBIDMap.FindObject(m_EnteredUserMap.begin()->first);
    if (!user) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        Singleton<PledgeRaidSystem>::inst().SetPledgeRaidInzoneBindingAllUserInPledge(pledge, true);
    }
}

void CInstantZone::SetAssociate(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SetAssociatedInstantZoneId(Id(), m_nTypeId, m_nInzoneDBId, true);
    }
}

void CInstantZone::SetInzoneFlag(const int flagId, const int value) {
    L2_GUARD_FUNCTION();
    g_InstantZoneMgr.SetFlagList(m_nInzoneDBId, m_nTypeId, flagId, value, true);
}

void CInstantZone::_SetBinding(
    const UserKey key, InfoForEntered* const info, const int bindingTime, const InZoneBindingType bindingType
) {
    L2_GUARD_FUNCTION();
    if (info == nullptr || m_const == nullptr) {
        return;
    }

    CSPointer<User> user = g_UserDBIDMap.FindObject(key);
    CInstantZoneGroup* const group = m_const->m_instantZoneGroup;
    if (info->binded) {
        if (!user) {
            db.RequestMarkInZoneRestriction(
                key, m_nTypeId, bindingTime, info->myEntranceInfo.count, false, info->myEntranceInfo.maxCount, m_nInzoneDBId
            );
            if (group != nullptr) {
                db.RequestMarkInZoneRestriction(
                    key, group->GetGroupId(), bindingTime, info->groupEntranceInfo.count, true,
                    info->groupEntranceInfo.maxCount, m_nInzoneDBId
                );
            }
        } else {
            user->UpdateInZoneRestrictionTime(m_nTypeId, bindingTime, false, m_nInzoneDBId);
            if (group != nullptr) {
                user->UpdateInZoneRestrictionTime(group->GetGroupId(), bindingTime, true, m_nInzoneDBId);
            }
            g_ToDoListManager.OnDailyMissionCheckAndUpdate(user.get(), E_DAILY_MISSION_TYPE_INZONECOUNT);
        }
        return;
    }

    const bool newRestriction = info->eReason == IZERT_NORMAL || info->eReason == IZERT_GROUP_EXTRA2;
    const bool resetOrExtra = info->eReason > IZERT_NORMAL && info->eReason < IZERT_GROUP_EXTRA2;

    if (newRestriction) {
        if (!user) {
            ++info->myEntranceInfo.count;
            db.RequestMarkInZoneRestriction(
                key, m_nTypeId, bindingTime, info->myEntranceInfo.count, false, info->myEntranceInfo.maxCount, m_nInzoneDBId
            );
            if (group != nullptr) {
                ++info->groupEntranceInfo.count;
                db.RequestMarkInZoneRestriction(
                    key, group->GetGroupId(), bindingTime, info->groupEntranceInfo.count, true,
                    info->groupEntranceInfo.maxCount, m_nInzoneDBId
                );
            }
        } else {
            user->MarkInZoneRestriction(m_nTypeId, bindingTime, false, 1, m_nInzoneDBId);
            if (group != nullptr) {
                user->MarkInZoneRestriction(group->GetGroupId(), bindingTime, true, group->GetMaxEntrance(), m_nInzoneDBId);
            }
            g_ToDoListManager.OnDailyMissionCheckAndUpdate(user.get(), E_DAILY_MISSION_TYPE_INZONECOUNT);
        }
    } else if (resetOrExtra) {
        if (!user) {
            db.RequestMarkInZoneRestriction(
                key, m_nTypeId, bindingTime, info->myEntranceInfo.count, false, info->myEntranceInfo.maxCount, m_nInzoneDBId
            );
            if (group != nullptr) {
                ++info->groupEntranceInfo.count;
                db.RequestMarkInZoneRestriction(
                    key, group->GetGroupId(), bindingTime, info->groupEntranceInfo.count, true,
                    info->groupEntranceInfo.maxCount, m_nInzoneDBId
                );
            }
        } else {
            user->UpdateInZoneRestrictionTime(m_nTypeId, bindingTime, false, m_nInzoneDBId);
            if (group != nullptr) {
                user->MarkInZoneRestriction(group->GetGroupId(), bindingTime, true, group->GetMaxEntrance(), m_nInzoneDBId);
                if (user->GetSocket() != nullptr) {
                    user->GetSocket()->Send(
                        "chcch", 0x62, SystemMessageId::NAMED_INSTANCE_ENTRY_RESTRICTED, 1, 10,
                        static_cast<int>(static_cast<std::int16_t>(m_nTypeId))
                    );
                }
            }
        }
    }

    info->binded = true;
    info->usedTime = 0;
    const auto existing = m_ExistingUserMap.find(key);
    if (existing != m_ExistingUserMap.end() && existing->second != nullptr) {
        existing->second->useTime = 0;
    }

    if (user) {
        InstantZoneLogHelper::Log(this, user.get(), 0x1f66, IZET_MAX, m_const->m_nTimeLimit, bindingType);
    } else {
        CLogFormatter formatter;
        formatter.WriteLog(
            0x1f66, key.m_SecondaryKey, info->logInfo.accountId, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0,
            m_nTypeId, Id(), 0, 0, m_const->m_nTimeLimit, bindingType, info->logInfo.charName.c_str(),
            info->logInfo.accountName.c_str(), nullptr, nullptr, 0
        );
    }
}

InZoneEnterReasonType CInstantZone::GetEnterReason(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return IZERT_FAILED;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    bool groupExtraEntrance = false;
    if (m_const->m_instantZoneGroup != nullptr) {
        const InstantZoneBindingLevel level = m_const->m_instantZoneGroup->IsAlreadyBoundUser(user, now);
        if (level == IZBL_BINDED || level == IZBL_ABS_BINDED) {
            return IZERT_FAILED;
        }
        groupExtraEntrance = level == IZBL_EXTRA_ENTRANCE;
    }

    int latestBindingTime = 0;
    int entranceCount = 0;
    int maxEntranceCount = 0;
    std::tm local{};
    runtime::crt::LocalTime32(now, local);
    if (!user->GetInZoneRestriction(m_nTypeId, false, latestBindingTime, entranceCount, maxEntranceCount)) {
        return groupExtraEntrance ? IZERT_GROUP_EXTRA2 : IZERT_NORMAL;
    }

    if (groupExtraEntrance) {
        return IZERT_GROUP_EXTRA1;
    }

    if (m_const->m_InZoneType == IZT_PLEDGE_RAID) {
        const SharedPledgeRaidInfo* const raidInfo = SharedPledgeRaidInfo::GetInfo();
        return raidInfo != nullptr && raidInfo->tSeasonStartTime_ < latestBindingTime ? IZERT_FAILED : IZERT_TIME_RESET;
    }

    if (now < latestBindingTime + m_const->m_nTimeLimit) {
        const int latestResetMinutes = m_const->GetLatesetResetBindingDay(local.tm_wday, local.tm_hour * 60 + local.tm_min);
        return now - latestResetMinutes * 60 < latestBindingTime ? IZERT_FAILED : IZERT_TIME_RESET;
    }
    return IZERT_BIND_EXPIRED;
}

InZoneEnterReasonType CInstantZone::CheckEnterConditionCommon(
    User* user, InstantZoneEnterFailInfo& failInfo, const int bindingId
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || static_cast<std::int32_t>(user->m_data->m_moveType) == CMT_FLY) {
        return IZERT_FAILED;
    }

    const bool normalOrPledgeRaid = m_const->m_InZoneType == IZT_NORMAL || m_const->m_InZoneType == IZT_PLEDGE_RAID;
    if (user->GetInZoneID() != 0 && normalOrPledgeRaid) {
        failInfo.reason = EFR_LIMIT_MULTIFUL_ZONE;
        return IZERT_FAILED;
    }

    const int associatedZoneId = user->m_nAssociatedInstantZoneId;
    InZoneEnterReasonType enterReason = IZERT_FAILED;
    if (associatedZoneId > 0 && normalOrPledgeRaid) {
        if (static_cast<unsigned int>(associatedZoneId) != Id()) {
            failInfo.reason = EFR_LIMIT_MULTIFUL_ZONE;
            return IZERT_FAILED;
        }
        enterReason = IZERT_NORMAL;
    } else {
        if (user->HasFlagOfSiege()) {
            failInfo.reason = EFR_LIMIT_ITEM;
            CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
            return IZERT_FAILED;
        }
        for (const auto& item : m_const->m_RequiredItemList) {
            if (user->m_inv.GetItemAmount(item.first, 0, true) < item.second) {
                failInfo.reason = EFR_LIMIT_ITEM;
                CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
                return IZERT_FAILED;
            }
        }
        for (const auto& item : m_const->m_DeletedItemList) {
            if (user->m_inv.GetItemAmount(item.first, 0, true) < item.second) {
                failInfo.reason = EFR_LIMIT_ITEM;
                CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
                return IZERT_FAILED;
            }
        }

        if (user->m_nCreateInstantZoneCount >= 6) {
            failInfo.reason = EFR_LIMIT_CREATE_COUNT;
            CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
            return IZERT_FAILED;
        }
        if (m_const->m_pccafePoint > 0 && user->m_data->m_nPCCafePoint < m_const->m_pccafePoint) {
            failInfo.reason = EFR_LIMIT_PCCAFE_POINT;
            CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
            return IZERT_FAILED;
        }
        if (!user->m_data->m_bIsActive) {
            failInfo.reason = EFR_ALREADY_TELEPORT;
            CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
            return IZERT_FAILED;
        }

        enterReason = GetEnterReason(user);
        if (enterReason == IZERT_FAILED) {
            failInfo.reason = EFR_LIMIT_USE_COUNT;
            CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
            return IZERT_FAILED;
        }
    }

    if (m_const->m_bSpawnNpc && !g_PledgeTimer.IsAllNpcLoaded()) {
        failInfo.reason = EFR_MAKER_NOT_LOADED;
        return IZERT_FAILED;
    }

    for (CInstantZoneEntranceCondition* const condition : m_const->m_vEntranceCond) {
        if (condition != nullptr && !condition->CanEnter(user, this, associatedZoneId > 0, bindingId)) {
            failInfo.reason = condition->GetFailReason();
            CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(user->GetName()).c_str());
            return IZERT_FAILED;
        }
    }
    return enterReason;
}

bool CInstantZone::CheckEnterCondition(
    User* user,
    const InZoneEnterType enterType,
    MemberListInfo& members,
    InstantZoneEnterFailInfo& failInfo,
    const bool force
) {
    L2_GUARD_FUNCTION();
    members.memberList.clear();
    members.pTypeList.clear();
    if (user == nullptr || m_const == nullptr) {
        return false;
    }

    const int bindingId = user->m_data == nullptr ? 0 : static_cast<int>(user->m_data->m_nPledgeID);
    if (force) {
        if (!user->IsBuilder()) {
            return false;
        }
        if (enterType == IZET_INDIVIDUAL) {
            const InZoneEnterReasonType reason = CheckEnterConditionCommon(user, failInfo, 0);
            members.memberList.push_back(user);
            members.pTypeList.push_back(reason);
            return true;
        }
        if (enterType == IZET_PARTY) {
            CParty* const party = user->GetParty();
            if (party == nullptr) {
                failInfo.reason = EFR_LIMIT_PARTY;
                return false;
            }
            party->GetAllMember(members.memberList);
        } else if (enterType == IZET_MPCC) {
            CMultiPartyCommandChannel* const channel = user->GetMPCC();
            if (channel == nullptr) {
                failInfo.reason = EFR_LIMIT_MPCC;
                return false;
            }
            channel->GetAllMember(members.memberList);
        } else {
            return false;
        }
        for (User* const member : members.memberList) {
            members.pTypeList.push_back(CheckEnterConditionCommon(member, failInfo, bindingId));
        }
        return true;
    }

    if (enterType == IZET_INDIVIDUAL) {
        const InZoneEnterReasonType reason = CheckEnterConditionCommon(user, failInfo, 0);
        if (reason == IZERT_FAILED) {
            return false;
        }
        members.memberList.push_back(user);
        members.pTypeList.push_back(reason);
        return true;
    }

    if (enterType == IZET_PARTY) {
        CParty* const party = user->GetParty();
        if (party == nullptr) {
            failInfo.reason = EFR_LIMIT_PARTY;
            return false;
        }
        User* const master = party->GetMaster();
        if (master != user) {
            failInfo.reason = EFR_NOT_PARTY_MASTER;
            return false;
        }
        party->GetAllMember(members.memberList);

        const std::size_t memberCount = members.memberList.size();
        if (memberCount > static_cast<std::size_t>(m_const->m_nMaxUser)) {
            failInfo.reason = EFR_LIMIT_PARTY_MEMBER_COUNT;
            return false;
        }
        if (memberCount < static_cast<std::size_t>(m_const->m_nMinUser)) {
            failInfo.reason = EFR_LIMIT_MIN_MEMBER_COUNT;
            failInfo.intParam = m_const->m_nMinUser;
            return false;
        }

        for (User* const member : members.memberList) {
            const InZoneEnterReasonType reason = CheckEnterConditionCommon(member, failInfo, bindingId);
            if (reason == IZERT_FAILED) {
                return false;
            }
            const FVector memberPosition = member->GetPos();
            const FVector masterPosition = master->GetPos();
            const double dx = masterPosition.x - memberPosition.x;
            const double dy = masterPosition.y - memberPosition.y;
            if (dx * dx + dy * dy > 250000.0) {
                failInfo.reason = EFR_LIMIT_DISTANCE;
                CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(member->GetName()).c_str());
                return false;
            }
            members.pTypeList.push_back(reason);
        }
        return true;
    }

    if (enterType == IZET_MPCC) {
        CMultiPartyCommandChannel* const channel = user->GetMPCC();
        if (channel == nullptr) {
            failInfo.reason = EFR_LIMIT_MPCC;
            return false;
        }
        User* const master = channel->GetMaster();
        if (master != user) {
            failInfo.reason = EFR_NOT_MPCC_MASTER;
            return false;
        }
        channel->GetAllMember(members.memberList);

        const std::size_t memberCount = members.memberList.size();
        if (memberCount > static_cast<std::size_t>(m_const->m_nMaxUser)) {
            failInfo.reason = EFR_LIMIT_MPCC_MEMBER_COUNT;
            return false;
        }
        if (memberCount < static_cast<std::size_t>(m_const->m_nMinUser)) {
            failInfo.reason = EFR_LIMIT_MIN_MEMBER_COUNT;
            failInfo.intParam = m_const->m_nMinUser;
            return false;
        }

        for (User* const member : members.memberList) {
            const InZoneEnterReasonType reason = CheckEnterConditionCommon(member, failInfo, bindingId);
            if (reason == IZERT_FAILED) {
                return false;
            }
            const FVector memberPosition = member->GetPos();
            const FVector masterPosition = master->GetPos();
            const double dx = masterPosition.x - memberPosition.x;
            const double dy = masterPosition.y - memberPosition.y;
            if (dx * dx + dy * dy > 250000.0) {
                failInfo.reason = EFR_LIMIT_DISTANCE;
                CopyInstantZoneFailUser(failInfo.efUser, Utf16ToWide(member->GetName()).c_str());
                return false;
            }
            members.pTypeList.push_back(reason);
        }
        return true;
    }
    return false;
}

bool CInstantZone::EnterZoneWithCheck(
    User* user, const InZoneEnterType enterType, InstantZoneEnterFailInfo& failInfo, const bool force
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_const == nullptr) {
        return false;
    }

    MemberListInfo members;
    if (m_eStatus == IZS_DOING) {
        const InZoneEnterReasonType reason = CheckEnterConditionCommon(user, failInfo, 0);
        if (reason == IZERT_FAILED) {
            return false;
        }
        members.memberList.push_back(user);
        members.pTypeList.push_back(reason);
    } else if (!CheckEnterCondition(user, enterType, members, failInfo, force)) {
        return false;
    }

    const FVector destination = m_const->GetStartPos(0);
    const FVector currentPosition = user->GetPos();
    for (std::size_t index = 0; index < members.memberList.size(); ++index) {
        if (!EnterZone(
                runtime::CheckedVectorAt(members.memberList, index), currentPosition, destination, enterType,
                runtime::CheckedVectorAt(members.pTypeList, index), TeleportSourceType::InstantAgit
            )) {
            return false;
        }
    }
    Activate();
    return true;
}

bool CInstantZone::GetRetPos(User& user, const bool disorder, FVector& position) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InstantZoneLock);
    return _GetRetPos(&user, disorder, position);
}

bool CInstantZone::_GetRetPos(User* user, const bool disorder, FVector& position) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data == nullptr || m_const == nullptr) {
        return false;
    }

    const UserKey key(user->m_data->GetWorldId(), static_cast<int>(user->GetDBID()));
    if (m_ExistingUserMap.find(key) == m_ExistingUserMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NO inzone(%d) user(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x1161,
        //     m_nTypeId,
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NO inzone(%d) user(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x1161,
        //     m_nTypeId,
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     "Cannot resolve return position for %s (DB ID %u) in instant zone type %d: user entry is missing.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()), m_nTypeId)
        L2_LOG_ERROR(
            Log, "Cannot resolve return position for %s (DB ID %u) in instant zone type %d: user entry is missing.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()), m_nTypeId
        );
        return false;
    }

    const auto creature = m_mapInstantZoneCreature.find(user->Id());
    if (creature == m_mapInstantZoneCreature.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NO inzone(%d) creature(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x1169,
        //     m_nTypeId,
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NO inzone(%d) creature(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x1169,
        //     m_nTypeId,
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     "Cannot resolve return position for %s (DB ID %u) in instant zone type %d: creature entry is missing.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()), m_nTypeId)
        L2_LOG_ERROR(
            Log, "Cannot resolve return position for %s (DB ID %u) in instant zone type %d: creature entry is missing.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()), m_nTypeId
        );
        return false;
    }

    position = m_const->GetEndPos(user, creature->second, disorder);
    position = user->GetInstantZoneReturnPoint(position);
    return true;
}

bool CInstantZone::_DeleteUserFromMap(User& user) {
    L2_GUARD_FUNCTION();
    if (user.m_data == nullptr) {
        return false;
    }

    const UserKey key(user.m_data->GetWorldId(), static_cast<int>(user.GetDBID()));
    const auto existing = m_ExistingUserMap.find(key);
    if (existing == m_ExistingUserMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NO inzone(%d) user(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x1192,
        //     m_nTypeId,
        //     user.GetName(),
        //     static_cast<int>(user.GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NO inzone(%d) user(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x1192,
        //     m_nTypeId,
        //     user.GetName(),
        //     static_cast<int>(user.GetDBID()))
        // L2_LOG_ERROR(Log,
        //     "Cannot remove user for %s (DB ID %u) in instant zone type %d: user entry is missing.",
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned>(user.GetDBID()), m_nTypeId)
        L2_LOG_ERROR(
            Log, "Cannot remove user for %s (DB ID %u) in instant zone type %d: user entry is missing.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned>(user.GetDBID()), m_nTypeId
        );
        return false;
    }

    const auto creature = m_mapInstantZoneCreature.find(user.Id());
    if (creature == m_mapInstantZoneCreature.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NO inzone(%d) creature(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x119a,
        //     m_nTypeId,
        //     user.GetName(),
        //     static_cast<int>(user.GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NO inzone(%d) creature(%s-%d) info!!!",
        //     L"InstantZone.cpp",
        //     0x119a,
        //     m_nTypeId,
        //     user.GetName(),
        //     static_cast<int>(user.GetDBID()))
        // L2_LOG_ERROR(Log,
        //     "Cannot remove user for %s (DB ID %u) in instant zone type %d: creature entry is missing.",
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned>(user.GetDBID()), m_nTypeId)
        L2_LOG_ERROR(
            Log, "Cannot remove user for %s (DB ID %u) in instant zone type %d: creature entry is missing.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned>(user.GetDBID()), m_nTypeId
        );
        return false;
    }
    m_mapInstantZoneCreature.erase(creature);

    const UserKey enteredKey(user.m_data->GetWorldId(), static_cast<int>(user.GetDBID()));
    const auto entered = m_EnteredUserMap.find(enteredKey);
    if (entered != m_EnteredUserMap.end() && entered->second != nullptr && existing->second != nullptr) {
        entered->second->usedTime += existing->second->useTime;
    }
    if (existing->second != nullptr) {
        L2_RELEASE(existing->second, ObjectRefType::CreateDelete);
    }
    m_ExistingUserMap.erase(existing);
    return true;
}
