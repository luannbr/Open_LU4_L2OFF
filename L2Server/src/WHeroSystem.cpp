#include "WHeroSystem.h"

#include "Alliance.h"
#include "DB.h"
#include "OlympiadNetwork.h"
#include "OlympiadServiceProxy.h"
#include "Pledge.h"
#include "User.h"

#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/QueryPacket.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace {

template <std::size_t Capacity>
void CopyHeroText(std::array<char16_t, Capacity>& destination, const std::u16string_view source) {
    destination.fill(u'\0');
    source.copy(destination.data(), Capacity - 1);
}

template <std::size_t Capacity>
std::u16string HeroPacketText(const std::array<char16_t, Capacity>& source) {
    std::size_t length = 0;
    while (length < Capacity && source[length] != u'\0') {
        ++length;
    }
    return std::u16string(std::u16string_view(source.data(), length));
}

} // namespace

WHeroInfo::WHeroInfo() { Init(); }

WHeroInfo::~WHeroInfo() = default;

void WHeroInfo::ClearMatchResult() {
    L2_GUARD_FUNCTION();
    m_HeroMatchResultInfo = {};
    L2_GUARD(m_Lock);
    m_HeroMatchResultList.clear();
}

void WHeroInfo::Init() {
    // m_nHeroType = 0
    m_nHeroType = HERO_NONE;
    m_nAchievementsSize = 0;
    runtime::raw_atomic::StoreI32Relaxed(&m_nAchievementsChanged, 0);
    m_key = {};
    m_sCharName.fill(u'\0');
    m_nPledgeId = 0;
    m_sPledgeName.fill(u'\0');
    m_nPledgeCrestId = 0;
    m_nAllianceId = 0;
    m_sAllianceName.fill(u'\0');
    m_nAllianceCrestId = 0;
    m_nWinCount = 0;
    m_sWords.fill(u'\0');
    m_bHistoryOpen = false;
    ClearMatchResult();
}

WHeroSystem::WHeroSystem() :
    m_HeroInfo(),
    m_nHeroCount(0),
    m_nHeroListChanged(0) {}

WHeroSystem::~WHeroSystem() = default;

void WHeroSystem::AddGiveSkill(const std::int64_t skillKey) { m_vHeroSkillList.push_back(skillKey); }

std::vector<std::int64_t>* WHeroSystem::GetHeroSkillListVector() { return &m_vHeroSkillList; }

WHeroInfo* WHeroSystem::GetHeroInfoByClass(const int classType) {
    const int index = OlympiadDefine::GetHeroClassIndex(static_cast<ClassType>(classType));
    return index == -1 ? nullptr : &m_HeroInfo[index];
}

int WHeroSystem::GetHeroClass(const std::CDualKey<int, int> key) {
    L2_GUARD(m_HeroMapLock);
    const auto position = m_HeroMap.find(key);
    return position == m_HeroMap.end() ? 0 : position->second;
}

void WHeroSystem::SetHeroProfile(
    const int classType, const std::CDualKey<int, int> key, const std::u16string_view characterName
) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    info->m_key = key;
    CopyHeroText(info->m_sCharName, characterName);
    runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
}

void WHeroSystem::SetHeroWinCount(const int classType, const int winCount) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    info->m_nWinCount = winCount;
    runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
}

void WHeroSystem::SetHeroType(const int classType, const int heroType) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    const int previous = info->m_nHeroType;
    info->m_nHeroType = heroType;
    if (previous != heroType) {
        runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
    }
}

void WHeroSystem::SetHeroWords(const int classType, const std::u16string_view words) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    CopyHeroText(info->m_sWords, words);
    runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
}

void WHeroSystem::SetHistoryOpen(const int classType, const bool open) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    info->m_bHistoryOpen = open;
    runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
}

void WHeroSystem::SetHeroPledge(
    const int classType, const int pledgeId, const std::u16string_view pledgeName, const int pledgeCrestId
) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    const int previousPledgeId = info->m_nPledgeId;
    const int previousCrestId = info->m_nPledgeCrestId;
    info->m_nPledgeId = pledgeId;
    CopyHeroText(info->m_sPledgeName, pledgeName);
    info->m_nPledgeCrestId = pledgeCrestId;
    if (previousPledgeId != pledgeId || previousCrestId != pledgeCrestId) {
        runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
    }
}

