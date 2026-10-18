#include "PledgeMissionInfo.h"

#include "Pledge.h"
#include "PledgeInfo.h"
#include "PledgeMastery.h"
#include "PledgeMission.h"
#include "PledgeMissionParser.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <string_view>

namespace {

bool EqualsAsciiCaseInsensitive(const char16_t* left, const std::u16string_view right) {
    std::size_t index = 0;
    for (; index < right.size(); ++index) {
        char16_t lhs = left[index];
        char16_t rhs = right[index];
        if (lhs >= u'a' && lhs <= u'z') {
            lhs = static_cast<char16_t>(lhs - (u'a' - u'A'));
        }
        if (rhs >= u'a' && rhs <= u'z') {
            rhs = static_cast<char16_t>(rhs - (u'a' - u'A'));
        }
        if (lhs != rhs || lhs == u'\0') {
            return false;
        }
    }
    return left[index] == u'\0';
}

} // namespace

IMissionCondition::~IMissionCondition() = default;
IMissionGoalCondition::~IMissionGoalCondition() = default;

bool UserInPledge_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x28,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x28,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot check clan membership for a mission: the user is unavailable; the condition fails.");
        return false;
    }
    return user->m_data->m_nPledgeID != 0;
}

UserJob_MissionCondition::UserJob_MissionCondition(const bool mainJob, const bool dualJob, const bool subJob) :
    bMainJob_(mainJob), bDualJob_(dualJob), bSubJob_(subJob) {}

bool UserJob_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x3d,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x3d,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot check the class type for a clan mission: the user is unavailable; the condition fails.");
        return false;
    }
    const CSharedCreatureData& data = *user->m_data;
    return (data.m_nSubJobId == 0 && bMainJob_) || (data.m_IsDualClass && bDualJob_) ||
        (data.m_nSubJobId > 0 && !data.m_IsDualClass && bSubJob_);
}

UserMinLevel_MissionCondition::UserMinLevel_MissionCondition(const int minimumLevel) : nMinLevel_(minimumLevel) {}

bool UserMinLevel_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x52,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x52,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check minimum character level %d for a clan mission: the user is unavailable; the condition fails.",
            nMinLevel_
        );
        return false;
    }
    return nMinLevel_ <= user->m_data->m_nLevel;
}

UserMaxLevel_MissionCondition::UserMaxLevel_MissionCondition(const int maximumLevel) : nMaxLevel_(maximumLevel) {}

bool UserMaxLevel_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x67,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x67,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check maximum character level %d for a clan mission: the user is unavailable; the condition fails.",
            nMaxLevel_
        );
        return false;
    }
    return user->m_data->m_nLevel <= nMaxLevel_;
}

PledgeLevel_MissionCondition::PledgeLevel_MissionCondition(const int minimumLevel) : nMinLevel_(minimumLevel) {}

bool PledgeLevel_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x7c,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x7c,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check minimum clan level %u for a mission: the user is unavailable; the condition fails.",
            static_cast<unsigned>(nMinLevel_)
        );
        return false;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(user->m_data->m_nWorldId, static_cast<int>(user->m_data->m_nPledgeID));
    return pledge.get() != nullptr && static_cast<std::uint32_t>(nMinLevel_) <= pledge->GetPledgeLevel();
}

PledgeMastery_MissionCondition::PledgeMastery_MissionCondition(const char16_t* masteryName) : nType_(PledgeMasteryID::NONE) {
    nType_ = PledgeStuff::ConvertToPledgeMasteryID(std::u16string(masteryName));
}

bool PledgeMastery_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x95,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0x95,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan mastery %d for a mission: the user is unavailable; the condition fails.",
            static_cast<int>(nType_)
        );
        return false;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(user->m_data->m_nWorldId, static_cast<int>(user->m_data->m_nPledgeID));
    return pledge.get() != nullptr && pledge->GetMastery()->GetPoint(nType_) != 0;
}

