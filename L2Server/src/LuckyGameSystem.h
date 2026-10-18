#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/IOC.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include "L2Core/src/inc/XStd.h"

#include <array>
#include <cstddef>
#include <cstdint>

// One reward item created by parser_luckygame. The current L2Server
// parser allocates exactly 0x10 bytes, clears the full object, and then writes
// the item type, amount, and probability at +0x00, +0x04, and +0x08.
struct LuckyGameItem {
    LuckyGameItem() : nItemType(0), nAmount(0), dPer(0.0) {}

    int nItemType; // +0x00
    int nAmount; // +0x04
    double dPer; // +0x08
};


class CParserForLuckyGame;
class CLuckyGameSystem;
class User;

class LuckyGameReward {
public:
    explicit LuckyGameReward(int index);

    bool Exists(int turn);
    void AddCommonReward(LuckyGameItem* item);
    int AddUniqueReward(LuckyGameItem* item, std::vector<int>* turns);
    void AddModifyReward(LuckyGameItem* item);
    bool IsModifyTurn(int personalPoint);
    LuckyGameItem* GetCommonReward();
    LuckyGameItem* GetUniqueReward(int turn);
    LuckyGameItem* GetModifyReward(int personalPoint);
    int GetTurningPoint() { return m_nTurningPoint; }
    void SetTurningPoint(int turningPoint);
    int GetRevCount() { return m_nRevCount; }
    bool IsModifyEmpty() { return m_vModifyReward.empty(); }

private:
    friend class CParserForLuckyGame;
    friend class CLuckyGameSystem;

    int m_nIndex; // +0x00
    int m_nTurningPoint; // +0x04
    int m_nRevCount; // +0x08
    int m_nModify_min; // +0x0c
    int m_nModify_max; // +0x10
    std::vector<LuckyGameItem*> m_vCommonReward; // +0x18
    std::map<int, LuckyGameItem*> m_mapUniqueReward; // +0x38
    std::vector<LuckyGameItem*> m_vModifyReward; // +0x58
};


class CLuckyGameDB {
public:
    CLuckyGameDB() = default;
    ~CLuckyGameDB() = default;

    void LoadLuckyGamedata();
    LuckyGameReward* GetReward(int index);
    void AddReward(int index, LuckyGameReward* reward);

private:
    std::map<int, LuckyGameReward*> m_mapReward; // +0x00
};


extern CLuckyGameDB g_CLuckyGameDB;


class CLuckyGameSystem final : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CLuckyGameSystem)

    CLuckyGameSystem();
    ~CLuckyGameSystem() override;

    static CLuckyGameSystem* Inst();

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void SetCommonGameRewardIndex(int index) noexcept { m_nCommonGameRewardIndex = index; }
    void SetPremiumGameRewardIndex(int index) noexcept { m_nPremiumGameRewardIndex = index; }
    int GetCommonGameRewardIndex() const noexcept { return m_nCommonGameRewardIndex; }
    int GetPremiumGameRewardIndex() const noexcept { return m_nPremiumGameRewardIndex; }
    bool IsClosingLuckyGame(LuckyGameSystem::BettingType type) const noexcept;
    bool AcquirePickLuckyGameFlag() noexcept;
    void ReleasePickLuckyGameFlag() noexcept;
    bool IsAcquiredPickLuckyGameFlag() const noexcept;

    void IncreaseCommonTurnCnt();
    void IncreasePremiumTurnCnt();
    bool CheckRevCount(LuckyGameSystem::BettingType type, LuckyGameReward* reward);
    void IncreaseCurrentRevCount(LuckyGameSystem::BettingType type);
    void ClosingLuckyGame(LuckyGameSystem::BettingType type);
    int GetRevCount(LuckyGameSystem::BettingType type);
    void SetCommonTurnCnt(int turn);
    void SetPremiumTurnCnt(int turn);
    int GetCommonTurnCnt();
    int GetPremiumTurnCnt();
    void LoadFromDB(int index, int value);
    int GetRewardIndex(LuckyGameSystem::BettingType type);
    int GetTarotCardId(LuckyGameSystem::BettingType type);
    void IncreaseCurrentTurnCnt(LuckyGameSystem::BettingType type);
    void SetCurrentTurnCnt(LuckyGameSystem::BettingType type, int turn);
    int GetCurrentTurnCnt(LuckyGameSystem::BettingType type);
    bool CheckTurningPoint(LuckyGameSystem::BettingType type, int turningPoint);
    void ScreenNoticeUniqueReward(User* user, LuckyGameSystem::BettingType type, int itemClassId, int amount);
    LuckyGameReward* GetReward(int index);
    int GetPersonalPoint(const User* user, int index);
    bool SetPersonalPoint(const User* user, int index, int point, bool loadFromDatabase);
    LuckyGameItem* PickLuckyGameReward(
        User* user, LuckyGameSystem::BettingType type, LuckyGameSystem::LuckyRewardType& rewardType
    );
    bool GetLuckyGameResult(User* user, LuckyGameSystem::BettingType type, int count, int& result);

private:
    std::int32_t m_nCommonGameRewardIndex; // +0x18
    std::int32_t m_nPremiumGameRewardIndex; // +0x1c
    RWLock m_CommonTurnLock; // +0x20
    RWLock m_PremiumTurnLock; // +0x58
    std::int32_t m_nCommonTurnCnt; // +0x90
    std::int32_t m_nPremiumTurnCnt; // +0x94
    std::map<std::CDualKey<int, int>, std::int32_t> m_mapPersonalPoint; // +0x98
    std::int32_t m_nCommonRevCount; // +0xb8
    std::int32_t m_nPremiumRevCount; // +0xbc
    volatile std::int32_t m_nPickLuckyGameFlag; // +0xc0
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignmentC4ToC8{};
};