void WHeroSystem::SetHeroAlliance(
    const int classType, const int allianceId, const std::u16string_view allianceName, const int allianceCrestId
) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        return;
    }
    const int previousAllianceId = info->m_nAllianceId;
    const int previousCrestId = info->m_nAllianceCrestId;
    info->m_nAllianceId = allianceId;
    CopyHeroText(info->m_sAllianceName, allianceName);
    info->m_nAllianceCrestId = allianceCrestId;
    if (previousAllianceId != allianceId || previousCrestId != allianceCrestId) {
        runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
    }
}

void WHeroSystem::SetHeroTypeById(const std::CDualKey<int, int> key, const int heroType) {
    L2_GUARD_FUNCTION();
    SetHeroType(GetHeroClass(key), heroType);
}

void WHeroSystem::SetHeroPledge(User* const user) {
    L2_GUARD_FUNCTION();
    if (!user || !user->m_data) {
        return;
    }

    const int classType = GetHeroClass(std::CDualKey<int, int>(user->m_data->GetWorldId(), user->GetDBID()));
    CPledge* const pledge = user->GetPledge();
    if (!pledge) {
        SetHeroPledge(classType, 0, u"", 0);
        return;
    }
    SetHeroPledge(
        classType, static_cast<int>(pledge->GetDbId()), pledge->GetPledgeName(), static_cast<int>(pledge->GetCrestId())
    );
}

void WHeroSystem::SetHeroAlliance(User* const user) {
    L2_GUARD_FUNCTION();
    if (!user || !user->m_data) {
        return;
    }

    const int classType = GetHeroClass(std::CDualKey<int, int>(user->m_data->GetWorldId(), user->GetDBID()));
    CAlliance* const alliance = user->GetAlliance();
    if (!alliance || !alliance->m_data) {
        SetHeroAlliance(classType, 0, u"", 0);
        return;
    }
    SetHeroAlliance(
        classType, static_cast<int>(alliance->m_data->m_nDbId), FixedUtf16View(alliance->m_data->m_sAllianceName),
        static_cast<int>(alliance->m_data->m_nCrestId)
    );
}

void WHeroSystem::SetHeroRank(
    const int heroCount, int (&heroClassList)[36], int (&heroWorldIdList)[36], int (&heroIdList)[36]
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_nHeroCount = heroCount;
    {
        L2_GUARD(m_HeroMapLock);
        m_HeroMap.clear();
        for (int index = 0; index < 36; ++index) {
            if (index < m_nHeroCount) {
                m_HeroClassList[index] = heroClassList[index];
                m_HeroIdList[index] = heroIdList[index];
                m_HeroWorldIdList[index] = heroWorldIdList[index];
                m_HeroMap[std::CDualKey<int, int>(heroWorldIdList[index], heroIdList[index])] = heroClassList[index];
            } else {
                m_HeroClassList[index] = -1;
                m_HeroIdList[index] = -1;
                m_HeroWorldIdList[index] = -1;
            }
        }
    }
    for (int index = 0; index < m_nHeroCount; ++index) {
        if (WHeroInfo* const info = GetHeroInfoByClass(m_HeroClassList[index])) {
            info->Init();
        }
    }
    runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 1);
}

bool WHeroSystem::ClearHeroMatchResult(const std::CDualKey<int, int> key) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(GetHeroClass(key));
    if (info) {
        info->ClearMatchResult();
    }
    return info != nullptr;
}

void WHeroSystem::AddHeroMatchResult(
    const std::CDualKey<int, int> key, const std::list<protocol::PkWOlympiadMatchResult>& results
) {
    L2_GUARD_FUNCTION();
    WHeroInfo* const info = GetHeroInfoByClass(GetHeroClass(key));
    if (!info) {
        return;
    }
    L2_GUARD(info->m_Lock);
    for (const protocol::PkWOlympiadMatchResult& source : results) {
        if (source.nIsWinner == 0) {
            ++info->m_HeroMatchResultInfo.loseCount;
        } else if (source.nIsWinner == 1) {
            ++info->m_HeroMatchResultInfo.winCount;
        } else if (source.nIsWinner == 3) {
            ++info->m_HeroMatchResultInfo.drawCount;
        } else {
            continue;
        }
        ++info->m_HeroMatchResultInfo.matchCount;
        WOlympiadMatchResult result{};
        result.matchTime = source.nMatchTime;
        result.isWinner = source.nIsWinner;
        result.elapsedTime = source.nElapsedTime;
        result.rivalClassId = source.nRivalClassType;
        result.gameRule = source.nGameRule;
        result.rivalName = source.szRivalName;
        info->m_HeroMatchResultList.push_back(result);
    }
}

