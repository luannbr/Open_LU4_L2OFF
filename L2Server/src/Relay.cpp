#include "Relay.h"
#include "Shared/src/ScopedLockGuards.h"

#include "CConfig.h"
#include "Castle.h"
#include "L2MobileNoti.h"
#include "PartyRoom.h"
#include "PartyRoomInfo.h"
#include "PartyRoomRegistry.h"
#include "Pledge.h"
#include "RelayClientSocket.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"

#include <array>
#include <cstring>
#include <limits>

namespace {


} // namespace

RelayClient* RelayClient::m_RelayClient = nullptr;
bool RelayClient::m_bSendPrevPartyRoomPacket = false;

RelayClient::RelayClient() :
    enabled(false),
    sendable(0),
    supressed(false),
    m_nConnectThread(0),
    m_hConnectThread(nullptr) {
    L2_GUARD_FUNCTION();
    g_RelaySetting.heartBeat = 10000;
}

RelayClient::~RelayClient() = default;

RelayClient* RelayClient::Inst() {
    if (m_RelayClient == nullptr) {
        m_RelayClient = new RelayClient;
    }
    return m_RelayClient;
}

bool RelayClient::Connect() {
    // Current L2Server 0x00a27630.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&sendable) != 0) {
        // The shipped diagnostic names the petition server.
        // Log.Add(
        //     LOG_ERROR,
        //     L"Already connected to petition server")
        // L2_LOG_ERROR(Log,
        //     L"Already connected to petition server")
        L2_LOG_WARN(Log, "Relay connection request skipped: the client is already marked sendable.");
        return false;
    }

    const runtime::NativeSocket nativeSocket = runtime::OpenTcpSocket();
    if (!runtime::ConnectTcpSocket(
            nativeSocket, g_RelaySetting.serverAddr, static_cast<std::uint16_t>(g_RelaySetting.serverPort)
        )) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't connect to relay server")
        // L2_LOG_ERROR(Log,
        //     L"Can't connect to relay server")
        const int socketError = portable::GetNativeSocketLastError();
        const std::uint32_t address = ntohl(g_RelaySetting.serverAddr.s_addr);
        L2_LOG_ERROR(
            Log,
            "Cannot %s relay connection to %u.%u.%u.%u:%u (socket error %d); closing the socket and marking the client "
            "unavailable.",
            nativeSocket == runtime::kInvalidNativeSocket ? "create" : "establish",
            static_cast<unsigned>((address >> 24) & 255U), static_cast<unsigned>((address >> 16) & 255U),
            static_cast<unsigned>((address >> 8) & 255U), static_cast<unsigned>(address & 255U),
            static_cast<unsigned>(static_cast<std::uint16_t>(g_RelaySetting.serverPort)), socketError
        );
        runtime::CloseTcpSocket(nativeSocket);
        runtime::raw_atomic::StoreI32(&sendable, 0);
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to relay server")
    // L2_LOG_INFO(Log,
    //     L"Connected to relay server")
    L2_LOG_INFO(Log, "Relay TCP connection established; transport setup and world registration still follow.");
    RelayClientSocket* const socket = new RelayClientSocket(nativeSocket);
    if (m_spRelaySocket) {
        m_spRelaySocket.SetObject(socket, ObjectRefType::FunctionChain);
    } else {
        CSPointer<RelayClientSocket> replacement(socket, ObjectRefType::FunctionChain);
        m_spRelaySocket = replacement;
    }

    const bool bound = portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, const std::size_t length) {
            return portable::FeedSocketRead(static_cast<RelayClientSocket*>(owner), data, length);
        },
        [](void* owner, const int error) { portable::FeedSocketTerminal(static_cast<RelayClientSocket*>(owner), error); }
    );
    if (!bound) {
        runtime::CloseTcpSocket(nativeSocket);
    }
    m_spRelaySocket->Initialize();
    runtime::raw_atomic::StoreI32(&sendable, 1);
    return true;
}

bool RelayClient::Init() {
    // Current L2Server 0x00a27924.
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_GUARD(mLock);
        bool result = false;
        RelayClient* const relayClient = Inst();
        if (runtime::raw_atomic::LoadI32Relaxed(&relayClient->sendable) == 0) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"Connecting relay server...")
            // Noise: The reconnect loop already reports connection failure or TCP establishment. An extra attempt-start
            // message repeats every retry. L2_LOG_INFO(Log,
            //     L"Connecting relay server...")
            result = Connect();
            runtime::raw_atomic::StoreI32(&relayClient->sendable, result ? 1 : 0);
            if (result && m_spRelaySocket) {
                m_spRelaySocket->Send("cd", 0, g_nWorldId);
            }
        }
        return result;
    }();
    return result;
}

