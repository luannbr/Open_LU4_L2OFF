#pragma once

#include "Auth.h"

#include "L2Core/src/inc/XStd.h"
#include "Server.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/Socket.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

// MSVC's `long` is the original 32-bit return lane. Preserve the source ABI
// on Windows and its exact width on LP64 portable hosts.
using RaidUserCountLong = std::conditional_t<sizeof(long) == sizeof(std::int32_t), long, std::int32_t>;

enum RaidServerOptionType : std::int32_t {
    RSOT_NORMAL = 0,
    RSOT_MAX = 1,
};


class CPledge;
class CAlliance;
class CUserSocket;
class RaidClientSocket;
class RaidServerSocket;
class User;
struct XMLElement;
enum SayPacketType : std::int32_t;


// Current Raid.cpp reservation record. The current image fixes the complete
// 0x100-byte consumer layout. The adjacent PDB supplies the original member
// and accessor names without overriding those current offsets.
class RaidReserverInfo {
public:
    RaidReserverInfo(
        int worldId,
        int accountId,
        SharedUtilsWideChar* accountName,
        int characterId,
        SharedUtilsWideChar* characterName,
        unsigned char languageType,
        int characterServerId,
        FVector originPosition,
        FVector destinationPosition,
        stAccountAuthInfo accountAuthInfo,
        int premiumLevel,
        int dormantType,
        int dormantMarkingTime
    );
    ~RaidReserverInfo();

    int GetWorldId() const { return m_nWorldId; }
    int GetAccountId() const { return m_nAccountId; }
    SharedUtilsWideChar* GetAccountName() const { return const_cast<SharedUtilsWideChar*>(m_sAccountName); }
    int GetCharId() const { return m_nCharId; }
    SharedUtilsWideChar* GetCharName() const { return const_cast<SharedUtilsWideChar*>(m_sCharName); }
    unsigned char GetLanguageType() const { return m_cLanguageType; }
    int GetCharServerId() const { return m_nCharServerId; }
    FVector GetOriginPos() const { return m_OriginPos; }
    FVector GetDestPos() const { return m_DestPos; }
    stAccountAuthInfo GetAuthInfo() const { return m_AccountAuthInfo; }
    int GetPremiumLevel() const { return m_nPremiumLevel; }
    int GetDormantType() const { return m_nDormantType; }
    int GetDormantMarkingTime() const { return m_nDormantMarkingTime; }

private:
    int m_nWorldId; // +0x000
    int m_nAccountId; // +0x004
    SharedUtilsWideChar m_sAccountName[15]; // +0x008
    [[maybe_unused]] std::uint16_t m_alignment026{}; // +0x026
    int m_nCharId; // +0x028
    SharedUtilsWideChar m_sCharName[25]; // +0x02c
    unsigned char m_cLanguageType; // +0x05e
    [[maybe_unused]] std::uint8_t m_alignment05f{};
    int m_nCharServerId; // +0x060
    [[maybe_unused]] std::uint32_t m_alignment064{};
    FVector m_OriginPos; // +0x068
    FVector m_DestPos; // +0x080
    int m_nPremiumLevel; // +0x098
    int m_nDormantType; // +0x09c
    int m_nDormantMarkingTime; // +0x0a0
    stAccountAuthInfo m_AccountAuthInfo; // +0x0a4
};


class WorldRaidHost {
public:
    using WorldRaidNpcMap = std::map<int, std::uint32_t>;

    WorldRaidHost();
    ~WorldRaidHost();

    std::uint32_t GetNPCID(int worldId);
    void StopWorldRaid(int worldId);
    bool StartWorldRaid(int worldId, std::uint32_t npcId);

private:
    CYieldLock m_Lock; // +0x00

public:
    // RaidServerPacketHandler.cpp reads this physical source member when it
    // builds the RSC_WORLD_RAID_START reply.
    int m_idFirstStartedWorld; // +0x10

private:
    [[maybe_unused]] std::uint32_t m_alignment14{}; // +0x14
    WorldRaidNpcMap m_WorldRaidNpcs; // +0x18
};


class RaidServerManager : public Server, public Singleton<RaidServerManager> {
public:
    using RaidSocketMap = std::map<int, RaidClientSocket*>;
    using ClientQuotaMap = std::map<int, int>;
    using ForbiddenItemSet = std::set<int>;
    using RaidKey = std::CDualKey<int, int>;
    using RaidCandidateMap = std::map<RaidKey, RaidReserverInfo*>;
    using RaidUserMap = std::map<RaidKey, User*>;
    using RaidWorldMap = std::map<int, bool>;
    using RaidPledgeMap = std::map<int, RaidWorldMap>;

