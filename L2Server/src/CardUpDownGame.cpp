#include "CardUpDownGame.h"
#include "CardUpDownGameParser.h"
#include "DB.h"
#include "NPC.h"
#include "Object.h"
#include "ObjectDB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"

#include <cwchar>
#include <string>

namespace {


void ClearGameReward(CardUpDownGame::GameReward& reward) {
    reward.m_nGameId = 0;
    reward.m_nFinishTime = 0;
    reward.m_FixedReward.m_nItemClassID = 0;
    reward.m_FixedReward.m_nAmount = 0;
    reward.m_RandomReward.m_nItemClassID = 0;
    reward.m_RandomReward.m_nAmount = 0;
}

} // namespace

namespace CardUpDownGame {

ConsumeItem::ConsumeItem(char16_t* itemName, int amount) : m_nItemClassID(-1), m_nAmount(amount) {
    CObject* const object = g_ObjectDB.GetObjectByName(itemName);
    if (object != nullptr && object->IsItem()) {
        m_nItemClassID = object->GetClassId();
        return;
    }

    const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
    // Log.Add(
    //     LOG_ERROR,
    //     L"Undefined Item[%s] In CardUpDownGame Script File.",
    //     hostName.c_str())
    // L2_LOG_ERROR(Log,
    //     L"Undefined Item[%s] In CardUpDownGame Script File.",
    //     hostName.c_str())
    L2_LOG_ERROR(
        Log, "Number card game consumed item '%s' is missing or not an item (amount %d); entry retains class ID -1.",
        itemName != nullptr ? Utf16ToUtf8(target_utf16::FromHostCodeUnits(hostName)).c_str() : "<null>", amount
    );
}

RewardItem::RewardItem(char16_t* itemName, int amount, double probability) :
    m_nItemClassID(-1), m_nAmount(amount), m_dProb(probability) {
    L2_GUARD_FUNCTION();

    CObject* const object = g_ObjectDB.GetObjectByName(itemName);
    if (object != nullptr && object->IsItem()) {
        m_nItemClassID = object->GetClassId();
        return;
    }

    const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
    // Log.Add(
    //     LOG_ERROR,
    //     L"Undefined Item[%s] In CardUpDownGame Script File.",
    //     hostName.c_str())
    // L2_LOG_ERROR(Log,
    //     L"Undefined Item[%s] In CardUpDownGame Script File.",
    //     hostName.c_str())
    L2_LOG_ERROR(
        Log,
        "Number card game reward item '%s' is missing or not an item (amount %d, probability %.17g%%); entry retains class "
        "ID -1.",
        itemName != nullptr ? Utf16ToUtf8(target_utf16::FromHostCodeUnits(hostName)).c_str() : "<null>", amount, probability
    );
}

GameData::GameData() :
    m_nId(0),
    m_nNpcId(0),
    m_nStartTime(0),
    m_nEndTime(0),
    m_nTryCount(0),
    m_nCardCount(0),
    m_nDuration(0),
    m_nRewardDuration(1) {}

UserGame::UserGame() {
    m_nGameStartTime = 0;
    m_nTalkNpcServerId = 0;
    m_nPlayingGameId = 0;
    m_nCurrentTry = 0;
    m_nCorrectNumber = 0;
    // Current C++ construction leaves m_SelectedCards intact.
    m_nStateChanging = 0;
    m_nStateClearing = 0;
    m_GamePlayState = GPS_LOADING;
    ClearGameReward(m_GameReward);
}

void GameData::SetNpc(char16_t* npcName) {
    if (npcName == nullptr) {
        m_nNpcId = -1;
        return;
    }

    CObject* const object = g_ObjectDB.GetObjectByName(npcName);
    if (object != nullptr && object->IsNPC()) {
        m_nNpcId = object->GetClassId();
        return;
    }

    const std::wstring hostName = target_utf16::ToHostCodeUnits(npcName);
    // Log.Add(
    //     LOG_ERROR,
    //     L"Undefined NPC[%s] In CardUpDownGame Script File.",
    //     hostName.c_str())
    // L2_LOG_ERROR(Log,
    //     L"Undefined NPC[%s] In CardUpDownGame Script File.",
    //     hostName.c_str())
    L2_LOG_ERROR(
        Log, "Number card game %d NPC '%s' is missing or not an NPC; previous NPC class ID %d is retained.", m_nId,
        Utf16ToUtf8(target_utf16::FromHostCodeUnits(hostName)).c_str(), m_nNpcId
    );
}

bool GameData::IsInTime(int currentTime) {
    L2_GUARD_FUNCTION();
    return m_nStartTime <= currentTime && currentTime <= m_nEndTime;
}

RewardItem* GameData::PickFixedReward() {
    L2_GUARD_FUNCTION();

    RewardItem* selected = nullptr;
    double cumulativeProbability = 0.0;
    for (RewardItem* const reward : m_vFixedReward) {
        selected = reward;
        const double random = ::genrand64_real1() * 100.0 + 0.0;
        cumulativeProbability += reward->m_dProb;
        if (cumulativeProbability >= random) {
            break;
        }
    }
    return selected;
}

RewardItem* GameData::PickRandomReward() {
    L2_GUARD_FUNCTION();

    for (RewardItem* const reward : m_vRandomReward) {
        if (reward != nullptr && reward->m_dProb >= ::genrand64_real1() * 100.0 + 0.0) {
            return reward;
        }
    }
    return nullptr;
}

Script::Script() {}

Script::~Script() = default;

void Script::AddGameData(GameData* gameData) {
    if (gameData == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid data, data is null",
        //     L"CardUpDownGame.cpp",
        //     0xa8)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid data, data is null",
        //     L"CardUpDownGame.cpp",
        //     0xa8)
        L2_LOG_ERROR(Log, "Cannot register number card game: game data is null; entry ignored.");
        return;
    }

    m_mapGameData.emplace(gameData->m_nId, gameData);
}

GameData* Script::GetLatestGameData() {
    L2_GUARD_FUNCTION();

    const int currentTime = runtime::crt::Time32Now();
    for (const auto& entry : m_mapGameData) {
        if (entry.second->IsInTime(currentTime)) {
            return entry.second;
        }
    }
    return nullptr;
}

GameData* Script::GetGameData(int gameId) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapGameData.find(gameId);
    return found != m_mapGameData.end() ? found->second : nullptr;
}