CompleteMission_MissionCondition::CompleteMission_MissionCondition(const int missionId) : nMissionId_(missionId) {}

bool CompleteMission_MissionCondition::Check(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0xad,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMissionInfo.cpp",
        //     0xad,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check prerequisite mission %d completion: the user is unavailable; the condition fails.",
            nMissionId_
        );
        return false;
    }
    UserPledgeMissionInfo* const mission = user->m_UserPledgeMission.Find(nMissionId_);
    return mission != nullptr && mission->nState_ == PledgeMission::PMS_COMPLETE;
}

StartEndDateTime_MissionCondition::StartEndDateTime_MissionCondition(
    const int startDate, const int startTime, const int endDate, const int endTime
) {
    std::tm start;
    start.tm_sec = 0;
    start.tm_mday = startDate % 100;
    start.tm_mon = (startDate / 100) % 100 - 1;
    start.tm_year = (startDate / 100) / 100;
    start.tm_year = (1 - start.tm_year / 100) * 100 + start.tm_year;
    start.tm_min = startTime % 100;
    start.tm_hour = (startTime / 100) % 100;
    tStartDateTime_ = runtime::crt::Mktime32(&start);

    std::tm end;
    end.tm_sec = 0;
    end.tm_mday = endDate % 100;
    end.tm_mon = (endDate / 100) % 100 - 1;
    end.tm_year = (endDate / 100) / 100;
    end.tm_year = (1 - end.tm_year / 100) * 100 + end.tm_year;
    end.tm_min = endTime % 100;
    end.tm_hour = (endTime / 100) % 100;
    tEndDateTime_ = runtime::crt::Mktime32(&end);
}

bool StartEndDateTime_MissionCondition::Check(User*) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return tStartDateTime_ <= now && now <= tEndDateTime_;
}

StartEndTime_MissionCondition::StartEndTime_MissionCondition(const int startTime, const int endTime) {
    tmStartTime_.tm_min = startTime % 100;
    tmStartTime_.tm_hour = (startTime / 100) % 100;
    tmEndTime_.tm_min = endTime % 100;
    tmEndTime_.tm_hour = (endTime / 100) % 100;
}

bool StartEndTime_MissionCondition::Check(User*) {
    L2_GUARD_FUNCTION();
    const std::tm* const local = runtime::crt::LocalTime32(runtime::crt::Time32Now());
    const int hour = local->tm_hour;
    if (tmStartTime_.tm_hour < hour && hour < tmEndTime_.tm_hour) {
        return true;
    }
    return (tmStartTime_.tm_hour == hour && tmStartTime_.tm_min <= local->tm_min) ||
        (tmEndTime_.tm_hour == hour && local->tm_min <= tmEndTime_.tm_min);
}

AvailableDay_MissionCondition::AvailableDay_MissionCondition(const bool availableDay[7]) {
    for (std::size_t index = 0; index < 7; ++index) {
        bAvailableDay_[index] = availableDay[index];
    }
}

bool AvailableDay_MissionCondition::Check(User*) {
    L2_GUARD_FUNCTION();
    const std::tm* const local = runtime::crt::LocalTime32(runtime::crt::Time32Now());
    return bAvailableDay_[local->tm_wday];
}

bool IsInIntList_MissionCondition::Check(const int value) {
    L2_GUARD_FUNCTION();
    for (const int entry : vIntList_) {
        if (entry == value) {
            return true;
        }
    }
    return false;
}