void RelayClient::SendLogOnPacket(User* const user) {
    // Current L2Server.exe 0x00a282ac.
    L2_GUARD_FUNCTION();
    CIOSocket* const socket = m_spRelaySocket.get();
    if (sendable == 0 || socket == nullptr ||
        socket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RelaySocket is not valid - LogOnPacket")
        // L2_LOG_ERROR(Log,
        //     L"RelaySocket is not valid - LogOnPacket")
        L2_LOG_WARN(
            Log,
            "Cannot notify relay of player login: the client is not sendable or its socket is unavailable; the notification "
            "is skipped."
        );
        return;
    }
    if (user == nullptr) {
        // Log.Add(LOG_ERROR, L"Invalid User - LogOnPacket")
        // L2_LOG_ERROR(Log, L"Invalid User - LogOnPacket")
        L2_LOG_ERROR(Log, "Cannot notify relay of player login: the player is unavailable; the notification is skipped.");
        return;
    }

    const bool pcCafe = user->m_data->m_bPCCafeUser;
    const int premiumType = user->GetPremiumLevel() > 0 || pcCafe ? 2 : 0;
    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_CHAR_LOG_IN::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), std::u16string(user->GetOriginName()),
        user->m_data->m_nExp, user->m_data->m_nLevel, pcCafe, premiumType
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendLogOffPacket(User* const user) {
    // Current L2Server.exe 0x00a284d8.
    L2_GUARD_FUNCTION();
    CIOSocket* const socket = m_spRelaySocket.get();
    if (sendable == 0 || socket == nullptr ||
        socket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RelaySocket is not valid - LogOffPacket")
        // L2_LOG_ERROR(Log,
        //     L"RelaySocket is not valid - LogOffPacket")
        L2_LOG_WARN(
            Log,
            "Cannot notify relay of player logout: the client is not sendable or its socket is unavailable; the "
            "notification is skipped."
        );
        return;
    }
    if (user == nullptr) {
        // Log.Add(LOG_ERROR, L"Invalid User - LogOffPacket")
        // L2_LOG_ERROR(Log, L"Invalid User - LogOffPacket")
        L2_LOG_ERROR(Log, "Cannot notify relay of player logout: the player is unavailable; the notification is skipped.");
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_CHAR_LOG_OUT::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), std::u16string(user->GetOriginName()),
        user->m_data->m_nExp, user->m_data->m_nLevel, user->m_nPlayTime
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendUserInfo(User* const user) {
    // Current L2Server.exe 0x00a2ac10.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(LOG_ERROR, L"Invalid User - SendUserInfo")
        // L2_LOG_ERROR(Log, L"Invalid User - SendUserInfo")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of player information: the player is unavailable; the notification is skipped."
        );
        return;
    }

    const bool pcCafe = user->m_data->m_bPCCafeUser;
    const int premiumType = user->GetPremiumLevel() > 0 || pcCafe ? 2 : 0;
    const int privateStoreType = user->GetPrivateStoreType();
    std::vector<MakeBufferL2MobileNoti::L2M_USER_PING::ItemListTuple> itemList;

    User::PrivateStoreMap store;
    if (privateStoreType == PST_OPEN) {
        store = user->GetPrivateStoreMap(false);
    } else if (privateStoreType == PST_WHOLESALE_OPEN) {
        store = user->GetPrivateStoreMap(true);
    }
    itemList.reserve(store.size());
    for (const auto& entry : store) {
        const CPrivateStoreData* const item = entry.second;
        if (item != nullptr) {
            itemList.emplace_back(item->nClassId, item->nItemDBID, item->nPrice, item->nCount);
        }
    }

    const std::int64_t gainedExperience =
        runtime::raw_atomic::ExchangeI64(&user->m_UserStatData.m_UserStat[UserStat::US_EXP], 0);
    const std::int64_t gainedSp = runtime::raw_atomic::ExchangeI64(&user->m_UserStatData.m_UserStat[UserStat::US_SP], 0);
    const std::int64_t gainedAdena =
        runtime::raw_atomic::ExchangeI64(&user->m_UserStatData.m_UserStat[UserStat::US_ADENA], 0);
    const FVector position = user->GetPos();

    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_USER_PING::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), std::u16string(user->GetOriginName()),
        user->m_data->m_nExp, user->m_data->m_nLevel, pcCafe, premiumType, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), gainedExperience,
        gainedSp, gainedAdena, privateStoreType, itemList
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendByPassPacket(const int length, unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    CIOSocket* const socket = m_spRelaySocket.get();
    if (length > 0 && packet != nullptr && sendable != 0 && socket != nullptr &&
        socket->m_hSocket.load(std::memory_order_acquire) != std::numeric_limits<std::uint64_t>::max()) {
        socket->Send("cb", 1, length, packet);
    }
}