void Script::LoadData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\cardupdowngame.txt";
#else
    constexpr const wchar_t* kDataPath = L"../script/cardupdowngame.txt";
#endif

    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"CardUpDownGame.cpp",
        //     0xb9)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"CardUpDownGame.cpp",
        //     0xb9)
        L2_LOG_ERROR(Log, "Cannot read cardupdowngame.txt; number card game loading stops.");
        return;
    }

    CParserForCardUpDownGame parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForAlchemy::create failed",
        //     L"CardUpDownGame.cpp",
        //     0xc2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForAlchemy::create failed",
        //     L"CardUpDownGame.cpp",
        //     0xc2)
        L2_LOG_ERROR(Log, "Cannot initialize cardupdowngame.txt parser; number card game loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CParserForCardUpDownGame file failed "
        //     L"after line[%d]",
        //     L"CardUpDownGame.cpp",
        //     0xce,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CParserForCardUpDownGame file failed "
        //     L"after line[%d]",
        //     L"CardUpDownGame.cpp",
        //     0xce,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse cardupdowngame.txt at script line %lld; games registered before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

// CardUpDownGame::UserGame::OnRequestLoad@0x0042ecbc.
void UserGame::OnRequestLoad() {
    L2_GUARD_FUNCTION();
    if (m_GamePlayState != GPS_LOADING) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] OnRequestLoad() invalid state[%d]",
        //     L"CardUpDownGame.cpp",
        //     0x151,
        //     static_cast<int>(m_GamePlayState))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] OnRequestLoad() invalid state[%d]",
        //     L"CardUpDownGame.cpp",
        //     0x151,
        //     static_cast<int>(m_GamePlayState))
        L2_LOG_WARN(
            Log, "Number card game load requested in state %d instead of loading (0); resetting state to loading.",
            static_cast<int>(m_GamePlayState)
        );
        m_GamePlayState = GPS_LOADING;
    }
}