namespace PledgeMissionStuff {

PledgeMission::GoalType ConvertGoalTypeStringToEnum(const char16_t* value) {
    struct GoalName {
        std::u16string_view name;
        PledgeMission::GoalType type;
    };
    static constexpr std::array<GoalName, 41> names{{
        {u"HUNTING_ANY", PledgeMission::PMGT_HUNTING_ANY},
        {u"HUNTING", PledgeMission::PMGT_HUNTING},
        {u"W_HUNTING", PledgeMission::PMGT_W_HUNTING},
        {u"QUEST_ANY", PledgeMission::PMGT_QUEST_ANY},
        {u"QUEST", PledgeMission::PMGT_QUEST},
        {u"OLYMPIAD_JOIN", PledgeMission::PMGT_OLYMPIAD_JOIN},
        {u"OLYMPIAD_WIN", PledgeMission::PMGT_OLYMPIAD_WIN},
        {u"OLYMPIAD_LOSE", PledgeMission::PMGT_OLYMPIAD_LOSE},
        {u"OLYMPIAD_FIRST", PledgeMission::PMGT_OLYMPIAD_FIRST},
        {u"PLEDGE_GAME_JOIN", PledgeMission::PMGT_PLEDGE_GAME_JOIN},
        {u"PLEDGE_GAME_SURVIVE", PledgeMission::PMGT_PLEDGE_GAME_SURVIVE},
        {u"PLEDGE_GAME_ITEM", PledgeMission::PMGT_PLEDGE_GAME_ITEM},
        {u"PLEDGE_GAME_WIN", PledgeMission::PMGT_PLEDGE_GAME_WIN},
        {u"PLEDGE_GAME_FIRST", PledgeMission::PMGT_PLEDGE_GAME_FIRST},
        {u"CASTLEWAR_ATK_WIN", PledgeMission::PMGT_CASTLEWAR_ATK_WIN},
        {u"CASTLEWAR_DEF_WIN", PledgeMission::PMGT_CASTLEWAR_DEF_WIN},
        {u"W_CASTLEWAR_ATK_WIN", PledgeMission::PMGT_W_CASTLEWAR_ATK_WIN},
        {u"W_CASTLEWAR_DEF_WIN", PledgeMission::PMGT_W_CASTLEWAR_DEF_WIN},
        {u"ITEM_USE_SKILL_REDUCE", PledgeMission::PMGT_ITEM_USE_SKILL_REDUCE},
        {u"ITEM_DISASSEMBLE", PledgeMission::PMGT_ITEM_DISASSEMBLE},
        {u"VP_GET_ITEM", PledgeMission::PMGT_VP_GET_ITEM},
        {u"PCCAFE_POINT_GET", PledgeMission::PMGT_PCCAFE_POINT_GET},
        {u"MISSION_SUCCESS", PledgeMission::PMGT_MISSION_SUCCESS},
        {u"PC_MAIN_LEVEL_UP", PledgeMission::PMGT_PC_MAIN_LEVEL_UP},
        {u"PC_DUAL_LEVEL_UP", PledgeMission::PMGT_PC_DUAL_LEVEL_UP},
        {u"PC_GET_NOBLE", PledgeMission::PMGT_PC_GET_NOBLE},
        {u"PC_GET_HONORS", PledgeMission::PMGT_PC_GET_HONORS},
        {u"AI_CONTROL", PledgeMission::PMGT_AI_CONTROL},
        {u"FISHING_ANY", PledgeMission::PMGT_FISHING_ANY},
        {u"FISHING", PledgeMission::PMGT_FISHING},
        {u"FISHING_BAIT", PledgeMission::PMGT_FISHING_BAIT},
        {u"ITEM_VARI_TRY", PledgeMission::PMGT_ITEM_VARI_TRY},
        {u"ITEM_VARI_TRY_ANY", PledgeMission::PMGT_ITEM_VARI_TRY_ANY},
        {u"ITEM_ENSOUL_TRY", PledgeMission::PMGT_ITEM_ENSOUL_TRY},
        {u"ITEM_ENCHANT_TRY", PledgeMission::PMGT_ITEM_ENCHANT_TRY},
        {u"ITEM_ENCHANT_NUM_SUC", PledgeMission::PMGT_ITEM_ENCHANT_NUM_SUC},
        {u"ITEM_COMBI_TRY_ANY", PledgeMission::PMGT_ITEM_COMBI_TRY_ANY},
        {u"ITEM_COMBI_TRY", PledgeMission::PMGT_ITEM_COMBI_TRY},
        {u"ITEM_COMBI_GET", PledgeMission::PMGT_ITEM_COMBI_GET},
        {u"AUCTION_TRY", PledgeMission::PMGT_AUCTION_TRY},
        {u"AUCTION_SUCCESS", PledgeMission::PMGT_AUCTION_SUCCESS},
    }};
    for (const GoalName& entry : names) {
        if (EqualsAsciiCaseInsensitive(value, entry.name)) {
            return entry.type;
        }
    }
    return PledgeMission::PMGT_MAX;
}

} // namespace PledgeMissionStuff

