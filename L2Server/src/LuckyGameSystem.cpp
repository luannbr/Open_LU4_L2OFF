#include "LuckyGameSystem.h"
#include "DB.h"
#include "Inventory.h"
#include "ObjectDB.h"
#include "ParserLuckyGame.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

CLuckyGameDB g_CLuckyGameDB;

bool LuckyGameReward::IsModifyTurn(int personalPoint) {
    L2_GUARD_FUNCTION();

    if (personalPoint < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Luckygame] PersonalPoint Error [%d]",
        //     personalPoint)
        // L2_LOG_ERROR(Log,
        //     L"[Luckygame] PersonalPoint Error [%d]",
        //     personalPoint)
        L2_LOG_ERROR(
            Log, "Lucky Game reward table %d has negative personal points %d; modified-reward eligibility returns false.",
            m_nIndex, personalPoint
        );
        return false;
    }

    const int maximum = m_nModify_max - 1;
    if (personalPoint == maximum) {
        return true;
    }

    const int minimum = m_nModify_min - 1;
    const int threshold =
        static_cast<int>(::genrand64_real2() * static_cast<double>((maximum - minimum) + 1) + static_cast<double>(minimum));
    return personalPoint + 1 > threshold;
}

LuckyGameItem* LuckyGameReward::GetCommonReward() {
    L2_GUARD_FUNCTION();

    if (m_vCommonReward.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Luckygame] CommonReward Empty")
        // L2_LOG_ERROR(Log,
        //     L"[Luckygame] CommonReward Empty")
        L2_LOG_ERROR(Log, "Lucky Game reward table %d has no common rewards; selection returns no item.", m_nIndex);
        return nullptr;
    }

    double accumulatedProbability = 0.0;
    const double selectedProbability = ::genrand64_real1() * 100.0 + 0.0;
    for (LuckyGameItem* const item : m_vCommonReward) {
        accumulatedProbability += item->dPer;
        if (selectedProbability < accumulatedProbability) {
            return item;
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[Luckygame] CommonReward Pick Fail")
    // L2_LOG_ERROR(Log,
    //     L"[Luckygame] CommonReward Pick Fail")
    L2_LOG_ERROR(
        Log,
        "Lucky Game common reward selection in table %d missed probability %.17g%% with cumulative %.17g%%; using the first "
        "of %zu entries.",
        m_nIndex, selectedProbability, accumulatedProbability, m_vCommonReward.size()
    );
    return m_vCommonReward.front();
}

LuckyGameItem* LuckyGameReward::GetModifyReward(int personalPoint) {
    L2_GUARD_FUNCTION();

    if (!IsModifyTurn(personalPoint)) {
        return nullptr;
    }
    if (m_vModifyReward.empty()) {
        return nullptr;
    }

    double accumulatedProbability = 0.0;
    const double selectedProbability = ::genrand64_real1() * 100.0 + 0.0;
    for (LuckyGameItem* const item : m_vModifyReward) {
        accumulatedProbability += item->dPer;
        if (selectedProbability < accumulatedProbability) {
            return item;
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[Luckygame] ModifyReward Pick Fail")
    // L2_LOG_ERROR(Log,
    //     L"[Luckygame] ModifyReward Pick Fail")
    L2_LOG_ERROR(
        Log,
        "Lucky Game modified reward selection in table %d missed probability %.17g%% with cumulative %.17g%% (personal "
        "points %d); using the first of %zu entries.",
        m_nIndex, selectedProbability, accumulatedProbability, personalPoint, m_vModifyReward.size()
    );
    return m_vModifyReward.front();
}

void LuckyGameReward::SetTurningPoint(int turningPoint) {
    L2_GUARD_FUNCTION();

    if (turningPoint >= 0) {
        m_nTurningPoint = std::min(turningPoint, 100000000);
    }
}

bool LuckyGameReward::Exists(int turn) {
    L2_GUARD_FUNCTION();
    return m_mapUniqueReward.find(turn) != m_mapUniqueReward.end();
}

LuckyGameItem* LuckyGameReward::GetUniqueReward(int turn) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapUniqueReward.find(turn);
    return found == m_mapUniqueReward.end() ? nullptr : found->second;
}

LuckyGameReward* CLuckyGameDB::GetReward(int index) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapReward.find(index);
    return found == m_mapReward.end() ? nullptr : found->second;
}

int LuckyGameReward::AddUniqueReward(LuckyGameItem* item, std::vector<int>* turns) {
    L2_GUARD_FUNCTION();

    if (item == nullptr) {
        return -1;
    }

    int duplicateTurn = 0;
    if (g_ObjectDB.GetObjectW(item->nItemType, false) != nullptr) {
        for (const int turn : *turns) {
            if (Exists(turn)) {
                duplicateTurn = turn;
            } else {
                m_mapUniqueReward.emplace(turn, item);
            }
        }
    }
    return duplicateTurn;
}

void CLuckyGameDB::AddReward(int index, LuckyGameReward* reward) {
    L2_GUARD_FUNCTION();

    if (reward != nullptr) {
        m_mapReward.emplace(index, reward);
    }
}

void LuckyGameReward::AddCommonReward(LuckyGameItem* item) {
    L2_GUARD_FUNCTION();

    if (item != nullptr) {
        m_vCommonReward.push_back(item);
    }
}

void LuckyGameReward::AddModifyReward(LuckyGameItem* item) {
    L2_GUARD_FUNCTION();

    if (item != nullptr) {
        m_vModifyReward.push_back(item);
    }
}

void CLuckyGameDB::LoadLuckyGamedata() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\luckygamedata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/luckygamedata.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from LuckyGame file failed",
        //     L"LuckyGameSystem.cpp",
        //     0x112)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from LuckyGame file failed",
        //     L"LuckyGameSystem.cpp",
        //     0x112)
        L2_LOG_ERROR(Log, "Cannot read luckygamedata.txt; Lucky Game loading stops.");
        return;
    }

    CParserForLuckyGame parser;
    if (!parser.create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] objectParser.create failed",
        //     L"LuckyGameSystem.cpp",
        //     0x11a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] objectParser.create failed",
        //     L"LuckyGameSystem.cpp",
        //     0x11a)
        L2_LOG_ERROR(Log, "Cannot initialize luckygamedata.txt parser; Lucky Game loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing LuckyGame file failed after line[%d]",
        //     L"LuckyGameSystem.cpp",
        //     0x126,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing LuckyGame file failed after line[%d]",
        //     L"LuckyGameSystem.cpp",
        //     0x126,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse luckygamedata.txt at script line %lld; reward data registered before the error is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

CLuckyGameSystem::CLuckyGameSystem() :
    m_nCommonGameRewardIndex(0),
    m_nPremiumGameRewardIndex(0),
    m_nCommonTurnCnt(1),
    m_nPremiumTurnCnt(1),
    m_nCommonRevCount(0),
    m_nPremiumRevCount(0),
    m_nPickLuckyGameFlag(0) {}

CLuckyGameSystem::~CLuckyGameSystem() = default;

CLuckyGameSystem* CLuckyGameSystem::Inst() {
    // Current L2Server uses one function-local, process-lifetime instance.
    static CLuckyGameSystem system;
    return &system;
}

// Current L2Server 0x0057820c.
L2_DEFINE_OBJECT_RELEASE(CLuckyGameSystem)

bool CLuckyGameSystem::IsClosingLuckyGame(const LuckyGameSystem::BettingType type) const noexcept {
    if (type == LuckyGameSystem::BT_COMMON) {
        // return m_nCommonRevCount == -2.
        return m_nCommonRevCount == LuckyGameSystem::RCT_CLOSING;
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        // return m_nPremiumRevCount == -2.
        return m_nPremiumRevCount == LuckyGameSystem::RCT_CLOSING;
    }
    return false;
}

bool CLuckyGameSystem::AcquirePickLuckyGameFlag() noexcept {
    return runtime::raw_atomic::CompareExchangeI32(&m_nPickLuckyGameFlag, 1, 0) == 0;
}

void CLuckyGameSystem::ReleasePickLuckyGameFlag() noexcept { runtime::raw_atomic::ExchangeI32(&m_nPickLuckyGameFlag, 0); }

bool CLuckyGameSystem::IsAcquiredPickLuckyGameFlag() const noexcept {
    return runtime::raw_atomic::LoadI32Relaxed(&m_nPickLuckyGameFlag) != 0;
}

void CLuckyGameSystem::IncreaseCommonTurnCnt() {
    // Current L2Server 0x0081650c.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_CommonTurnLock);
        ++m_nCommonTurnCnt;
    }
}