// CardUpDownGame::UserGame::_OnFail@0x0042ed8c.
void UserGame::_OnFail(User& user) {
    L2_GUARD_FUNCTION();
    if (user.IsBuilder()) {
        char16_t message[1024];
        FormatUtf16To(message, std::size(message), u"game fail!");
        user.SendSystemMessage(u"SYS", message);
    }

    m_nCurrentTry = 0;
    m_nCorrectNumber = 0;
    m_nGameStartTime = 0;
    m_GamePlayState = GPS_NOT_PLAY;
    ClearGameReward(m_GameReward);
}

// CardUpDownGame::UserGame::OnFailItemMani@0x0042eebc.
void UserGame::OnFailItemMani(GamePlayState state) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[CardUpDownGame][OnFailItemMani] state[%d]",
    //     static_cast<int>(m_GamePlayState))
    // L2_LOG_ERROR(Log,
    //     L"[CardUpDownGame][OnFailItemMani] state[%d]",
    //     static_cast<int>(m_GamePlayState))
    L2_LOG_ERROR(
        Log, "Number card game %d item operation failed in state %d; restoring state %d.", m_nPlayingGameId,
        static_cast<int>(m_GamePlayState), static_cast<int>(state)
    );
    m_GamePlayState = state;
}

// CardUpDownGame::UserGame::OnRewardFinish@0x0042efa8.
void UserGame::OnRewardFinish(User& user) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_GamePlayState == GPS_DO_REWARD) {
        ClearGameReward(m_GameReward);
        m_nCurrentTry = 0;
        m_nCorrectNumber = 0;
        m_nGameStartTime = 0;
        m_GamePlayState = GPS_NOT_PLAY;
        user.GetSocket()->Send("chc", 0xfe, 0x1d2, 1);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[CardUpDownGame]OnRewardFinish() invalid state[%d]",
    //     static_cast<int>(m_GamePlayState))
    // L2_LOG_ERROR(Log,
    //     L"[CardUpDownGame]OnRewardFinish() invalid state[%d]",
    //     static_cast<int>(m_GamePlayState))
    L2_LOG_ERROR(
        Log,
        "Number card game reward completion ignored for character %u: state %d is not rewarding (3); reward data retained.",
        static_cast<unsigned>(user.GetDBID()), static_cast<int>(m_GamePlayState)
    );
}

// CardUpDownGame::UserGame::_OnSuccess@0x0042f334.
void UserGame::_OnSuccess(User& user, GameData* gameData) {
    L2_GUARD_FUNCTION();
    if (gameData == nullptr) {
        return;
    }

    m_GamePlayState = GPS_UPDATE_RECORD;
    if (user.IsBuilder()) {
        char16_t message[1024];
        FormatUtf16To(message, std::size(message), u"game success!! game id[%d]", gameData->m_nId);
        user.SendSystemMessage(u"SYS", message);
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble("d", gameData->m_nId);
    packet.Assemble("d", runtime::crt::Time32Now());

    RewardItem* const fixedReward = gameData->PickFixedReward();
    RewardItem* const randomReward = gameData->PickRandomReward();
    int fixedClassId = 0;
    std::int64_t fixedAmount = 0;
    int randomClassId = 0;
    std::int64_t randomAmount = 0;
    if (fixedReward != nullptr) {
        fixedClassId = fixedReward->m_nItemClassID;
        fixedAmount = fixedReward->m_nAmount;
    }
    if (randomReward != nullptr) {
        randomClassId = randomReward->m_nItemClassID;
        randomAmount = randomReward->m_nAmount;
    }
    if (fixedReward == nullptr && randomReward == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CardUpDownGame::_OnSuccess invalid reward item")
        // L2_LOG_ERROR(Log,
        //     L"CardUpDownGame::_OnSuccess invalid reward item")
        L2_LOG_ERROR(
            Log,
            "Number card game %d selected neither fixed nor random reward for character %u; record update skipped, state "
            "remains updating record (1).",
            gameData->m_nId, static_cast<unsigned>(user.GetDBID())
        );
        return;
    }

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1fec, &user, nullptr, nullptr, nullptr, nullptr, fixedClassId, fixedAmount, randomClassId, randomAmount, 0, 0, 0
    );

    packet.Assemble("dQdQ", fixedClassId, fixedAmount, randomClassId, randomAmount);
    db.RequestUpdateCardUpdownGameRecord(user, packet);
    user.GetSocket()->Send(
        "chcdQdQ", 0xfe, 0x1d0,
        // 3,
        PRT_SUCCESS, fixedClassId, fixedAmount, randomClassId, randomAmount
    );
}