void WHeroSystem::Init() {
    L2_GUARD_FUNCTION();
    CQUERY_WOLYMPIAD_LOAD_ALL_HERO rank;
    if (!TQuery<CQUERY_WOLYMPIAD_LOAD_ALL_HERO, 2>::Do(g_nWorldId, &rank)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in "
        //     L"CQUERY_WOLYMPIAD_LOAD_ALL_HERO",
        //     L"WHeroSystem.cpp",
        //     0x9b)
        // Log.Add(LOG_ERROR,
        //     "[world olympiad] Hero initialization stopped: the hero-rank query through world %d did not complete.",
        //     g_nWorldId)
        L2_LOG_ERROR(
            Log, "[world olympiad] Hero initialization stopped: the hero-rank query through world %d did not complete.",
            g_nWorldId
        );
        return;
    }

    SetHeroRank(rank.mHeroCount, rank.mHeroClassList, rank.mHeroWorldIDList, rank.mHeroIdList);
    for (int index = 0; index < m_nHeroCount; ++index) {
        CQUERY_WOLYMPIAD_GET_HERO_BY_ID hero(rank.mHeroWorldIDList[index], rank.mHeroIdList[index]);
        // if (!TQuery<CQUERY_WOLYMPIAD_GET_HERO_BY_ID, 3>::Do(
        //         g_nWorldId,
        //         &hero) ||
        //     !hero.mReturn) {
        //     continue.
        // }
        const bool queryCompleted = TQuery<CQUERY_WOLYMPIAD_GET_HERO_BY_ID, 3>::Do(g_nWorldId, &hero);
        if (!queryCompleted || !hero.mReturn) {
            // Log.Add(LOG_WARN,
            //     "[world olympiad] Hero initialization skipped character DB ID %d from world %d: %s.",
            //     rank.mHeroIdList[index], rank.mHeroWorldIDList[index],
            //     queryCompleted ? "CacheD returned no hero profile" : "query did not complete")
            L2_LOG_WARN(
                Log, "[world olympiad] Hero initialization skipped character DB ID %d from world %d: %s.",
                rank.mHeroIdList[index], rank.mHeroWorldIDList[index],
                queryCompleted ? "CacheD returned no hero profile" : "query did not complete"
            );
            continue;
        }

        const std::CDualKey<int, int> key(hero.m_nWorldID, hero.mCharId);
        SetHeroProfile(hero.mMainClass, key, FixedUtf16View(hero.mCharName));
        SetHeroWinCount(hero.mMainClass, hero.mWinCount);
        SetHeroType(hero.mMainClass, hero.mHeroType);
        SetHeroWords(hero.mMainClass, FixedUtf16View(hero.mWords));
        SetHeroPledge(hero.mMainClass, hero.mPledgeId, FixedUtf16View(hero.mPledgeName), hero.mPledgeCrestId);
        SetHeroAlliance(hero.mMainClass, hero.mAllianceId, FixedUtf16View(hero.mAllianceName), hero.mAllianceCrestId);
        SetHistoryOpen(hero.mMainClass, hero.mHistoryOpen != 0);
        ClearHeroMatchResult(key);
        db.RequestWOlympiadGetMatchResult(hero.m_nWorldID, hero.mCharId);
    }
}

void WHeroSystem::RefreshHeroList() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_vHeroInfosForPacket.clear();
    for (int index = 0; index < m_nHeroCount; ++index) {
        const int classType = m_HeroClassList[index];
        WHeroInfo* const info = GetHeroInfoByClass(classType);
        if (!info || !HeroType_IsHero(info->m_nHeroType)) {
            continue;
        }
        olympiad::PkHeroInfo packetInfo{};
        packetInfo.nWorldID = info->m_key.m_PrimaryKey;
        packetInfo.sName = HeroPacketText(info->m_sCharName);
        packetInfo.nClassType = classType;
        packetInfo.sPledgeName = HeroPacketText(info->m_sPledgeName);
        packetInfo.nPledgeCrestId = info->m_nPledgeCrestId;
        packetInfo.sAllianceName = HeroPacketText(info->m_sAllianceName);
        packetInfo.nAllianceCrestId = info->m_nAllianceCrestId;
        packetInfo.nWinCount = info->m_nWinCount;
        packetInfo.nHeroType = info->m_nHeroType;
        m_vHeroInfosForPacket.push_back(std::move(packetInfo));
    }
}