void RelayClient::SendDoorDestructPacket(const int residenceId, const int doorType, const int damageGrade) {
    // Current L2Server.exe 0x00a26ac8.
    L2_GUARD_FUNCTION();
    if (residenceId >= 0 && doorType >= 0 && damageGrade >= 0) {
        std::array<unsigned char, 0x5000> packet;
        packet[0] = 0;
        std::memset(packet.data() + 1, 0, packet.size() - 1);
        const int length = MakeBufferL2MobileNoti::L2M_DOOR_DESTRUCT::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), g_nWorldId, residenceId, doorType, damageGrade
        );
        SendByPassPacket(length, packet.data());
    }
}

void RelayClient::SendTowerDestructPacket(
    const std::int64_t x, const std::int64_t y, const std::int64_t z, const int residenceId, const int towerId
) {
    // Current L2Server.exe 0x00a26990.
    L2_GUARD_FUNCTION();
    if (residenceId >= 0 && towerId >= 0) {
        std::array<unsigned char, 0x5000> packet{};
        const int length = MakeBufferL2MobileNoti::L2M_TOWER_DESTRUCT::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), g_nWorldId, x, y, z, residenceId, towerId
        );
        SendByPassPacket(length, packet.data());
    }
}

void RelayClient::SendSiegeStartPacket(const int residenceId) {
    // Current L2Server.exe 0x00a26be0.
    L2_GUARD_FUNCTION();
    if (residenceId >= 0) {
        std::array<unsigned char, 0x5000> packet;
        packet[0] = 0;
        std::memset(packet.data() + 1, 0, packet.size() - 1);
        const int length = MakeBufferL2MobileNoti::L2M_SIEGE_START::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), g_nWorldId, residenceId
        );
        SendByPassPacket(length, packet.data());
    }
}

void RelayClient::SendSiegeEndPacket(const int residenceId) {
    // Current L2Server.exe 0x00a26cd8.
    L2_GUARD_FUNCTION();
    if (residenceId >= 0) {
        std::array<unsigned char, 0x5000> packet;
        packet[0] = 0;
        std::memset(packet.data() + 1, 0, packet.size() - 1);
        const int length = MakeBufferL2MobileNoti::L2M_SIEGE_END::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), g_nWorldId, residenceId
        );
        SendByPassPacket(length, packet.data());
    }
}