// CardUpDownGame::UserGame::CanGetReward@0x0042fb38.
bool UserGame::CanGetReward() {
    L2_GUARD_FUNCTION();
    GameData* const gameData = Script::inst().GetGameData(m_GameReward.m_nGameId);
    return gameData != nullptr && runtime::crt::Time32Now() < m_GameReward.m_nFinishTime + gameData->m_nRewardDuration;
}

// CardUpDownGame::UserGame::ValidatePrepareState@0x0042fc04.
void UserGame::ValidatePrepareState() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (CanGetReward()) {
        if (m_GamePlayState == GPS_NOT_PLAY) {
            m_GamePlayState = GPS_PREPARE_REWARD;
        }
        return;
    }

    if (m_GamePlayState == GPS_PREPARE_REWARD) {
        m_GamePlayState = GPS_NOT_PLAY;
        ClearGameReward(m_GameReward);
    }
}

// CardUpDownGame::UserGame::OnReplyLoad@0x0042fdb4.
void UserGame::OnReplyLoad(
    int gameId,
    int finishTime,
    int fixedRewardClassId,
    std::int64_t fixedRewardAmount,
    int randomRewardClassId,
    std::int64_t randomRewardAmount
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_GamePlayState != GPS_LOADING) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] OnReplyLoad() invalid state[%d]",
        //     L"CardUpDownGame.cpp",
        //     300,
        //     static_cast<int>(m_GamePlayState))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] OnReplyLoad() invalid state[%d]",
        //     L"CardUpDownGame.cpp",
        //     300,
        //     static_cast<int>(m_GamePlayState))
        L2_LOG_WARN(
            Log, "Number card game %d DB load reply arrived in state %d instead of loading (0); reply processing continues.",
            gameId, static_cast<int>(m_GamePlayState)
        );
    }

    GameData* const gameData = Script::inst().GetGameData(gameId);
    if (gameData != nullptr) {
        m_GameReward.m_nGameId = gameId;
        m_GameReward.m_nFinishTime = finishTime;
        m_GameReward.m_FixedReward.m_nItemClassID = fixedRewardClassId;
        m_GameReward.m_FixedReward.m_nAmount = fixedRewardAmount;
        m_GameReward.m_RandomReward.m_nItemClassID = randomRewardClassId;
        m_GameReward.m_RandomReward.m_nAmount = randomRewardAmount;
        if (gameId > 0 && CanGetReward()) {
            m_GamePlayState = GPS_PREPARE_REWARD;
            return;
        }
    }
    m_GamePlayState = GPS_NOT_PLAY;
}

// CardUpDownGame::UserGame::OnReplyUpdate@0x0042ff24.
void UserGame::OnReplyUpdate(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_GamePlayState != GPS_UPDATE_RECORD) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] OnReplyUpdate() invalid state[%d]",
        //     L"CardUpDownGame.cpp",
        //     0x160,
        //     static_cast<int>(m_GamePlayState))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] OnReplyUpdate() invalid state[%d]",
        //     L"CardUpDownGame.cpp",
        //     0x160,
        //     static_cast<int>(m_GamePlayState))
        L2_LOG_ERROR(
            Log, "Number card game %d DB update reply ignored: state %d is not updating record (1).", m_nPlayingGameId,
            static_cast<int>(m_GamePlayState)
        );
        return;
    }

    ::Disassemble(
        packet, "dddQdQ", &m_GameReward.m_nGameId, &m_GameReward.m_nFinishTime, &m_GameReward.m_FixedReward.m_nItemClassID,
        &m_GameReward.m_FixedReward.m_nAmount, &m_GameReward.m_RandomReward.m_nItemClassID,
        &m_GameReward.m_RandomReward.m_nAmount
    );
    m_GamePlayState = m_GameReward.m_nGameId > 0 && CanGetReward() ? GPS_PREPARE_REWARD : GPS_NOT_PLAY;
}