void CLuckyGameSystem::IncreasePremiumTurnCnt() {
    // Current L2Server 0x008165cc.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_PremiumTurnLock);
        ++m_nPremiumTurnCnt;
    }
}

bool CLuckyGameSystem::CheckRevCount(const LuckyGameSystem::BettingType type, LuckyGameReward* const reward) {
    // Current L2Server 0x0081668c returns true for an unknown type.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        return m_nCommonRevCount + 1 < reward->GetRevCount();
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        return m_nPremiumRevCount + 1 < reward->GetRevCount();
    }
    return true;
}

void CLuckyGameSystem::IncreaseCurrentRevCount(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x0081675c.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        ++m_nCommonRevCount;
        db.RequestSetEventScore(
            0x14,
            // -3,
            LuckyGameSystem::LFT_COMMON_REV, 0, m_nCommonRevCount
        );
        return;
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        ++m_nPremiumRevCount;
        db.RequestSetEventScore(
            0x14,
            // -4,
            LuckyGameSystem::LFT_PREMIUM_REV, 0, m_nPremiumRevCount
        );
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x1e8,
    //     static_cast<int>(type))
    // L2_LOG_ERROR(Log,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x1e8,
    //     static_cast<int>(type))
    L2_LOG_ERROR(
        Log, "Lucky Game cannot increment cycle count: betting type %d is unsupported; no count or DB update performed.",
        static_cast<int>(type)
    );
}