void RelayClient::SendKillScoreInSeigePacket(CCastle* const castle) {
    // Current L2Server.exe 0x00a26dd0. The shipped spelling is "Seige".
    L2_GUARD_FUNCTION();
    if (castle == nullptr) {
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const int residenceId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
    const int length = MakeBufferL2MobileNoti::L2M_SIEGE_SCORE::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), castle->m_nWorldId, residenceId, castle->GetAttackerScore(),
        castle->GetDefenderScore()
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendCommissionTradeBuyPacket(const int characterId) {
    // Current L2Server 0x00a2726c.
    L2_GUARD_FUNCTION();
    if (characterId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid value [%d] - SendCommissionTradeBuyPacket",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid value [%d] - SendCommissionTradeBuyPacket",
        //     characterId)
        L2_LOG_ERROR(
            Log, "Cannot report commission purchase to relay: character ID %d is negative; the notification is skipped.",
            characterId
        );
        return;
    }
    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_COMMISSION_TRADE_BUY::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), g_nWorldId, characterId
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendGivePostResult(
    const int presentId, const int characterDatabaseId, const int itemTemplateId, const int itemAmount, const int result
) {
    // Current L2Server.exe 0x00a27380.
    L2_GUARD_FUNCTION();
    if (presentId < 0 || characterDatabaseId < 0 || itemTemplateId < 0 || itemAmount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid arg - SendGivePostResult")
        // L2_LOG_ERROR(Log,
        //     L"Invalid arg - SendGivePostResult")
        L2_LOG_ERROR(
            Log,
            "Cannot report gift-mail result to relay: a value is negative (present %d, character %d, item %d, amount %d); "
            "the notification is skipped.",
            presentId, characterDatabaseId, itemTemplateId, itemAmount
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_GIVE_POST_RESULT::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), presentId, g_nWorldId, characterDatabaseId, itemTemplateId,
        static_cast<std::int64_t>(itemAmount), result == 1
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendSpawnNPCPacket(const int npcId) {
    // Current L2Server.exe 0x00a267b0.
    L2_GUARD_FUNCTION();
    std::array<unsigned char, 0x5000> packet;
    packet[0] = 0;
    std::memset(packet.data() + 1, 0, packet.size() - 1);
    const int length = MakeBufferL2MobileNoti::L2M_CREATE_NPC::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), g_nWorldId, npcId
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendDespawnNPCPacket(const int npcId) {
    // Current L2Server.exe 0x00a268a0.
    L2_GUARD_FUNCTION();
    std::array<unsigned char, 0x5000> packet;
    packet[0] = 0;
    std::memset(packet.data() + 1, 0, packet.size() - 1);
    const int length = MakeBufferL2MobileNoti::L2M_DESPAWN_NPC::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), g_nWorldId, npcId
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendDieBossNPCPacket(const int npcClassId) {
    // Current L2Server.exe 0x00a27018.
    L2_GUARD_FUNCTION();
    if (npcClassId < 0) {
        return;
    }
    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_DIE_BOSS_NPC::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), g_nWorldId, npcClassId
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendBossRegenTime(const int npcClassId, const int respawnTime) {
    // Current L2Server.exe 0x00a26f08.
    L2_GUARD_FUNCTION();
    if (npcClassId >= 0 && respawnTime >= 0) {
        std::array<unsigned char, 0x5000> packet{};
        const int length = MakeBufferL2MobileNoti::L2M_BOSS_REGEN::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), g_nWorldId, npcClassId, static_cast<std::int64_t>(respawnTime)
        );
        SendByPassPacket(length, packet.data());
    }
}

void RelayClient::SendEnterOlympiad(User* const user) {
    // Current L2Server 0x00a28fb8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendEnterOlympiad")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendEnterOlympiad")
        L2_LOG_ERROR(Log, "Cannot notify relay of Olympiad entry: the player is unavailable; the notification is skipped.");
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_ENTER_OLYMPIAD::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendEnterPledgeGame(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendEnterOlympiad")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendEnterOlympiad")
        L2_LOG_ERROR(Log, "Cannot notify relay of clan-game entry: the player is unavailable; the notification is skipped.");
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_ENTER_PLEDGEGAME::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendDeclarePledgeWarPacket(CPledge* const declaringPledge, CPledge* const declaredPledge) {
    // Current L2Server 0x00a27110.
    L2_GUARD_FUNCTION();
    if (declaringPledge == nullptr || declaredPledge == nullptr) {
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_PLEDGE_WAR_DECLARE::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), declaringPledge->GetWorldId(),
        static_cast<int>(declaringPledge->GetDbId()), declaredPledge->GetWorldId(),
        static_cast<int>(declaredPledge->GetDbId()), static_cast<int>(declaringPledge->DidDeclareWarBoth(declaredPledge)) + 1
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendWarPacket(User* const user, const int place) {
    // Current L2Server.exe 0x00a29308.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendWarPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendWarPacket")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of siege participation: the player is unavailable; the notification is skipped."
        );
        return;
    }
    if (place < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid Place[%d] - SendWarPacket - 1",
        //     place)
        // L2_LOG_ERROR(Log,
        //     L"Invalid Place[%d] - SendWarPacket - 1",
        //     place)
        L2_LOG_ERROR(
            Log, "Cannot report siege participation to relay: place %d is outside 0 through 2; the notification is skipped.",
            place
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    if (place > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid Place %d - SendWarPacket - 2",
        //     place)
        // L2_LOG_ERROR(Log,
        //     L"Invalid Place %d - SendWarPacket - 2",
        //     place)
        L2_LOG_ERROR(
            Log, "Cannot report siege participation to relay: place %d is outside 0 through 2; the notification is skipped.",
            place
        );
        return;
    }
    int length = 0;
    if (place == 0) {
        const std::u16string characterName = std::u16string(user->GetOriginName());
        length = MakeBufferL2MobileNoti::L2M_ENTER_CASTLE::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(),
            static_cast<int>(user->GetAccountID()), static_cast<int>(user->GetDBID()),
            static_cast<int>(user->m_data->m_nPledgeID), characterName
        );
    } else if (place == 1) {
        const std::u16string characterName = std::u16string(user->GetOriginName());
        length = MakeBufferL2MobileNoti::L2M_ENTER_FORTRESS::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(),
            static_cast<int>(user->GetAccountID()), static_cast<int>(user->GetDBID()),
            static_cast<int>(user->m_data->m_nPledgeID), characterName
        );
    } else {
        const std::u16string characterName = std::u16string(user->GetOriginName());
        length = MakeBufferL2MobileNoti::L2M_ENTER_AGIT::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(),
            static_cast<int>(user->GetAccountID()), static_cast<int>(user->GetDBID()),
            static_cast<int>(user->m_data->m_nPledgeID), characterName
        );
    }
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPrivateStoreOpen(User* const user, std::vector<PrivateStoreItemData>& itemData) {
    // Current L2Server 0x00a29f24.
    L2_GUARD_FUNCTION();
    CIOSocket* const socket = m_spRelaySocket.get();
    if (sendable == 0 || socket == nullptr ||
        socket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RelaySocket is not valid - SendPrivateStoreOpen")
        // L2_LOG_ERROR(Log,
        //     L"RelaySocket is not valid - SendPrivateStoreOpen")
        L2_LOG_WARN(
            Log,
            "Cannot notify relay of private sell-store opening: the client is not sendable or its socket is unavailable; "
            "the notification is skipped."
        );
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendPrivateStoreOpen")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendPrivateStoreOpen")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of private sell-store opening: the player is unavailable; the notification is skipped."
        );
        return;
    }

    const int privateStoreType = user->GetPrivateStoreType();
    const int itemCount = static_cast<int>(itemData.size());
    std::vector<MakeBufferL2MobileNoti::L2M_PRIVATESTORE_OPEN::ItemListTuple> itemList;
    for (int index = 0; index < itemCount; ++index) {
        const PrivateStoreItemData& item = itemData.back();
        itemList.emplace_back(item.nItemClassId, item.nItemDbId, item.nPrice, item.nCount);
        itemData.pop_back();
    }

    const std::int64_t z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const std::int64_t y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const std::int64_t x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_PRIVATESTORE_OPEN::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), std::u16string(user->GetOriginName()),
        x, y, z, itemCount, privateStoreType, itemList
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPrivateStoreBuyOpenPacket(User* const user, std::vector<PrivateStoreItemData>& itemData) {
    // Current L2Server 0x00a2a240.
    L2_GUARD_FUNCTION();
    CIOSocket* const socket = m_spRelaySocket.get();
    if (sendable == 0 || socket == nullptr ||
        socket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RelaySocket is not valid - SendPrivateStoreBuyOpenPacket")
        // L2_LOG_ERROR(Log,
        //     L"RelaySocket is not valid - SendPrivateStoreBuyOpenPacket")
        L2_LOG_WARN(
            Log,
            "Cannot notify relay of private buy-store opening: the client is not sendable or its socket is unavailable; the "
            "notification is skipped."
        );
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendPrivateStoreBuyOpenPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendPrivateStoreBuyOpenPacket")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of private buy-store opening: the player is unavailable; the notification is skipped."
        );
        return;
    }
    if (itemData.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"vItemData is empty - SendPrivateStoreBuyOpenPacket")
        // L2_LOG_ERROR(Log,
        //     L"vItemData is empty - SendPrivateStoreBuyOpenPacket")
        L2_LOG_ERROR(
            Log, "Cannot report private buy-store opening to relay: the item list is empty; the notification is skipped."
        );
        return;
    }

    const int privateStoreType = user->GetPrivateStoreType();
    const int itemCount = static_cast<int>(itemData.size());
    std::vector<MakeBufferL2MobileNoti::L2M_PRIVATESTORE_OPEN::ItemListTuple> itemList;
    for (int index = 0; index < itemCount; ++index) {
        const PrivateStoreItemData& item = itemData.back();
        itemList.emplace_back(item.nItemClassId, 0, item.nPrice, item.nCount);
        itemData.pop_back();
    }

    const std::int64_t z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const std::int64_t y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const std::int64_t x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_PRIVATESTORE_OPEN::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), std::u16string(user->GetOriginName()),
        x, y, z, itemCount, privateStoreType, itemList
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPrivateStoreBuyPacket(
    User* const seller,
    User* const buyer,
    RelayPrivateStoreMap& remainingItems,
    PSStoreBuyData* const boughtItems,
    const int boughtItemCount
) {
    // Current L2Server 0x00a2a580.
    L2_GUARD_FUNCTION();
    if (boughtItemCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nBuyItemCount<=0 - SendPrivateStoreBuyPacket")
        // L2_LOG_ERROR(Log,
        //     L"nBuyItemCount<=0 - SendPrivateStoreBuyPacket")
        L2_LOG_ERROR(
            Log,
            "Cannot report a purchase from a private sell-store to relay: bought-item count %d must be positive; the "
            "notification is skipped.",
            boughtItemCount
        );
        return;
    }
    const int remainingItemCount = static_cast<int>(remainingItems.size());
    if (remainingItemCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nRemainItemCount<=0 - SendPrivateStoreBuyPacket")
        // L2_LOG_ERROR(Log,
        //     L"nRemainItemCount<=0 - SendPrivateStoreBuyPacket")
        L2_LOG_ERROR(
            Log,
            "Cannot report a purchase from a private sell-store to relay: remaining-item count converted to %d from list "
            "size %llu; the notification is skipped.",
            remainingItemCount, static_cast<unsigned long long>(remainingItems.size())
        );
        return;
    }
    if (seller == nullptr || buyer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendPrivateStoreBuyPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendPrivateStoreBuyPacket")
        L2_LOG_ERROR(
            Log,
            "Cannot report a purchase from a private sell-store to relay: seller or buyer is unavailable; the notification "
            "is skipped."
        );
        return;
    }

    std::vector<MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY::BuyItemListTuple> boughtList;
    for (int index = 0; index < boughtItemCount; ++index) {
        const PSStoreBuyData& item = boughtItems[index];
        boughtList.emplace_back(item.m_ClassId, static_cast<int>(item.m_DBId), item.m_Price, item.m_Count);
    }

    std::vector<MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY::RemainItemListTuple> remainingList;
    for (const auto& entry : remainingItems) {
        const CPrivateStoreData* const item = entry.second;
        remainingList.emplace_back(item->nClassId, item->nItemDBID, item->nPrice, item->nCount);
    }

    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), seller->m_data->GetWorldId(),
        static_cast<int>(seller->GetAccountID()), static_cast<int>(seller->GetDBID()),
        static_cast<int>(seller->m_data->m_nPledgeID), std::u16string(seller->GetOriginName()), buyer->m_data->GetWorldId(),
        static_cast<int>(buyer->GetAccountID()), static_cast<int>(buyer->GetDBID()),
        static_cast<int>(buyer->m_data->m_nPledgeID), std::u16string(buyer->GetOriginName()), boughtItemCount, boughtList,
        seller->GetPrivateStoreType(), remainingItemCount, remainingList
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPrivateStoreBuyBuyPacket(
    User* const buyer, User* const seller, CPrivateStoreData* const boughtItems, const int boughtItemCount
) {
    // Current L2Server 0x00a2b06c.
    L2_GUARD_FUNCTION();
    if (boughtItemCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nBuyCount <= 0 [%d] - SendPrivateStoreBuyBuyPacket",
        //     boughtItemCount)
        // L2_LOG_ERROR(Log,
        //     L"nBuyCount <= 0 [%d] - SendPrivateStoreBuyBuyPacket",
        //     boughtItemCount)
        L2_LOG_ERROR(
            Log,
            "Cannot report a sale to a private buy-store to relay: item count %d must be positive; the notification is "
            "skipped.",
            boughtItemCount
        );
        return;
    }
    if (buyer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid pBuyerUser - SendPrivateStoreBuyBuyPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid pBuyerUser - SendPrivateStoreBuyBuyPacket")
        L2_LOG_ERROR(
            Log,
            "Cannot report a sale to a private buy-store to relay: the buyer is unavailable; the notification is skipped."
        );
        return;
    }
    if (seller == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid pSellerUser - SendPrivateStoreBuyBuyPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid pSellerUser - SendPrivateStoreBuyBuyPacket")
        L2_LOG_ERROR(
            Log,
            "Cannot report a sale to a private buy-store to relay: the seller is unavailable; the notification is skipped."
        );
        return;
    }

    const RelayPrivateStoreMap remainingItems = buyer->GetPrivateStoreBuyMap();
    std::vector<MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY_BUY::RemainBuyItemListTuple> remainingList;
    int remainingItemCount = 0;
    for (const auto& entry : remainingItems) {
        const CPrivateStoreData* const item = entry.second;
        remainingList.emplace_back(item->nClassId, 0, item->nPrice, item->nCount);
        ++remainingItemCount;
    }

    std::vector<MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY_BUY::SellItemListTuple> boughtList;
    for (int index = 0; index < boughtItemCount; ++index) {
        const CPrivateStoreData& item = boughtItems[index];
        boughtList.emplace_back(item.nClassId, item.nItemDBID, item.nPrice, item.nCount);
    }

    std::array<unsigned char, 0x5000> packet{};
    const int length = MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY_BUY::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), buyer->m_data->GetWorldId(), static_cast<int>(buyer->GetAccountID()),
        static_cast<int>(buyer->GetDBID()), static_cast<int>(buyer->m_data->m_nPledgeID),
        std::u16string(buyer->GetOriginName()), seller->m_data->GetWorldId(), static_cast<int>(seller->GetAccountID()),
        static_cast<int>(seller->GetDBID()), static_cast<int>(seller->m_data->m_nPledgeID),
        std::u16string(seller->GetOriginName()), boughtItemCount, boughtList, remainingItemCount, remainingList
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPrivateStoreClose(User* const user) {
    // Current L2Server 0x00a28930.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendPrivateStoreClose")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendPrivateStoreClose")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of private-store closure: the player is unavailable; the notification is skipped."
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_PRIVATESTORE_CLOSE::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendFishingItemPakcet(
    User* const user, const int itemId, const std::int64_t experience, const std::int64_t sp
) {
    // Current L2Server 0x00a28cb8. The misspelling is the original ABI.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendFishingItemPakcet")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendFishingItemPakcet")
        L2_LOG_ERROR(Log, "Cannot notify relay of fishing reward: the player is unavailable; the notification is skipped.");
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_FISHING_ITEM::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName, itemId, experience, sp
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendChangePledgeAnnounce(const int pledgeId, const SharedUtilsWideChar* const content) {
    // Current L2Server 0x00a28e74.
    L2_GUARD_FUNCTION();
    if (pledgeId < 0 || content == nullptr) {
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string targetContent(content, content + std::char_traits<SharedUtilsWideChar>::length(content));
    const int length = MakeBufferL2MobileNoti::L2M_CHANAGE_PLEDGE_ANNOUNCE::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), g_nWorldId, pledgeId, targetContent
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendEnterRaidServer(User* const user) {
    // Current L2Server 0x00a2963c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendEnterRaidServer")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendEnterRaidServer")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of raid-server entry: the player is unavailable; the notification is skipped."
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_ENTER_RAID_SERVER::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendGetRaidBossPointPacket(User* const user, const int npcClassId, const int point) {
    // Current L2Server 0x00a297e4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendGetRaidBossPointPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendGetRaidBossPointPacket")
        L2_LOG_ERROR(
            Log, "Cannot notify relay of raid-boss points: the player is unavailable; the notification is skipped."
        );
        return;
    }
    if (npcClassId < 0 || point < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid value [%d] [%d] - SendGetRaidBossPointPacket",
        //     npcClassId,
        //     point)
        // L2_LOG_ERROR(Log,
        //     L"Invalid value [%d] [%d] - SendGetRaidBossPointPacket",
        //     npcClassId,
        //     point)
        L2_LOG_ERROR(
            Log,
            "Cannot report raid-boss points to relay: NPC class %d or points %d is negative; the notification is skipped.",
            npcClassId, point
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_GET_RAID_POINT::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName, npcClassId, point,
        user->GetPoint(8)
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPcKillPacket(User* const dieUser, User* const killer, const int type) {
    // Current L2Server 0x00a286dc.
    L2_GUARD_FUNCTION();
    CIOSocket* const socket = m_spRelaySocket.get();
    if (sendable == 0 || socket == nullptr ||
        socket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RelaySocket is not valid - PCKillPacket")
        // L2_LOG_ERROR(Log,
        //     L"RelaySocket is not valid - PCKillPacket")
        L2_LOG_WARN(
            Log,
            "Cannot notify relay of player kill: the client is not sendable or its socket is unavailable; the notification "
            "is skipped."
        );
        return;
    }
    if (dieUser == nullptr || killer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - PCKillPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - PCKillPacket")
        L2_LOG_ERROR(
            Log, "Cannot report player kill to relay: the victim or killer is unavailable; the notification is skipped."
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string dieName = std::u16string(dieUser->GetOriginName());
    const std::u16string killerName = std::u16string(killer->GetOriginName());
    const int length = MakeBufferL2MobileNoti::L2M_PC_KILL_PC::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), dieUser->m_data->GetWorldId(), static_cast<int>(dieUser->GetDBID()),
        dieName, static_cast<int>(dieUser->m_data->m_nPledgeID), killer->m_data->GetWorldId(),
        static_cast<int>(killer->GetDBID()), killerName, static_cast<int>(killer->m_data->m_nPledgeID), type
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendSayPledge(CSPointer<User> user, const char16_t* const message) {
    // Current L2Server 0x00a27d24.
    User* const rawUser = user.get();
    if (rawUser == nullptr || rawUser->IsDeleting() || rawUser->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - RelayClient::SendSayPledge")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - RelayClient::SendSayPledge")
        L2_LOG_WARN(
            Log,
            "Cannot forward clan chat to relay: the player is unavailable or being deleted; the notification is skipped."
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    User* const characterDatabaseUser = user.operator->();
    const std::u16string chatMessage = Utf16Text(message).String();
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int pledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    const int worldId = user->m_data->m_nWorldId;
    const int characterDatabaseId = static_cast<int>(characterDatabaseUser->GetDBID());
    const int length = MakeBufferL2MobileNoti::L2M_SAY_PLEDGE::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), worldId, pledgeId, characterDatabaseId, characterName, chatMessage
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendSayAlliance(CSPointer<User> user, const char16_t* const message) {
    // Current L2Server 0x00a27fd4.
    User* const rawUser = user.get();
    if (rawUser == nullptr || rawUser->IsDeleting() || rawUser->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - RelayClient::SendSayAlliance")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - RelayClient::SendSayAlliance")
        L2_LOG_WARN(
            Log,
            "Cannot forward alliance chat to relay: the player is unavailable or being deleted; the notification is skipped."
        );
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    User* const characterDatabaseUser = user.operator->();
    const std::u16string chatMessage = Utf16Text(message).String();
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const int pledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    User* const allianceDatabaseUser = user.operator->();
    const int worldId = user->m_data->m_nWorldId;
    const int characterDatabaseId = static_cast<int>(characterDatabaseUser->GetDBID());
    const int allianceDatabaseId = allianceDatabaseUser->GetAllianceDbId();
    const int length = MakeBufferL2MobileNoti::L2M_SAY_ALLIANCE::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), worldId, allianceDatabaseId, pledgeId, characterDatabaseId,
        characterName, chatMessage
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendWorldChatPacket(User* const user, const char16_t* const message) {
    // Current L2Server 0x00a28ad8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid User - SendWorldChatPacket")
        // L2_LOG_ERROR(Log,
        //     L"Invalid User - SendWorldChatPacket")
        L2_LOG_ERROR(Log, "Cannot notify relay of world chat: the player is unavailable; the notification is skipped.");
        return;
    }

    std::array<unsigned char, 0x5000> packet{};
    const std::u16string characterName = std::u16string(user->GetOriginName());
    const std::u16string contents(message != nullptr ? message : u"");
    const int length = MakeBufferL2MobileNoti::L2M_WORLD_CHAT::MakeSendBuffer(
        packet.data(), static_cast<int>(packet.size()), user->m_data->GetWorldId(), static_cast<int>(user->GetAccountID()),
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), characterName, contents
    );
    SendByPassPacket(length, packet.data());
}

void RelayClient::SendPartyMatchingInfo() {
    // Current L2Server 0x00a2b5dc.
    L2_GUARD_FUNCTION();

    CPartyRoomRegistry::PartyRoomList rooms;
    rooms.reserve(500);
    rooms.clear();
    CPartyRoomRegistry::inst().GetRoomList(rooms);

    using PartyRoomTuple = MakeBufferL2MobileNoti::L2M_PARTY_MATCHING_LIST::partyRoomInfoTuple;
    std::vector<PartyRoomTuple> packetRooms;
    packetRooms.reserve(150);
    packetRooms.clear();

    bool firstPacket = true;
    const auto sendPacket = [&]() {
        std::array<unsigned char, 0x5000> packet{};
        const int length = MakeBufferL2MobileNoti::L2M_PARTY_MATCHING_LIST::MakeSendBuffer(
            packet.data(), static_cast<int>(packet.size()), g_nWorldId, static_cast<int>(packetRooms.size()), packetRooms,
            firstPacket
        );
        SendByPassPacket(length, packet.data());
        packetRooms.clear();
        firstPacket = false;
    };

    for (CSPointer<CPartyRoom>& roomPointer : rooms) {
        CPartyRoom* const room = roomPointer.get();
        if (room == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] referencing NULL object.",
            //     L"Relay.cpp",
            //     0x75)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] referencing NULL object.",
            //     L"Relay.cpp",
            //     0x75)
            L2_LOG_WARN(Log, "Relay party-room list contains an unavailable room; skipping it and continuing the list.");
            continue;
        }

        CPartyRoomInfo info(room->GetPartyRoomInfo());
        CSPointer<User> master = info.GetMasterUser();
        if (!master) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] referencing NULL object.",
            //     L"Relay.cpp",
            //     0x75)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] referencing NULL object.",
            //     L"Relay.cpp",
            //     0x75)
            L2_LOG_WARN(
                Log, "Relay party-room list skips room %d because its leader is unavailable; list assembly continues.",
                info.GetId()
            );
            continue;
        }

        packetRooms.push_back(PartyRoomTuple(
            std::u16string(master->GetOriginName()), info.GetLowerLevelLimit(), info.GetUpperLevelLimit(),
            info.GetMaxMemberLimit(), info.GetTitle(), room->GetMemberCount()
        ));
        if (packetRooms.size() == 150) {
            sendPacket();
        }
    }

    if (!packetRooms.empty() || firstPacket) {
        sendPacket();
    }
}