// CardUpDownGame::UserGame::QuitGame@0x004300a0.
void UserGame::QuitGame(User& user, GameQuitReason reason) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (reason == GQR_BY_USER) {
        if (CanGetReward()) {
            GameData* const gameData = Script::inst().GetGameData(m_GameReward.m_nGameId);
            if (gameData != nullptr) {
                user.GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::EVENT_REWARD_STORAGE_HOURS_AND_COLLECTION_NOTICE, 1, 1,
                    gameData->m_nRewardDuration / 86400
                );
            }
        }
    } else if (reason == GQR_BY_TIMEOUT) {
        user.SendSystemMessage(SystemMessageId::GAME_ENDED_TIME_EXPIRED);
    } else if (reason == GQR_BY_NPC_DISTANCE) {
        user.SendSystemMessage(SystemMessageId::NUMBER_CARD_GAME_ENDED_EVENT_NPC_TOO_FAR);
    } else if (reason == GQR_BY_LEAVE_WORLD) {
        CUserSocket* const socket = user.GetSocket();
        if (socket != nullptr && socket->GetRaidStatus() != URS_NONE) {
            user.SendSystemMessage(SystemMessageId::NUMBER_CARD_GAME_ENDED_EVENT_NPC_TOO_FAR);
        }
    }

    if (m_GamePlayState != GPS_PLAYING) {
        if (m_GamePlayState != GPS_NOT_PLAY && m_GamePlayState != GPS_PREPARE_REWARD) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] unpredicted game quit, state[%d] action[%d]",
            //     L"CardUpDownGame.cpp",
            //     0x1b7,
            //     static_cast<int>(m_GamePlayState),
            //     static_cast<int>(reason))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] unpredicted game quit, state[%d] action[%d]",
            //     L"CardUpDownGame.cpp",
            //     0x1b7,
            //     static_cast<int>(m_GamePlayState),
            //     static_cast<int>(reason))
            L2_LOG_WARN(
                Log,
                "Number card game %d quit for character %u in unexpected state %d (reason %d); resetting to not playing "
                "(4).",
                m_nPlayingGameId, static_cast<unsigned>(user.GetDBID()), static_cast<int>(m_GamePlayState),
                static_cast<int>(reason)
            );
            m_GamePlayState = GPS_NOT_PLAY;
        }
    } else {
        m_GamePlayState = GPS_NOT_PLAY;
    }

    m_nGameStartTime = 0;
    m_nTalkNpcServerId = 0;
    m_nCurrentTry = 0;
    m_nCorrectNumber = 0;
    user.GetSocket()->Send("chc", 0xfe, 0x1d3, static_cast<int>(static_cast<std::int8_t>(reason)));
}