PledgeMissionInfo::PledgeMissionInfo() {}

PledgeMissionInfo::~PledgeMissionInfo() = default;

PledgeMissionInfo& PledgeMissionInfo::operator=(const PledgeMissionInfo& other) {
    eCategory_ = other.eCategory_;
    bRepeat_ = other.bRepeat_;
    vMissionConditionList_ = other.vMissionConditionList_;
    eGoalType_ = other.eGoalType_;
    vMissionGoalConditionList_ = other.vMissionGoalConditionList_;
    nGoalCount_ = other.nGoalCount_;
    nRewardPledgeNameValue_ = other.nRewardPledgeNameValue_;
    nRewardPVPPoint_ = other.nRewardPVPPoint_;
    mRewardItemMap_ = other.mRewardItemMap_;
    return *this;
}

PledgeMissionDB::PledgeMissionDB() :
    mPledgeMissionCountOfOneWeekInfo_{0, 0, 0} {}

PledgeMissionDB::~PledgeMissionDB() = default;

void PledgeMissionDB::Load() {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, "Loading PledgeMissionDB")
    // L2_LOG_INFO(Log, "Loading PledgeMissionDB")
    L2_LOG_INFO(Log, "Loading clan-mission definitions from PledgeMission.txt.");

    const std::int64_t memoryBefore = g_Memory.GetTotalAllocSize();
    const std::uint32_t tickBefore = runtime::GetTickCount();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeMission.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/PledgeMission.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from PledgeMissionData file failed",
        //     L"PledgeMissionInfo.cpp",
        //     0x13e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from PledgeMissionData file failed",
        //     L"PledgeMissionInfo.cpp",
        //     0x13e)
        L2_LOG_ERROR(
            Log, "Cannot read PledgeMission.txt; no definitions are parsed and the current mission definitions are retained."
        );
        return;
    }

    CParserForPledgeMission parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ParserForPledgeMission.create failed",
        //     L"PledgeMissionInfo.cpp",
        //     0x146)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ParserForPledgeMission.create failed",
        //     L"PledgeMissionInfo.cpp",
        //     0x146)
        L2_LOG_ERROR(Log, "Cannot create the PledgeMission.txt parser; current mission definitions are retained.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = nullptr;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing PledgeMissionData file failed after line[%d]",
        //     L"PledgeMissionInfo.cpp",
        //     0x151,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing PledgeMissionData file failed after line[%d]",
        //     L"PledgeMissionInfo.cpp",
        //     0x151,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "PledgeMission.txt parsing failed at line %lld; definitions already published by the parser are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }

    const std::uint32_t tickAfter = runtime::GetTickCount();
    const std::int64_t memoryAfter = g_Memory.GetTotalAllocSize();
    // Log.Add(
    //     LOG_NORMAL,
    //     "Complete to load PledgeMissionData [%u MB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 20,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    // L2_LOG_INFO(Log,
    //     "Complete to load PledgeMissionData [%u MB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 20,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    L2_LOG_INFO(
        Log,
        "PledgeMission.txt parsing finished in %u ms; %llu mission definitions are currently held. Tracked memory: %lld -> "
        "%lld bytes.",
        static_cast<unsigned>(tickAfter - tickBefore), static_cast<unsigned long long>(mPledgeMissionInfoMap_.size()),
        static_cast<long long>(memoryBefore), static_cast<long long>(memoryAfter)
    );
}