    L2_DECLARE_OBJECT_REGISTRATION(RaidServerManager)

    RaidServerManager();
    ~RaidServerManager() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    RaidServerOptionType GetReviveType();
    RaidServerOptionType GetPKKarmaPenaltyType();
    void Load();
    RaidClientSocket* GetSocket(int worldId);
    bool SetSocket(int worldId, RaidClientSocket* socket);
    bool RemoveSocket(int worldId);
    void RaidClientVersionPacket(int worldId, RaidClientSocket* socket);
    void RaidReserveUserPacket(RaidClientSocket* socket, const unsigned char* packet);
    void DeleteRaidCandidate(int worldId, int characterDatabaseId);
    RaidReserverInfo* GetRaidCandidateInfo(RaidKey key);
    void ConfirmRaidUser(User& user);
    void ReplyVersionPacket(int worldId, int result);
    User* GetAttacker(RaidKey key);
    void DelegatePacketHandle(User& user, int packetProtocol, int packetLength, const char* packet);
    void ClearRaidUserMap(User& user);
    void DisconnectRaidUser(int worldId, int characterDatabaseId);
    void BroadcastWorldRaidEvent(int firstValue, int secondValue, int thirdValue);
    void NotifyRaidUserDisconnected(User& user);
    bool RestartOnRaidServer(User& user);
    bool ReturnToOrigin(User* user);
    void ShowSocketList();
    int GetCurrentRaiduserCount();
    void ShowRaidServerInfo(User& user);
    bool RequestPledgeChattingRelay(
        CPledge& pledge,
        int serverId,
        int characterDatabaseId,
        const char16_t* senderName,
        const char16_t* message,
        SayPacketType type
    );
    bool DoNotRelayForPledge(CPledge& pledge);
    void RemoveRaidPledge(CPledge& pledge);

private:
    RaidReserverInfo* _GetRaidCandidateInfo(RaidKey key);
    void LoadForbiddenItems(XMLElement& element);

    CYieldLock m_Lock; // +0x0a8
    RaidSocketMap m_RaidSockets; // +0x0b8
    ClientQuotaMap m_ClientQuotaMap; // +0x0d8
    ForbiddenItemSet m_ForbiddenItemSet; // +0x0f8
    RaidCandidateMap m_mapRaidCandidates; // +0x118
    RaidUserMap m_mapRaidUsersByKey; // +0x138
    RaidPledgeMap m_mapRaidPledge; // +0x158
    RaidServerOptionType m_ReviveType; // +0x178
    RaidServerOptionType m_DiePenaltyType; // +0x17c
    RaidServerOptionType m_PKKarmaPenaltyType; // +0x180
    [[maybe_unused]] std::uint32_t m_alignment184{};

public:
    // The current packet owner reaches this physical member directly.
    WorldRaidHost* m_pWorldRaidHost; // +0x188

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(RaidServerManager)
};


class RaidAllowAction {
public:

    RaidAllowAction();

    static RaidAllowAction& Inst();
    void SetAction();
    bool IsValidAction(int actionId) const;
    void AddAction(int actionId);
    void SendActionList(User* user);

private:
    static RaidAllowAction* m_AllowAction;

    std::vector<int> m_vInvalidActionList; // +0x0000
    int m_actionCount; // +0x0020
    std::uint32_t m_bufLen; // +0x0024
    std::uint8_t m_buf[0x4f9c]; // +0x0028
};


class RaidEnterVerifier final : public CIOObject {
public:
    explicit RaidEnterVerifier(CUserSocket* socket);
    ~RaidEnterVerifier() override = default;

    void TimerExpired(int id) override;

private:
    CUserSocket* m_pUserSocket; // +0x18
};


class RaidSocket : public CSocket {
public:
    explicit RaidSocket(std::uint64_t socket);
    ~RaidSocket() override;

    L2_DECLARE_OBJECT_TYPE(RaidSocket)

    void SetWorldId(int worldId) noexcept { m_nWorldId = worldId; }
    int GetWorldId() const noexcept { return m_nWorldId; }

    bool CheckPendingWrite() override;
    void OnCreate() override;

protected:
    void OnClose() override;
    void OnRead() override;

