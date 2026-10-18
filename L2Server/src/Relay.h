#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <netinet/in.h>
#endif

class CCastle;
class CPledge;
class User;
class CL2Time;
class RelayClientSocket;
struct CPrivateStoreData;
struct PrivateStoreItemData;
struct PSStoreBuyData;

using RelayPrivateStoreMap = std::map<int, CPrivateStoreData*>;

struct RelayStruct {
    ::in_addr serverAddr{}; // +0x00
    std::int16_t serverPort{}; // +0x04
    std::uint16_t alignment06{}; // +0x06
    std::int32_t heartBeat{}; // +0x08
};


extern RelayStruct g_RelaySetting;
extern bool g_bConnectRelayServer;


class RelayClient {
public:
    RelayClient();
    ~RelayClient();

    static RelayClient* Inst();

    bool Connect();
    bool Init();

    void SendLogOnPacket(User* user);
    void SendLogOffPacket(User* user);
    void SendUserInfo(User* user);
    void SendByPassPacket(int length, unsigned char* packet);
    void SendEnterOlympiad(User* user);
    void SendEnterPledgeGame(User* user);
    void SendWarPacket(User* user, int place);
    void SendDeclarePledgeWarPacket(CPledge* declaringPledge, CPledge* declaredPledge);
    void SendPrivateStoreOpen(User* user, std::vector<PrivateStoreItemData>& itemData);
    void SendPrivateStoreBuyOpenPacket(User* user, std::vector<PrivateStoreItemData>& itemData);
    void SendPrivateStoreBuyPacket(
        User* seller, User* buyer, RelayPrivateStoreMap& remainingItems, PSStoreBuyData* boughtItems, int boughtItemCount
    );
    void SendPrivateStoreBuyBuyPacket(User* buyer, User* seller, CPrivateStoreData* boughtItems, int boughtItemCount);
    void SendPrivateStoreClose(User* user);
    void SendFishingItemPakcet(User* user, int itemId, std::int64_t experience, std::int64_t sp);
    void SendChangePledgeAnnounce(int pledgeId, const SharedUtilsWideChar* content);
    void SendEnterRaidServer(User* user);
    void SendGetRaidBossPointPacket(User* user, int npcClassId, int point);
    void SendPcKillPacket(User* dieUser, User* killer, int type);
    void SendWorldChatPacket(User* user, const char16_t* message);
    void SendSayPledge(CSPointer<User> user, const char16_t* message);
    void SendSayAlliance(CSPointer<User> user, const char16_t* message);
    void SendDoorDestructPacket(int residenceId, int doorType, int damageGrade);
    void SendTowerDestructPacket(std::int64_t x, std::int64_t y, std::int64_t z, int residenceId, int towerId);
    void SendSiegeStartPacket(int residenceId);
    void SendSiegeEndPacket(int residenceId);
    void SendKillScoreInSeigePacket(CCastle* castle);
    void SendCommissionTradeBuyPacket(int characterId);
    void SendGivePostResult(int presentId, int characterDatabaseId, int itemTemplateId, int itemAmount, int result);
    void SendSpawnNPCPacket(int npcId);
    void SendDespawnNPCPacket(int npcId);
    void SendDieBossNPCPacket(int npcClassId);
    void SendBossRegenTime(int npcClassId, int respawnTime);
    void SendPartyMatchingInfo();

private:
    friend class CL2Time;
    friend class RelayClientSocket;

    static RelayClient* m_RelayClient;
    static bool m_bSendPrevPartyRoomPacket;

    bool enabled{}; // +0x00
    std::array<std::byte, 0x03> alignment01To04{}; // +0x01..+0x03
    std::int32_t sendable{}; // +0x04
    bool supressed{}; // +0x08
    std::array<std::byte, 0x07> alignment09To10{}; // +0x09..+0x0f
    RWLock lock; // +0x10..+0x47
    CYieldLock mLock; // +0x48..+0x57
    std::uint32_t m_nConnectThread{}; // +0x58
    std::uint32_t alignment5cTo60{}; // +0x5c
    void* m_hConnectThread{}; // +0x60

    CSPointer<RelayClientSocket> m_spRelaySocket; // +0x68..+0x77
};


struct PrivateStoreItemData {
    PrivateStoreItemData(int itemClassId, std::int64_t count, std::int64_t price, int itemDbId) :
        nItemClassId(itemClassId), nItemDbId(itemDbId), nCount(count), nPrice(price) {}

    int nItemClassId;
    int nItemDbId;
    std::int64_t nCount;
    std::int64_t nPrice;
};

static_assert(std::is_trivially_copyable_v<PrivateStoreItemData>);
static_assert(std::is_trivially_destructible_v<PrivateStoreItemData>);

struct PSStoreBuyData {
    PSStoreBuyData() : m_ServerId(0), m_Count(0), m_Price(0), m_ClassId(0), m_DBId(0) {}

    std::int64_t m_ServerId; // +0x00
    std::int64_t m_Count; // +0x08
    std::int64_t m_Price; // +0x10
    int m_ClassId; // +0x18
    std::int64_t m_DBId; // +0x20
};

static_assert(std::is_trivially_copyable_v<PSStoreBuyData>);
static_assert(std::is_trivially_destructible_v<PSStoreBuyData>);