void CLuckyGameSystem::ClosingLuckyGame(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x0081686c.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        // m_nCommonRevCount = -2.
        m_nCommonRevCount = LuckyGameSystem::RCT_CLOSING;
    } else if (type == LuckyGameSystem::BT_PREMIUM) {
        // m_nPremiumRevCount = -2.
        m_nPremiumRevCount = LuckyGameSystem::RCT_CLOSING;
    }
}

int CLuckyGameSystem::GetRevCount(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x00816924.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        return m_nCommonRevCount;
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        return m_nPremiumRevCount;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x217,
    //     static_cast<int>(type))
    // L2_LOG_ERROR(Log,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x217,
    //     static_cast<int>(type))
    L2_LOG_ERROR(
        Log, "Lucky Game cannot read cycle count: betting type %d is unsupported; returning -3.", static_cast<int>(type)
    );
    // return -3.
    return LuckyGameSystem::RCT_MAX;
}

void CLuckyGameSystem::SetCommonTurnCnt(const int turn) {
    // Current L2Server 0x00816a08.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][Luckygame] common turning point (%d) ",
    //     turn)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][Luckygame] common turning point (%d) ",
    //     turn)
    L2_LOG_INFO(Log, "Setting Lucky Game common turn counter to %d.", turn);
    {
        L2_WRITE_GUARD(m_CommonTurnLock);
        m_nCommonTurnCnt = turn;
    }
}