    std::uint8_t m_cMaxProtocolNo; // +0x108
    std::byte m_alignment109To10c[3]{};
    int m_nWorldId; // +0x10c
};

class RaidServerSocket : public RaidSocket {
public:
    explicit RaidServerSocket(std::uint64_t socket);
    ~RaidServerSocket() override;

    L2_DECLARE_OBJECT_TYPE(RaidServerSocket)

protected:
    void OnClose() override;
};


class RaidClientSocket : public RaidSocket {
public:
    explicit RaidClientSocket(std::uint64_t socket);
    ~RaidClientSocket() override;

    L2_DECLARE_OBJECT_TYPE(RaidClientSocket)

    static RaidClientSocket* Allocate(std::uint64_t socket);

    void SetQuota(int quota) noexcept { m_Quota = quota; }
    bool CheckQuota();
    void OnUserSocketClosed();
    long GetCurrentQuota() const noexcept { return static_cast<long>(m_CurrentQuota); }

protected:
    void OnClose() override;

private:
    int m_Quota; // +0x110
    volatile int m_CurrentQuota; // +0x114
};


using RaidServerPacketCallback = bool (*)(RaidServerSocket*, const unsigned char*);
using RaidClientPacketCallback = bool (*)(RaidClientSocket*, const unsigned char*);
using RaidServerPacketCallbackMap = FixedSizeCallbackMap<15, RaidServerPacketCallback>;
using RaidClientPacketCallbackMap = FixedSizeCallbackMap<39, RaidClientPacketCallback>;

extern RaidServerPacketCallbackMap g_RaidServerPacketTable;
extern RaidClientPacketCallbackMap g_RaidClientPacketTable;


class RaidServerInfo {
public:
    using RaidUserMap = CObjectMap<User, 4, 5, RWLock, int>;

    RaidServerInfo(int worldId, std::uint32_t serverAddress, int port);
    ~RaidServerInfo();

    void Init();
    bool Connect();
    bool IsConnected() const noexcept { return m_bConnected; }
    void Connected(bool value) noexcept { m_bConnected = value; }
    void SetSendable(bool value);
    bool GetSendable() const noexcept { return m_bSendable; }
    bool IsSendable() const noexcept { return m_bSendable && m_pRaidSocket != nullptr; }
    bool IsValidRaidSocket() const noexcept { return m_pRaidSocket != nullptr; }
    void SetLogTick(std::uint32_t tick) noexcept { m_nLogTick = tick; }
    bool CanLog() const noexcept;
    std::uint32_t GetServerAddr() const noexcept { return m_ServerAddr; }
    bool Send(const char* format, ...);
    int Quota() const noexcept { return m_Quota; }
    bool CheckCondition(User* user, bool byBuilder);
    bool RequestReserveUser(User* user, FVector& position);
    bool DeleteReserver(int characterDatabaseId, bool notifyRaidServer);
    void DeleteAllReserver();
    void DisconnectUser(int worldId, int characterDatabaseId);
    int GetCurrentQuota() const noexcept { return m_CurrentQuota; }
    int GetWorldID() const noexcept { return m_nWorldId; }

private:
    friend class RaidClientManager;

    bool CheckQuota() const noexcept;

    bool m_bConnected; // +0x000
    bool m_bSendable; // +0x001
    std::byte m_alignment002To008[6]{};
    RaidServerSocket* m_pRaidSocket; // +0x008
    std::uint32_t m_nLogTick; // +0x010
    int m_nWorldId; // +0x014
    std::uint32_t m_ServerAddr; // +0x018
    int m_nPort; // +0x01c
    int m_Quota; // +0x020
    volatile int m_CurrentQuota; // +0x024
    RaidUserMap m_mapRaidUsers; // +0x028
};


class RaidPledgeMemberService {
public:
    virtual bool Do(CPledge& pledge, RaidServerInfo& server) = 0;
};

class RaidPledgeMemberService_PacketRelay final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_PacketRelay(int length, const char* buffer) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    int m_nLen; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10{};
    const char* m_Buffer; // +0x10
};

class RaidPledgeMemberService_CancelRenamed final : public RaidPledgeMemberService {
public:
    bool Do(CPledge& pledge, RaidServerInfo& server) override;
};

class RaidPledgeMemberService_AffectedByWar final : public RaidPledgeMemberService {
public:
    bool Do(CPledge& pledge, RaidServerInfo& server) override;
};

