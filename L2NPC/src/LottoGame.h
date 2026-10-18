#pragma once

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

class CFHTML;

namespace l2npc {

// Exact current 0x40-byte CLottoRound layout. The member spellings come from
// the matching NASC64_2015 PDB and every current EXE access confirms that the
// later image kept their order and offsets.
struct alignas(8) CLottoRound {
    CLottoRound() = default;

    bool IsValid();
    const wchar_t* GetStateString();
    std::int64_t GetTotalReward();
    std::int64_t PreWinner1Reward();
    std::int64_t Winner1Reward();
    std::int64_t Winner2Reward();
    std::int64_t Winner3Reward();
    std::int64_t TotalWinner4Reward();
    std::int64_t Winner4Reward();
    std::int64_t Winner1Carry();
    std::int64_t Winner2Carry();
    std::int64_t Winner3Carry();
    std::int64_t TotalCarry();
    bool IsCanceled();

    int RoundNumber = 0;
    int State = 0;
    int LeftTime = 0;
    int ChosenNumber = 0;
    int RuleNumber = 0;
    int TotalCount = 0;
    int Winner1Count = 0;
    int Winner2Count = 0;
    int Winner3Count = 0;
    int Winner4Count = 0;
    std::int64_t CarriedAdena = 0;
    int PresetReward = 0;
    bool DrawingSuccessed = false;
    bool StateFirstTime = false;
    int LastTime = 0;
};


class CLottoGameManager : public CIOObject {
public:
    CLottoGameManager() = default;

    void Initialize();

    void AddLottoRoundFromDB(const CLottoRound& round);
    void WaitForLoading(int expectedRoundCount);

    void Update(int roundNumber, int totalCount);
    void UpdateDrawingState(int roundNumber, int winner1Count, int winner2Count, int winner3Count, int winner4Count);
    std::int64_t CalculateReward(int roundNumber, int numberFlag, int& rank);
    void MakeFinalResultFHTML(CFHTML* html);

    // Exact GeneralEventPacket(1, ...) administrative entry points. The
    // requester id is kept even for the two setters that
    // ignore it in the shipping target.
    void CancelRound(int requesterId, int roundNumber);
    void ShowRoundInfo(int requesterId, int roundNumber);
    void SetPresetReward(int requesterId, std::int64_t reward);
    void SetNumberFlag(int requesterId, int numberFlag);
    void SetDrawingTime(int requesterId, int weekday, int hour);

    void TimerExpired(int timerId) override;

    bool IsInitialized() { return Initialized; }
    bool IsPaused() { return Pause; }
    std::u16string CastLottoNumber(int numberFlag);
    int GetAccumulatedReward();
    std::u16string GetChooseTime();
    int GetChosenNumber();
    std::u16string GetChosenNumberString();
    CLottoRound& GetCurRound();
    CLottoRound& GetRound(int roundNumber);
    int GetLeftTime() { return GetCurRound().LeftTime; }
    int GetRoundNumber() { return CurRound; }
    int GetState(int roundNumber);
    int GetTopState();

