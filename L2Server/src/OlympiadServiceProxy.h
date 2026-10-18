#pragma once

#include "OlympiadNetwork.h"
#include "OlympiadStuff.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"
#include "Shared/src/proto/OlympiadPacket.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CHtmlGenerator;
class User;

class WOlympiadServiceNode {
public:
    explicit WOlympiadServiceNode(int worldId);
    ~WOlympiadServiceNode() = default;

    void Clear();
    bool Connect(CSPointer<User> user);
    void Disconnect(int userDatabaseId);

private:
    int m_idWorld; // +0x00
    CYieldLock m_Lock; // +0x04
    std::map<int, std::uint32_t> m_Members; // +0x18
};

class WOlympiadServiceProxy : public Singleton<WOlympiadServiceProxy>, public IWOlympiadNetworkPeerCallback {
public:
    WOlympiadServiceProxy();
    ~WOlympiadServiceProxy() override;

    void OnCloseHost() override;

    bool Init();
    bool IsPossibleToSetHero() const;
    bool IsMember(int userDatabaseId) const;

    void OnNotifyNoblessInfo(const olympiad::OSN_NOBLESS_INFO& packet);
    void RequestClassRankByServer();
    void UpdatePledgeInfo(int pledgeDatabaseId, int crestDatabaseId);
    bool SendMatchListToUser(CSPointer<User> user);
    bool SendHeroListToUser(CSPointer<User> user);
    void RequestNoblessInfo(CSPointer<User> user);
    bool SendMatchCountToUser(CSPointer<User> user, CSPointer<User> target);
    bool SendOlympiadRecordTo(CSPointer<User> user);
    bool SendOlympiadInfoTo(CSPointer<User> user);
    bool SendDebugStringTo(CSPointer<User> user);
    void OnOlympiadRecord(const olympiad::OSA_OLYMPIAD_RECORD& packet);
    void RequestLastMatchResult(CSPointer<User> user);
    bool ResetOlympiadPoint(CSPointer<User> user, int point);
    void UnregisterAllMember();
    bool SendHeroMatchResult(CSPointer<User> user, int classType, int page);
    void RemoveMemberByID(int userDatabaseId);
    bool RegisterUser(CSPointer<User> user);
    bool BroadcastOlympiadInfo();
    bool SetHeroType(CSPointer<User> user, int heroType, std::uint32_t npcServerId, bool byBuilder) const;
    void OnOlympiadMatchingInfo(const olympiad::OSN_OLYMPIAD_MATCHING_INFO& packet);
    void OnSetOlympiadTerm(int seasonStart, int nominateSecond);
    void OnHostDisconnected();
    bool DeleteTradePoint(CSPointer<User> user, int tradePoint, std::uint32_t npcServerId);
    bool ConnectGame(CSPointer<User> user);
    void RegisterMember(CSPointer<User> user);
    void UnregisterMember(CSPointer<User> user);
    void ReloadUserIfReturnedCompletely(CSPointer<User> user);
    void OnConnectResult(int userDatabaseId, bool success, bool asPlayer);
    bool StopMatchMaking(CSPointer<User> user, bool byDisconnect);
    void OnStartResult(const olympiad::OSA_START& packet);
    void OnStopResult(const olympiad::OSA_STOP& packet);
    void RestartUserIfCompletely(CSPointer<User> user);
    bool StartMatchMaking(CSPointer<User> user);
    bool HandleCommand(int command, const std::vector<std::u16string>& params);
    bool ObserveMatch(CSPointer<User> user, int fieldId);
    void OnPrepareAsPlayer(int userDatabaseId) const;
    void OnHeroMatchResult(const olympiad::OSA_HERO_MATCH_RESULT& packet);

private:
    struct MatchResultCache {
        CYieldLock m_Lock; // +0x00
        std::map<std::pair<int, int>, CHtmlGenerator*> m_heroMatchResultMap; // +0x10
    };

    mutable CYieldLock m_Lock; // +0x10
    WOlympiadServiceNode* m_pServiceNode; // +0x20
    std::map<int, std::uint32_t> m_Members; // +0x28
    MatchResultCache m_MatchResultCache; // +0x48
    int m_tSeasonStart; // +0x78
    int m_nNominateSecond; // +0x7c
    int m_nSeasonStartYear; // +0x80
    int m_nSeasonStartMonth; // +0x84
    bool m_bMatchOpen; // +0x88
    [[maybe_unused]] std::uint8_t m_alignment0089To0090[7]{};
};

namespace WOlympiadServiceProxyUtil {

bool IsHero(int characterDatabaseId);
void ResetRankInfo(ClassType classType);
void AddRankInfo(ClassType classType, int ranking, const wchar_t* name, int point);
void BroadcastPacket(S_EX_PROTOCOL protocol, const network::IPacket& packet);
void RegisterToOlympiad(const CSPointer<User> user);
void RegisterToOlympiad(int userDatabaseId);

} // namespace WOlympiadServiceProxyUtil