void WHeroSystem::RequestReloadHeroRankToCacheD() {
    L2_GUARD_FUNCTION();
    CQUERY_WOLYMPIAD_LOAD_ALL_HERO query;
    db.RequestTPacketProcess(g_nWorldId, 0x49, static_cast<int>(sizeof(query)), reinterpret_cast<CStackOnly&>(query));
}

void WHeroSystem::RequestReloadAllHeroesToCacheD() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (int index = 0; index < m_nHeroCount; ++index) {
        if (m_HeroIdList[index] < 0) {
            continue;
        }
        CQUERY_WOLYMPIAD_GET_HERO_BY_ID query(m_HeroWorldIdList[index], m_HeroIdList[index]);
        db.RequestTPacketProcess(g_nWorldId, 0x51, static_cast<int>(sizeof(query)), reinterpret_cast<CStackOnly&>(query));
    }
}

bool WHeroSystem::SendHeroMatchResult(const std::CDualKey<int, int> key, const int classType, const int page) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_HERO_MATCH_RESULT packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.nPage = page;
    packet.nClassType = classType;
    WHeroInfo* const info = GetHeroInfoByClass(classType);
    if (!info) {
        Singleton<WOlympiadNetworkHost>::inst().SendPacket(key.m_PrimaryKey, OSA_HERO_MATCH_RESULT, packet);
        return false;
    }

    packet.nWinCount = info->m_HeroMatchResultInfo.winCount;
    packet.nDrawCount = info->m_HeroMatchResultInfo.drawCount;
    packet.nLoseCount = info->m_HeroMatchResultInfo.loseCount;
    int cumulativeWin = 0;
    int cumulativeLose = 0;
    int cumulativeDraw = 0;
    int emitted = 0;
    const int firstResult = page * 15 - 15;
    const std::size_t available = info->m_HeroMatchResultList.size();
    for (int index = 0;
        index < info->m_HeroMatchResultInfo.matchCount && static_cast<std::size_t>(index) < available && emitted < 15;
        ++index) {
        const WOlympiadMatchResult& source =
            runtime::CheckedVectorAt(info->m_HeroMatchResultList, static_cast<std::size_t>(index));
        int templateNumber = 0;
        if (source.isWinner == 0) {
            ++cumulativeLose;
            templateNumber = 2;
        } else if (source.isWinner == 1) {
            ++cumulativeWin;
            templateNumber = 1;
        } else if (source.isWinner == 3) {
            ++cumulativeDraw;
            templateNumber = 3;
        }
        if (index < firstResult || templateNumber == 0) {
            continue;
        }

        olympiad::PkHeroMatchResult result{};
        result.nMatchTime = source.matchTime;
        result.nIsWinner = source.isWinner;
        result.nGameRule = source.gameRule;
        result.sRivalName = HeroPacketText(source.rivalName);
        result.nRivalClassID = source.rivalClassId;
        result.nElapsedTime = source.elapsedTime;
        result.nTempleteNumber = templateNumber;
        result.nWinCount = cumulativeWin;
        result.nDrawCount = cumulativeDraw;
        result.nLoseCount = cumulativeLose;
        packet.vList.push_back(std::move(result));
        ++emitted;
    }
    packet.bHasNextPage = page * 15 < info->m_HeroMatchResultInfo.matchCount;
    Singleton<WOlympiadNetworkHost>::inst().SendPacket(key.m_PrimaryKey, OSA_HERO_MATCH_RESULT, packet);
    return true;
}

void WHeroSystem::SendHeroList(const std::CDualKey<int, int> key) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_nHeroListChanged, 0) == 1) {
        RefreshHeroList();
    }

    olympiad::OSA_HERO_LIST packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    {
        L2_GUARD(m_Lock);
        for (const olympiad::PkHeroInfo& info : m_vHeroInfosForPacket) {
            packet.vList.push_back(info);
        }
    }
    Singleton<WOlympiadNetworkHost>::inst().SendPacket(key.m_PrimaryKey, OSA_HERO_LIST, packet);
}