void CLuckyGameSystem::SetPremiumTurnCnt(const int turn) {
    // Current L2Server 0x00816ae4.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][Luckygame] premium turning point (%d) ",
    //     turn)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][Luckygame] premium turning point (%d) ",
    //     turn)
    L2_LOG_INFO(Log, "Setting Lucky Game premium turn counter to %d.", turn);
    {
        L2_WRITE_GUARD(m_PremiumTurnLock);
        m_nPremiumTurnCnt = turn;
    }
}

int CLuckyGameSystem::GetCommonTurnCnt() {
    // Current L2Server 0x00816bc0.
    L2_GUARD_FUNCTION();
    const int result = [&] {
        L2_READ_GUARD(m_CommonTurnLock);
        const int result = m_nCommonTurnCnt;
        return result;
    }();
    return result;
}

int CLuckyGameSystem::GetPremiumTurnCnt() {
    // Current L2Server 0x00816c80.
    L2_GUARD_FUNCTION();
    const int result = [&] {
        L2_READ_GUARD(m_PremiumTurnLock);
        const int result = m_nPremiumTurnCnt;
        return result;
    }();
    return result;
}

void CLuckyGameSystem::LoadFromDB(const int index, const int value) {
    // Current L2Server 0x00816d40.
    L2_GUARD_FUNCTION();
    // if (index == -4) {
    if (index == LuckyGameSystem::LFT_PREMIUM_REV) {
        m_nPremiumRevCount = value;
        // } else if (index == -3) {
    } else if (index == LuckyGameSystem::LFT_COMMON_REV) {
        m_nCommonRevCount = value;
        // } else if (index == -2) {
    } else if (index == LuckyGameSystem::LFT_PREMIUM_PICK) {
        SetPremiumTurnCnt(value);
        // } else if (index == -1) {
    } else if (index == LuckyGameSystem::LFT_COMMON_PICK) {
        SetCommonTurnCnt(value);
    }
}

int CLuckyGameSystem::GetRewardIndex(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x00816e28.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        return m_nCommonGameRewardIndex;
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        return m_nPremiumGameRewardIndex;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x2e0,
    //     static_cast<int>(type))
    // L2_LOG_ERROR(Log,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x2e0,
    //     static_cast<int>(type))
    L2_LOG_ERROR(
        Log, "Lucky Game cannot look up reward table: betting type %d is unsupported; returning -1.", static_cast<int>(type)
    );
    return -1;
}

int CLuckyGameSystem::GetTarotCardId(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x00816f04.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        return 0x5cd7;
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        return 0x5cd8;
    }
    return -1;
}

void CLuckyGameSystem::IncreaseCurrentTurnCnt(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x008172e0.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        IncreaseCommonTurnCnt();
    } else if (type == LuckyGameSystem::BT_PREMIUM) {
        IncreasePremiumTurnCnt();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x18c,
        //     static_cast<int>(type))
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x18c,
        //     static_cast<int>(type))
        L2_LOG_ERROR(
            Log, "Lucky Game cannot increment turn counter: betting type %d is unsupported; counter unchanged.",
            static_cast<int>(type)
        );
    }
}

void CLuckyGameSystem::SetCurrentTurnCnt(const LuckyGameSystem::BettingType type, const int turn) {
    // Current L2Server 0x008173c4.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        SetCommonTurnCnt(turn);
    } else if (type == LuckyGameSystem::BT_PREMIUM) {
        SetPremiumTurnCnt(turn);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x24e,
        //     static_cast<int>(type))
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x24e,
        //     static_cast<int>(type))
        L2_LOG_ERROR(
            Log, "Lucky Game cannot set turn counter to %d: betting type %d is unsupported; counter unchanged.", turn,
            static_cast<int>(type)
        );
    }
}

