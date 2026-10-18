#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

class User;

namespace CardUpDownGame {

enum GamePlayState : std::int32_t {
    GPS_LOADING = 0,
    GPS_UPDATE_RECORD = 1,
    GPS_PREPARE_REWARD = 2,
    GPS_DO_REWARD = 3,
    GPS_NOT_PLAY = 4,
    GPS_PREPARE = 5,
    GPS_PLAYING = 6,
};

enum GameQuitReason : std::int32_t {
    GQR_BY_USER = 0,
    GQR_BY_TIMEOUT = 1,
    GQR_BY_NPC_DISTANCE = 2,
    GQR_BY_LEAVE_WORLD = 3,
};

class ConsumeItem {
public:
    ConsumeItem(char16_t* itemName, int amount);

    int m_nItemClassID; // +0x00
    int m_nAmount; // +0x04
};

class RewardItem {
public:
    RewardItem(char16_t* itemName, int amount, double probability);

    int m_nItemClassID; // +0x00
    int m_nAmount; // +0x04
    double m_dProb; // +0x08
};

class GameData {
public:
    GameData();

    RewardItem* PickFixedReward();
    RewardItem* PickRandomReward();
    void SetNpc(char16_t* npcName);
    bool IsInTime(int currentTime);

    int m_nId; // +0x00
    int m_nNpcId; // +0x04
    int m_nStartTime; // +0x08
    int m_nEndTime; // +0x0c
    int m_nTryCount; // +0x10
    int m_nCardCount; // +0x14
    int m_nDuration; // +0x18
    int m_nRewardDuration; // +0x1c
    std::vector<ConsumeItem*> m_vConsumeItem; // +0x20
    std::vector<RewardItem*> m_vFixedReward; // +0x40
    std::vector<RewardItem*> m_vRandomReward; // +0x60
};

struct GameRewardItem {
    std::int32_t m_nItemClassID; // +0x00
    std::int32_t m_alignment0004To0008; // +0x04
    std::int64_t m_nAmount; // +0x08
};

struct GameReward {
    std::int32_t m_nGameId; // +0x00
    std::int32_t m_nFinishTime; // +0x04
    GameRewardItem m_FixedReward; // +0x08
    GameRewardItem m_RandomReward; // +0x18
};

// Physical per-user CardUpDown state at User+0x86b8. The current
// constructor does not initialize m_SelectedCards.
class UserGame {
public:
    UserGame();

    void OnRequestLoad();
    void _OnFail(User& user);
    void OnFailItemMani(GamePlayState state);
    void OnRewardFinish(User& user);
    void _OnSuccess(User& user, GameData* gameData);
    bool CanGetReward();
    void ValidatePrepareState();
    void OnReplyLoad(
        int gameId,
        int finishTime,
        int fixedRewardClassId,
        std::int64_t fixedRewardAmount,
        int randomRewardClassId,
        std::int64_t randomRewardAmount
    );
    void OnReplyUpdate(const unsigned char* packet);
    void QuitGame(User& user, GameQuitReason reason);
    void PickNumber(User& user, int number);
    void OnLeaveWorld(User& user);
    void OnTimer(User& user);

    CYieldLock m_Lock; // +0x00
    std::int32_t m_nGameStartTime; // +0x10
    std::int32_t m_nTalkNpcServerId; // +0x14
    std::int32_t m_nPlayingGameId; // +0x18
    std::int32_t m_nCurrentTry; // +0x1c
    std::int32_t m_nCorrectNumber; // +0x20
    std::array<std::int32_t, 10> m_SelectedCards; // +0x24
    volatile std::int32_t m_nStateChanging; // +0x4c
    volatile std::int32_t m_nStateClearing; // +0x50
    GamePlayState m_GamePlayState; // +0x54
    GameReward m_GameReward; // +0x58
};


class Script : public ::Singleton<Script> {
public:
    Script();
    ~Script() override;

    void AddGameData(GameData* gameData);
    GameData* GetLatestGameData();
    GameData* GetGameData(int gameId);
    void LoadData();

private:
    using GameDataMap = ::std::map<int, GameData*>;

    GameDataMap m_mapGameData; // +0x08
};

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winaccessible-base"
#pragma clang diagnostic ignored "-Wpotentially-evaluated-expression"
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#elif defined(__GNUC__)
#endif


#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#endif

} // namespace CardUpDownGame

static_assert(std::is_polymorphic_v<CardUpDownGame::Script>);