class RaidPledgeMemberService_UnequipCastleItem final : public RaidPledgeMemberService {
public:
    bool Do(CPledge& pledge, RaidServerInfo& server) override;
};

class RaidPledgeMemberService_SetCastleSiege final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SetCastleSiege(
        const std::int32_t (&castleIds)[9], const std::int32_t (&castleStatuses)[9]
    ) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::int32_t m_castleIds[9]; // +0x08
    std::int32_t m_castleStatuses[9]; // +0x2c
};

class RaidPledgeMemberService_SetFortressSiege final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SetFortressSiege(int defenceFortressId, int offenceFortressId) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    int m_defenceFortressId; // +0x08
    int m_offenceFortressId; // +0x0c
};

class RaidPledgeMemberService_SetAlliance final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SetAlliance(std::uint32_t allianceDatabaseId, bool masterPledge) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::uint32_t m_allianceDatabaseId; // +0x08
    std::int32_t m_masterPledge; // +0x0c
};

class RaidPledgeMemberService_SetRelationChanged final : public RaidPledgeMemberService {
public:
    bool Do(CPledge& pledge, RaidServerInfo& server) override;
};

class RaidPledgeMemberService_OpenAllWindowPacket final : public RaidPledgeMemberService {
public:
    bool Do(CPledge& pledge, RaidServerInfo& server) override;
};

class RaidPledgeMemberService_DismissPledge final : public RaidPledgeMemberService {
public:
    bool Do(CPledge& pledge, RaidServerInfo& server) override;
};

// Current Raid.cpp relay command. Its complete vtable has the sole Do slot.
// the two dwords keep grade then power in the outbound cddd payload order.
class RaidPledgeMemberService_UpdatePledgePower final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_UpdatePledgePower(int gradeId, std::uint32_t pledgePowerBits) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    int m_nGradeId; // +0x08
    std::uint32_t m_lPledgePowerBits; // +0x0c
};


class RaidPledgeMemberService_SetCrest final : public RaidPledgeMemberService {
public:
    explicit RaidPledgeMemberService_SetCrest(std::uint32_t crestId) noexcept : m_crestId(crestId) {}

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::uint32_t m_crestId; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10;
};

class RaidPledgeMemberService_SetEmblem final : public RaidPledgeMemberService {
public:
    explicit RaidPledgeMemberService_SetEmblem(std::uint32_t emblemId) noexcept : m_emblemId(emblemId) {}

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::uint32_t m_emblemId; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10;
};

class RaidPledgeMemberService_SetAllianceCrest final : public RaidPledgeMemberService {
public:
    explicit RaidPledgeMemberService_SetAllianceCrest(std::uint32_t allianceCrestId) noexcept :
        m_allianceCrestId(allianceCrestId) {}

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::uint32_t m_allianceCrestId; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10;
};

class RaidPledgeMemberService_SetDominionSiege final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SetDominionSiege(int dominionSiegeId, int dominionSiegeStatus) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    int m_nDominionSiegeId; // +0x08
    int m_nDominionSiegeStatus; // +0x0c
};

class RaidPledgeMemberService_SyncPledgeSkill final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SyncPledgeSkill(int skillId, int skillLevel) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    int m_skillId; // +0x08
    int m_skillLevel; // +0x0c
};

class RaidPledgeMemberService_SyncPledgeType final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SyncPledgeType(std::uint32_t characterDatabaseId, PledgeType pledgeType) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::uint32_t m_characterDatabaseId; // +0x08
    PledgeType m_pledgeType; // +0x0c
};

class RaidPledgeMemberService_SyncPledgeLevel final : public RaidPledgeMemberService {
public:
    explicit RaidPledgeMemberService_SyncPledgeLevel(int level) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    int m_level; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10{};
};

class RaidPledgeMemberService_SyncPledgeMastery final : public RaidPledgeMemberService {
public:
    RaidPledgeMemberService_SyncPledgeMastery(PledgeMasteryID::Type masteryId, int point) noexcept;

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    PledgeMasteryID::Type m_masteryId; // +0x08
    int m_point; // +0x0c
};

class RaidPledgeMemberService_SetParamChanged final : public RaidPledgeMemberService {
public:
    explicit RaidPledgeMemberService_SetParamChanged(std::int64_t flag) noexcept : m_flag(flag) {}