int CLuckyGameSystem::GetCurrentTurnCnt(const LuckyGameSystem::BettingType type) {
    // Current L2Server 0x008174c0.
    L2_GUARD_FUNCTION();
    if (type == LuckyGameSystem::BT_COMMON) {
        return GetCommonTurnCnt();
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        return GetPremiumTurnCnt();
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x286,
    //     static_cast<int>(type))
    // L2_LOG_ERROR(Log,
    //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
    //     L"LuckyGameSystem.cpp",
    //     0x286,
    //     static_cast<int>(type))
    L2_LOG_ERROR(
        Log, "Lucky Game cannot read turn counter: betting type %d is unsupported; returning -1.", static_cast<int>(type)
    );
    return -1;
}

bool CLuckyGameSystem::CheckTurningPoint(const LuckyGameSystem::BettingType type, const int turningPoint) {
    // Current L2Server 0x008175a8.
    L2_GUARD_FUNCTION();
    if (turningPoint < 100000001) {
        const int currentTurn = GetCurrentTurnCnt(type);
        if (turningPoint <= currentTurn) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][Luckygame][type %d][%d] Turning Point!! ",
            //     static_cast<int>(type),
            //     currentTurn)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][Luckygame][type %d][%d] Turning Point!! ",
            //     static_cast<int>(type),
            //     currentTurn)
            L2_LOG_INFO(
                Log, "Lucky Game betting type %d reached turn threshold %d at turn %d; requesting counter reset to 1.",
                static_cast<int>(type), turningPoint, currentTurn
            );
            SetCurrentTurnCnt(type, 1);
            return true;
        }
        IncreaseCurrentTurnCnt(type);
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ERROR][Luckygame] TurningPoint is Too Big [%d]",
    //     turningPoint)
    // L2_LOG_ERROR(Log,
    //     L"[ERROR][Luckygame] TurningPoint is Too Big [%d]",
    //     turningPoint)
    L2_LOG_ERROR(
        Log,
        "Lucky Game betting type %d has turn threshold %d above 100000000; threshold check returns false without advancing "
        "the counter.",
        static_cast<int>(type), turningPoint
    );
    return false;
}

void CLuckyGameSystem::ScreenNoticeUniqueReward(
    User* const user, const LuckyGameSystem::BettingType type, const int itemClassId, const int amount
) {
    // Current L2Server 0x008176cc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (type == LuckyGameSystem::BT_COMMON) {
        CUserBroadcaster::inst().BroadcastToAllUser(
            "chccScdcd", 0x62, 0x17fd, 3, 0, user->GetName(), 3, itemClassId, 1, amount
        );
        return;
    }
    if (type == LuckyGameSystem::BT_PREMIUM) {
        CUserBroadcaster::inst().BroadcastToAllUser(
            "chccScdcd", 0x62, 0x17fe, 3, 0, user->GetName(), 3, itemClassId, 1, amount
        );
    }
}

LuckyGameReward* CLuckyGameSystem::GetReward(const int index) {
    // Current L2Server 0x00817ad8.
    L2_GUARD_FUNCTION();
    return g_CLuckyGameDB.GetReward(index);
}

int CLuckyGameSystem::GetPersonalPoint(const User* const user, const int index) {
    // Current L2Server 0x00817d6c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return -1;
    }
    const std::CDualKey<int, int> key(static_cast<int>(user->GetDBID()), index);
    const auto found = m_mapPersonalPoint.find(key);
    if (found == m_mapPersonalPoint.end()) {
        m_mapPersonalPoint.emplace(key, 0);
        return 0;
    }
    return found->second;
}

bool CLuckyGameSystem::SetPersonalPoint(
    const User* const user, const int index, const int point, const bool loadFromDatabase
) {
    // Current L2Server 0x00817e98.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    const std::CDualKey<int, int> key(static_cast<int>(user->GetDBID()), index);
    const auto found = m_mapPersonalPoint.find(key);
    if (found == m_mapPersonalPoint.end()) {
        m_mapPersonalPoint.emplace(key, point);
    } else {
        found->second = point;
    }
    if (!loadFromDatabase) {
        db.RequestSetEventScore(0x14, index, static_cast<int>(user->GetDBID()), point);
    }
    return true;
}

