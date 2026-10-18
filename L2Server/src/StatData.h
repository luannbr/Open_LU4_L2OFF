#pragma once

#include <cstddef>
#include <cstdint>

namespace UserStat {

enum UserStatType2 : std::int32_t {
    US_EXP = 0,
    US_SP = 1,
    US_ADENA = 2,
    US_MAX = 3,
};

enum ItemCategory : std::int32_t {
    IC_WEAPON = 0,
    IC_ARMOR = 1,
    MAX_ITEM_CATEGORY = 2,
};

} // namespace UserStat

class UserStatData {
public:
    UserStatData();

    void Clear();
    void OnEnterWorld();
    void OnLeaveWorld();
    void OnJoinParty(unsigned int partySize);
    void OnLeaveParty(unsigned int partySize);
    void BeginFullParty(unsigned int partySize);
    void EndFullParty(unsigned int partySize);
    void OnTryEnchant(UserStat::ItemCategory category);
    void OnEnchantSucceeded(UserStat::ItemCategory category, int enchantLevel);
    void OnLevelUp(unsigned int level);
    void OnAcceptQuest();
    void OnCompleteQuest();
    void OnCancelQuest();
    void OnEnterInzone();
    void OnKillNPC();
    void OnKillPC();
    void OnHillToPC();
    void OnBeginRaid();
    void OnCompleteRaid();
    void OnTalkToNPC();
    void AddUserStat2(UserStat::UserStatType2 type, std::int64_t value);

private:
    std::int32_t m_timeEnterWorld; // +0x00

public:
    volatile std::int64_t m_UserStat[UserStat::US_MAX]; // +0x08
};