    bool Do(CPledge& pledge, RaidServerInfo& server) override;

private:
    std::int64_t m_flag; // +0x08
};


// Current Raid.cpp physical interface. Its complete vtable contains the
// single Do slot and neither the base nor the stack-owned concrete service
// has a virtual destructor.
class RaidAllianceService {
public:
    virtual bool Do(CAlliance& alliance, RaidServerInfo& server) = 0;
};

class RaidAllianceService_Dismiss final : public RaidAllianceService {
public:
    bool Do(CAlliance& alliance, RaidServerInfo& server) override;
};

class RaidAllianceService_AddPledgeMember final : public RaidAllianceService {
public:
    explicit RaidAllianceService_AddPledgeMember(std::uint32_t pledgeDatabaseId) noexcept :
        m_pledgeDatabaseId(pledgeDatabaseId) {}

    bool Do(CAlliance& alliance, RaidServerInfo& server) override;

private:
    std::uint32_t m_pledgeDatabaseId; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10;
};

class RaidAllianceService_DelPledgeMember final : public RaidAllianceService {
public:
    explicit RaidAllianceService_DelPledgeMember(std::uint32_t pledgeDatabaseId) noexcept :
        m_pledgeDatabaseId(pledgeDatabaseId) {}

    bool Do(CAlliance& alliance, RaidServerInfo& server) override;

private:
    std::uint32_t m_pledgeDatabaseId; // +0x08
    [[maybe_unused]] std::uint32_t m_alignment0cTo10;
};


class RaidClientManager : public Singleton<RaidClientManager> {
public:
    using ServerListMap = std::map<int, RaidServerInfo*>;
    using RaidGroupMapType = std::map<int, std::vector<int>>;
    using ExternalIPMap = std::map<int, std::uint32_t>;

    RaidClientManager() {
        m_hConnectThread = nullptr;
        m_nConnectThread = 0;
    }

    ~RaidClientManager() override {
        m_hConnectThread = nullptr;
        m_nConnectThread = 0;
    }

    void Load();
    void Init();
    void CreateThread();
    void ConnectToRaidServer();
    void Connected(int worldId, bool connected);
    void DeleteAllRaidUser(int worldId);
    void RaidReserveRequestPacket(int worldId, int characterDatabaseId, bool success);
    void RaidReturnToOriginPacket(int characterDatabaseId, int characterServerId, int raidServerWorldId);
    bool RaidRestartOnRaidServerPacket(int characterDatabaseId, int characterServerId);
    void RaidServerVersionPacket(int worldId, int result, int quota, const unsigned char* packet);
    void RaidUserConfirmedPacket(int characterDatabaseId);
    void AddRaidPledge(int worldId, int pledgeDatabaseId);
    void AddRaidPledge(User& user);

    std::uint32_t GetExternalIP(int worldId);
    RaidServerInfo* GetServerInfo(int worldId);
    RaidUserCountLong GetCurrentRaidUserCount();
    void ShowRaidInfo(User& user);
    RaidServerInfo* GetHostServerInfoForWorldRaid() const;
    bool RequestWorldRaidStart(std::uint32_t npcSid) const;
    void RequestWorldRaidStop(std::uint32_t npcSid) const;
    void RequestWorldRaidBroadcastEvent(int first, int second, int third) const;

    bool DisconnectRaidUser(int worldId, int characterDatabaseId);
    bool DeleteRaidReserved(int worldId, int characterDatabaseId, bool notifyRaidServer);
    void ReloadUserIfReturnedCompletely(User& user);
    void RestartUserIfCompletely(User& user);
    bool RequestRaidPledgeMemberService(CPledge& pledge, RaidPledgeMemberService& service);
    bool RequestPledgeChattingRelay(
        CPledge& pledge,
        int serverId,
        int characterDatabaseId,
        const char16_t* senderName,
        const char16_t* message,
        SayPacketType type,
        int excludedWorldId
    );
    bool RequestAllianceService(CAlliance& alliance, RaidAllianceService& service);
    bool RequestRaidReserve(int worldId, User* user);
    bool RequestRaidReserve(int worldId, User* user, FVector position);

private:
    std::uint32_t m_nConnectThread;
    void* m_hConnectThread;
    CYieldLock m_lock;
    ServerListMap m_ServerListMap;
    RaidGroupMapType m_mapRaidPledge;
    ExternalIPMap m_mapExternalIP;
};