    void Drawing();
    void SaveTopRound();
    void StartNextRound();

protected:
private:
    void ResetPresetReward();
    void SetPresetRewardProfile(std::int64_t reward);

protected:
    bool Initialized = false; // +0x18
    std::map<int, CLottoRound> m_LottoRoundMap; // +0x20
    std::set<int> m_LottoNpcs; // +0x40
    mutable CIOCriticalSection m_Sect; // +0x60
    int CurRound = 0; // +0x88
    int m_AnnounceDay = 6; // +0x8c
    int m_AnnounceHour = 19; // +0x90
    std::int64_t m_PresetReward = 0; // +0x98

public:
    bool Pause = false; // +0xa0
};


// Exact process globals recovered from LottoGame.cpp/CConfig.
extern CLottoGameManager g_LottoGameManager;
extern int LOTTO_TICKET_PRICE;
extern int LOTTO_MINIMUM_WINNER_PRICE;
extern bool g_bCanLotto;

inline bool CLottoRound::IsValid() { return RoundNumber > 0; }

inline const wchar_t* CLottoRound::GetStateString() {
    switch (State) {
        case -1:
            return L"Canceled";
        case 0:
            return L"Disabled";
        case 1:
            return L"Selling";
        case 2:
            return L"Choosing";
        case 3:
            return L"Announcing";
        default:
            return L"Not defined";
    }
}

inline std::int64_t CLottoRound::GetTotalReward() {
    const int sales = runtime::MultiplyInt32Wrapping(TotalCount, LOTTO_TICKET_PRICE);
    std::int64_t reward = runtime::SubtractInt64Wrapping(
        CarriedAdena, runtime::Win64CvttDoubleToInt64(static_cast<double>(sales) * -0.5)
    );
    if (LOTTO_MINIMUM_WINNER_PRICE > 0 && reward < LOTTO_MINIMUM_WINNER_PRICE) {
        reward = LOTTO_MINIMUM_WINNER_PRICE;
    }
    return reward;
}

inline std::int64_t CLottoRound::PreWinner1Reward() {
    return runtime::SubtractInt64Wrapping(GetTotalReward(), TotalWinner4Reward());
}

inline std::int64_t CLottoRound::Winner1Reward() {
    if (Winner1Count == 0) {
        return 0;
    }
    std::int64_t reward = runtime::Win64CvttDoubleToInt64(
        static_cast<double>(PreWinner1Reward()) * 0.6 / static_cast<double>(Winner1Count)
    );
    if (LOTTO_MINIMUM_WINNER_PRICE > 0 && reward < LOTTO_MINIMUM_WINNER_PRICE) {
        reward = LOTTO_MINIMUM_WINNER_PRICE;
    }
    return reward;
}

inline std::int64_t CLottoRound::Winner2Reward() {
    if (Winner2Count == 0) {
        return 0;
    }
    return runtime::Win64CvttDoubleToInt32(
        static_cast<double>(PreWinner1Reward()) * 0.2 / static_cast<double>(Winner2Count)
    );
}

inline std::int64_t CLottoRound::Winner3Reward() {
    if (Winner3Count == 0) {
        return 0;
    }
    return runtime::Win64CvttDoubleToInt64(
        static_cast<double>(PreWinner1Reward()) * 0.2 / static_cast<double>(Winner3Count)
    );
}

inline std::int64_t CLottoRound::TotalWinner4Reward() { return runtime::AddInt32Wrapping(Winner4Count, Winner4Count); }

inline std::int64_t CLottoRound::Winner4Reward() { return 2; }

inline std::int64_t CLottoRound::Winner1Carry() {
    if (Winner1Count != 0) {
        return 0;
    }
    std::int64_t carry = runtime::Win64CvttDoubleToInt64(static_cast<double>(PreWinner1Reward()) * 0.6);
    if (LOTTO_MINIMUM_WINNER_PRICE > 0 && carry < LOTTO_MINIMUM_WINNER_PRICE) {
        carry = LOTTO_MINIMUM_WINNER_PRICE;
    }
    return carry;
}

inline std::int64_t CLottoRound::Winner2Carry() {
    if (Winner2Count != 0) {
        return 0;
    }
    return runtime::Win64CvttDoubleToInt64(static_cast<double>(PreWinner1Reward()) * 0.2);
}

inline std::int64_t CLottoRound::Winner3Carry() {
    if (Winner3Count != 0) {
        return 0;
    }
    return runtime::Win64CvttDoubleToInt64(static_cast<double>(PreWinner1Reward()) * 0.2);
}

inline std::int64_t CLottoRound::TotalCarry() {
    return runtime::AddInt64Wrapping(runtime::AddInt64Wrapping(Winner1Carry(), Winner2Carry()), Winner3Carry());
}

inline bool CLottoRound::IsCanceled() { return State == -1; }

const wchar_t* IntToWDay(int weekday);

} // namespace l2npc