// CardUpDownGame::UserGame::PickNumber@0x004302d0.
void UserGame::PickNumber(User& user, int number) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_GamePlayState != GPS_PLAYING) {
        return;
    }

    GameData* const gameData = Script::inst().GetGameData(m_nPlayingGameId);
    if (gameData == nullptr) {
        return;
    }

    if (m_nTalkNpcServerId > 0) {
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(m_nTalkNpcServerId));
        CNPC* const npc = object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
        if (npc == nullptr ||
            !TalkChannelCache::inst().mChannels[23].IsValidChannel(
                user.GetServerId(), static_cast<std::uint32_t>(m_nTalkNpcServerId), 0xfa, 0
            ) ||
            npc->GetInZoneID() != user.GetInZoneID()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[CardUpDownGame::PickNumber]NPC too far")
            // L2_LOG_ERROR(Log,
            //     L"[CardUpDownGame::PickNumber]NPC too far")
            L2_LOG_WARN(
                Log,
                "Number card game %d pick ignored for character %u: NPC object %d is unavailable, outside the talk channel "
                "or in another instance.",
                m_nPlayingGameId, static_cast<unsigned>(user.GetDBID()), m_nTalkNpcServerId
            );
            return;
        }
    }

    ++m_nCurrentTry;
    if (m_nCurrentTry > gameData->m_nTryCount) {
        // Log.Add(LOG_ERROR, L"maybe bot...")
        // L2_LOG_ERROR(Log, L"maybe bot...")
        L2_LOG_WARN(
            Log,
            "Number card game %d pick ignored for character %u: attempt %d exceeds limit %d; incremented attempt count is "
            "retained.",
            m_nPlayingGameId, static_cast<unsigned>(user.GetDBID()), m_nCurrentTry, gameData->m_nTryCount
        );
        return;
    }
    m_SelectedCards[static_cast<std::size_t>(m_nCurrentTry)] = number;

    if (user.IsBuilder()) {
        char16_t message[1024];
        FormatUtf16To(
            message, std::size(message),
            u"playing game[%d] correct number[%d] pick[%d] "
            u"try[%d] max try[%d]",
            m_nPlayingGameId, m_nCorrectNumber, number, m_nCurrentTry, gameData->m_nTryCount
        );
        user.SendSystemMessage(u"SYS", message);
    }

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1feb, &user, nullptr, nullptr, nullptr, nullptr, number, m_nCorrectNumber, m_nCurrentTry, 0, 0, 0, 0
    );

    if (number == m_nCorrectNumber) {
        _OnSuccess(user, gameData);
    } else if (m_nCurrentTry == gameData->m_nTryCount) {
        _OnFail(user);
        user.GetSocket()->Send("chc", 0xfe, 0x1d0, 2);
        user.SendSystemMessage(SystemMessageId::GAME_ENDED_ALL_CHANCES_USED);
    } else {
        user.GetSocket()->Send(
            "chc", 0xfe, 0x1d0,
            // number < m_nCorrectNumber ? 0 : 1)
            number < m_nCorrectNumber ? PRT_FAIL_UP : PRT_FAIL_DOWN
        );
        if (user.IsBuilder()) {
            char16_t message[1024];
            FormatUtf16To(
                message, std::size(message), u"Do Next Try! correct number[%d] pick[%d]", m_nCorrectNumber, number
            );
            user.SendSystemMessage(u"SYS", message);
        }
    }
}

// CardUpDownGame::UserGame::OnLeaveWorld@0x004306c4.
void UserGame::OnLeaveWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (Script::inst().GetGameData(m_nPlayingGameId) != nullptr) {
        QuitGame(user, GQR_BY_LEAVE_WORLD);
    }
}

// CardUpDownGame::UserGame::OnTimer@0x00430788.
void UserGame::OnTimer(User& user) {
    L2_GUARD_FUNCTION();
    if (m_GamePlayState != GPS_PLAYING) {
        return;
    }
    GameData* const gameData = Script::inst().GetGameData(m_nPlayingGameId);
    if (gameData == nullptr) {
        return;
    }

    if (m_nTalkNpcServerId > 0) {
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(m_nTalkNpcServerId));
        CNPC* const npc = object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
        if (npc == nullptr ||
            !TalkChannelCache::inst().mChannels[23].IsValidChannel(
                user.GetServerId(), static_cast<std::uint32_t>(m_nTalkNpcServerId), 0xfa, 0
            ) ||
            npc->GetInZoneID() != user.GetInZoneID()) {
            QuitGame(user, GQR_BY_NPC_DISTANCE);
            return;
        }
    }

    if (gameData->m_nDuration <= runtime::crt::Time32Now() - m_nGameStartTime) {
        QuitGame(user, GQR_BY_TIMEOUT);
    }
}

} // namespace CardUpDownGame