LuckyGameItem* CLuckyGameSystem::PickLuckyGameReward(
    User* const user, const LuckyGameSystem::BettingType type, LuckyGameSystem::LuckyRewardType& rewardType
) {
    // Current L2Server 0x008185c4.
    L2_GUARD_FUNCTION();
    const int rewardIndex = GetRewardIndex(type);
    if (rewardIndex == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x2fb,
        //     static_cast<int>(type))
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x2fb,
        //     static_cast<int>(type))
        L2_LOG_ERROR(
            Log, "Lucky Game reward selection stopped for betting type %d: reward table index is -1.", static_cast<int>(type)
        );
        return nullptr;
    }

    LuckyGameReward* const reward = GetReward(rewardIndex);
    if (reward == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][Luckygame][%d] undefined Index [%d]",
        //     static_cast<int>(type),
        //     rewardIndex)
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][Luckygame][%d] undefined Index [%d]",
        //     static_cast<int>(type),
        //     rewardIndex)
        L2_LOG_ERROR(
            Log, "Lucky Game reward table %d is not registered for betting type %d; selection returns no item.", rewardIndex,
            static_cast<int>(type)
        );
        return nullptr;
    }

    int personalPoint = GetPersonalPoint(user, rewardIndex);
    LuckyGameItem* item = reward->GetUniqueReward(GetCurrentTurnCnt(type));
    if (item != nullptr) {
        rewardType = LuckyGameSystem::LRT_UNIQUE;
        if (!reward->IsModifyEmpty()) {
            ++personalPoint;
            SetPersonalPoint(user, rewardIndex, personalPoint, false);
        }
    } else {
        item = reward->GetModifyReward(personalPoint);
        if (item != nullptr) {
            rewardType = LuckyGameSystem::LRT_MODIFY;
            personalPoint = 0;
            SetPersonalPoint(user, rewardIndex, personalPoint, false);
        } else {
            item = reward->GetCommonReward();
            if (item == nullptr) {
                return nullptr;
            }
            rewardType = LuckyGameSystem::LRT_COMMON;
            if (!reward->IsModifyEmpty()) {
                ++personalPoint;
                SetPersonalPoint(user, rewardIndex, personalPoint, false);
            }
        }
    }

    if (CheckTurningPoint(type, reward->GetTurningPoint())) {
        if (CheckRevCount(type, reward)) {
            IncreaseCurrentRevCount(type);
            // } else if (GetRevCount(type) != -1) {
        } else if (GetRevCount(type) != LuckyGameSystem::RCT_INFINITY) {
            ClosingLuckyGame(type);
        }
    }
    return item;
}

