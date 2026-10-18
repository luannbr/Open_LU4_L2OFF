#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"
#include "Shared/src/proto/DBPacket.h"
#include "Shared/src/proto/OlympiadPacket.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

class User;

struct WOlympiadMatchResult {
    int matchTime; // +0x00
    int isWinner; // +0x04
    int elapsedTime; // +0x08
    int rivalClassId; // +0x0c
    int gameRule; // +0x10
    std::array<char16_t, 25> rivalName; // +0x14..+0x45
    std::array<std::byte, 0x02> m_alignment0046To0048; // +0x46..+0x47
};

struct WHeroMatchResultInfo {
    int matchCount;
    int winCount;
    int loseCount;
    int drawCount;
};

class WHeroInfo {
public:

    WHeroInfo();
    ~WHeroInfo();

    void Init();
    void ClearMatchResult();

private:
    friend class WHeroSystem;

    std::multimap<int, std::int64_t> m_mmapAchievements; // +0x0000..+0x001f
    CYieldLock m_Lock; // +0x0020..+0x002f

    // Current code constructs and destroys this fixed achievement carrier
    // through owner-local helpers. Its element UDT is not required by the
    // hero-profile or match-result surface, but its bytes are part of the
    // physical WHeroInfo object and cannot be collapsed.
    [[maybe_unused]] std::array<std::byte, 0x1770> m_AchievementCarrier; // +0x0030..+0x179f

    int m_nAchievementsSize; // +0x17a0
    volatile std::int32_t m_nAchievementsChanged; // +0x17a4
    int m_nHeroType; // +0x17a8
    std::CDualKey<int, int> m_key; // +0x17ac
    std::array<char16_t, 25> m_sCharName; // +0x17b4..+0x17e5
    [[maybe_unused]] std::array<std::byte, 0x02> m_alignment17e6To17e8; // +0x17e6..+0x17e7
    int m_nPledgeId; // +0x17e8
    std::array<char16_t, 25> m_sPledgeName; // +0x17ec..+0x181d
    [[maybe_unused]] std::array<std::byte, 0x02> m_alignment181eTo1820; // +0x181e..+0x181f
    int m_nPledgeCrestId; // +0x1820
    int m_nAllianceId; // +0x1824
    std::array<char16_t, 25> m_sAllianceName; // +0x1828..+0x1859
    [[maybe_unused]] std::array<std::byte, 0x02> m_alignment185aTo185c; // +0x185a..+0x185b
    int m_nAllianceCrestId; // +0x185c
    int m_nWinCount; // +0x1860
    std::array<char16_t, 129> m_sWords; // +0x1864..+0x1965
    bool m_bHistoryOpen; // +0x1966
    [[maybe_unused]] std::byte m_alignment1967To1968; // +0x1967
    WHeroMatchResultInfo m_HeroMatchResultInfo; // +0x1968..+0x1977
    std::vector<WOlympiadMatchResult> m_HeroMatchResultList; // +0x1978..+0x1997
};

class WHeroSystem : public Singleton<WHeroSystem> {
public:

    WHeroSystem();
    ~WHeroSystem() override;

    void AddGiveSkill(std::int64_t skillKey);
    std::vector<std::int64_t>* GetHeroSkillListVector();

    int GetHeroClass(std::CDualKey<int, int> key);
    WHeroInfo* GetHeroInfoByClass(int classType);
    void SetHeroProfile(int classType, std::CDualKey<int, int> key, std::u16string_view characterName);
    void SetHeroWinCount(int classType, int winCount);
    void SetHeroType(int classType, int heroType);
    void SetHeroWords(int classType, std::u16string_view words);
    void SetHistoryOpen(int classType, bool open);
    void SetHeroPledge(int classType, int pledgeId, std::u16string_view pledgeName, int pledgeCrestId);
    void SetHeroAlliance(int classType, int allianceId, std::u16string_view allianceName, int allianceCrestId);
    void SetHeroTypeById(std::CDualKey<int, int> key, int heroType);
    void SetHeroPledge(User* user);
    void SetHeroAlliance(User* user);
    void SetHeroRank(int heroCount, int (&heroClassList)[36], int (&heroWorldIdList)[36], int (&heroIdList)[36]);
    bool ClearHeroMatchResult(std::CDualKey<int, int> key);
    void AddHeroMatchResult(std::CDualKey<int, int> key, const std::list<protocol::PkWOlympiadMatchResult>& results);

    void Init();
    void RefreshHeroList();
    void RequestReloadHeroRankToCacheD();
    void RequestReloadAllHeroesToCacheD();
    bool SendHeroMatchResult(std::CDualKey<int, int> key, int classType, int page);
    void SendHeroList(std::CDualKey<int, int> key);

private:
    CYieldLock m_Lock; // +0x00008..+0x00017
    std::list<olympiad::PkHeroInfo> m_vHeroInfosForPacket; // +0x00018..+0x0002f
    CYieldLock m_HeroMapLock; // +0x00030..+0x0003f
    std::map<std::CDualKey<int, int>, int> m_HeroMap; // +0x00040..+0x0005f
    std::array<WHeroInfo, 36> m_HeroInfo; // +0x00060..+0x399bf
    std::array<int, 36> m_HeroClassList; // +0x399c0..+0x39a4f
    std::array<int, 36> m_HeroIdList; // +0x39a50..+0x39adf
    std::array<int, 36> m_HeroWorldIdList; // +0x39ae0..+0x39b6f
    int m_nHeroCount; // +0x39b70
    volatile std::int32_t m_nHeroListChanged; // +0x39b74
    std::vector<std::int64_t> m_vHeroSkillList; // +0x39b78..+0x39b97
};