bool CLuckyGameSystem::GetLuckyGameResult(
    User* const user, const LuckyGameSystem::BettingType type, const int count, int& result
) {
    // Current L2Server 0x00818820. The final reference is present in the
    // source ABI but the current body never reads or writes it.
    L2_GUARD_FUNCTION();
    static_cast<void>(result);
    if (user == nullptr || user->GetSocket() == nullptr) {
        return false;
    }

    const int rewardIndex = GetRewardIndex(type);
    if (rewardIndex == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x362,
        //     static_cast<int>(type))
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x362,
        //     static_cast<int>(type))
        L2_LOG_ERROR(
            Log, "Lucky Game request for character %u stopped: betting type %d has reward table index -1.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(type)
        );
        return false;
    }
    const int tarotCardId = GetTarotCardId(type);
    if (tarotCardId == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x36a,
        //     static_cast<int>(type))
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"LuckyGameSystem.cpp",
        //     0x36a,
        //     static_cast<int>(type))
        L2_LOG_ERROR(
            Log, "Lucky Game request for character %u stopped: betting type %d has no tarot-card item ID.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(type)
        );
        return false;
    }

    using PickedReward = std::pair<LuckyGameSystem::LuckyRewardType, LuckyGameItem>;
    std::vector<PickedReward> pickedRewards;
    ItemAmountDataStruct addableItems;
    std::array<ItemManip, 100> requests{};
    const int personalPointBefore = GetPersonalPoint(user, rewardIndex);

    for (int pickIndex = 0; pickIndex < count; ++pickIndex) {
        LuckyGameSystem::LuckyRewardType rewardType = LuckyGameSystem::LRT_NONE;
        LuckyGameItem* const item = PickLuckyGameReward(user, type, rewardType);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ERROR][LuckyGame] Pick Reward fail index=%d, GameType=%d [%d/%d]",
            //     rewardIndex,
            //     static_cast<int>(type),
            //     pickIndex + 1,
            //     count)
            // L2_LOG_ERROR(Log,
            //     L"[ERROR][LuckyGame] Pick Reward fail index=%d, GameType=%d [%d/%d]",
            //     rewardIndex,
            //     static_cast<int>(type),
            //     pickIndex + 1,
            //     count)
            L2_LOG_ERROR(
                Log,
                "Lucky Game reward pick %lld of %d failed for character %u (betting type %d, table %d); remaining picks "
                "continue.",
                static_cast<long long>(pickIndex) + 1, count, static_cast<unsigned>(user->GetDBID()), static_cast<int>(type),
                rewardIndex
            );
            continue;
        }

        addableItems.Push(item->nItemType, item->nAmount);
        if (!user->m_inv.CheckAddableMulti(addableItems, false)) {
            user->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
            break;
        }
        pickedRewards.emplace_back(rewardType, *item);
    }

    int requestCount = 0;
    for (const PickedReward& picked : pickedRewards) {
        requests[requestCount++] = ItemManip(0, picked.second.nItemType, picked.second.nAmount, 0, 0, 0, true);
    }
    requests[requestCount++] = ItemManip(0, tarotCardId, -static_cast<std::int64_t>(pickedRewards.size()), 0, 0, 0, true);

    bool handledImmediately = false;
    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> etcPacket;
    User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_UNDEFINED_ITEM, 0, requestCount, requests.data(), nullptr, 0, etcPacket, false,
        ItemGetReason::Etc, message
    );

    const User::ManiItemResultType queueResult = user->_SetManiItemJob(queue);
    if (queueResult == User::MRT_NOJOB) {
        if (!user->_HandleItemManipulation(queue)) {
            user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
        } else {
            handledImmediately = true;
        }
    } else if (queueResult == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    } else if (queueResult == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"LuckyGameSystem.cpp",
        //     0x3d7)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"LuckyGameSystem.cpp",
        //     0x3d7)
        // Noise: Normal item-job queue insertion waits behind existing work. It does not indicate an error or completed
        // reward delivery.
    } else {
        user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"LuckyGameSystem.cpp",
        //     0x3dd)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"LuckyGameSystem.cpp",
        //     0x3dd)
        L2_LOG_ERROR(
            Log,
            "Lucky Game item operation failed for character %u: queue result %d (betting type %d, %d operations); request "
            "returns false.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(queueResult), static_cast<int>(type), requestCount
        );
        return false;
    }

    if (!handledImmediately) {
        return false;
    }

    const int pickedCount = static_cast<int>(pickedRewards.size());
    const int remainingTarotCards = static_cast<int>(user->m_inv.GetItemAmount(tarotCardId, 0, true)) - pickedCount;
    std::array<char, 0x5000> resultPacket{};
    int resultPacketLength = 0;
    for (const PickedReward& picked : pickedRewards) {
        const int assembled = ::Assemble(
            resultPacket.data() + resultPacketLength, static_cast<int>(resultPacket.size()) - resultPacketLength, "ddd",
            static_cast<int>(picked.first), picked.second.nItemType, picked.second.nAmount
        );
        if (assembled > 0) {
            resultPacketLength += assembled;
        }

        if (picked.first == LuckyGameSystem::LRT_UNIQUE) {
            ScreenNoticeUniqueReward(user, type, picked.second.nItemType, picked.second.nAmount);
        }

        const FVector position = user->GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        const auto accountEnd = std::find(user->m_sAccountName.begin(), user->m_sAccountName.end(), char16_t{});
        const std::wstring accountName = Utf16ToWide(
            std::u16string_view(
                user->m_sAccountName.data(), static_cast<std::size_t>(accountEnd - user->m_sAccountName.begin())
            )
        );
#if defined(_WIN32)
        constexpr const wchar_t* kLuckyGameLogFormat =
            L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%s,%s,,,";
#else
        constexpr const wchar_t* kLuckyGameLogFormat =
            L"%lld,%lld,%lld,,,%lld,%lld,%lld,,,,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%ls,%ls,,,";
#endif
        // Log.Add(
        //     LOG_BOTH,
        //     kLuckyGameLogFormat,
        //     static_cast<long long>(0x233b),
        //     static_cast<long long>(user->GetDBID()),
        //     static_cast<long long>(user->GetAccountID()),
        //     static_cast<long long>(x),
        //     static_cast<long long>(y),
        //     static_cast<long long>(z),
        //     static_cast<long long>(user->m_data->m_Race),
        //     static_cast<long long>(type),
        //     static_cast<long long>(user->m_data->m_nClass),
        //     static_cast<long long>(user->m_data->m_nLevel),
        //     static_cast<long long>(picked.second.nItemType),
        //     static_cast<long long>(picked.second.nAmount),
        //     static_cast<long long>(picked.first),
        //     static_cast<long long>(personalPointBefore),
        //     static_cast<long long>(GetCurrentTurnCnt(
        //         LuckyGameSystem::BT_COMMON)),
        //     static_cast<long long>(GetCurrentTurnCnt(
        //         LuckyGameSystem::BT_PREMIUM)),
        //     user->GetName(),
        //     accountName.c_str())
        Log.Add(
            LOG_BOTH, kLuckyGameLogFormat, static_cast<long long>(0x233b), static_cast<long long>(user->GetDBID()),
            static_cast<long long>(user->GetAccountID()), static_cast<long long>(x), static_cast<long long>(y),
            static_cast<long long>(z), static_cast<long long>(user->m_data->m_Race), static_cast<long long>(type),
            static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_nLevel),
            static_cast<long long>(picked.second.nItemType), static_cast<long long>(picked.second.nAmount),
            static_cast<long long>(picked.first), static_cast<long long>(personalPointBefore),
            static_cast<long long>(GetCurrentTurnCnt(LuckyGameSystem::BT_COMMON)),
            static_cast<long long>(GetCurrentTurnCnt(LuckyGameSystem::BT_PREMIUM)), user->GetName(), accountName.c_str()
        );
    }

    const int currentTurn = GetCurrentTurnCnt(type);
    if (type == LuckyGameSystem::BT_COMMON) {
        // db.RequestSetEventScore(0x14, -1, 0, currentTurn)
        db.RequestSetEventScore(0x14, LuckyGameSystem::LFT_COMMON_PICK, 0, currentTurn);
    } else if (type == LuckyGameSystem::BT_PREMIUM) {
        // db.RequestSetEventScore(0x14, -2, 0, currentTurn)
        db.RequestSetEventScore(0x14, LuckyGameSystem::LFT_PREMIUM_PICK, 0, currentTurn);
    }

    user->GetSocket()->Send(
        "chddddb", 0xfe, 0x161, 1, static_cast<int>(type), remainingTarotCards, pickedCount, resultPacketLength,
        resultPacket.data()
    );
    user->SendSuccessLuckyGame(static_cast<int>(type), pickedCount);
    return true;
}
