#include "DBNPC.h"

#include "DBNPCSocket.h"
#include "LottoGame.h"
#include "NPC.h"
#include "NpcMaker.h"
#include "Portable/DBNPCConnection.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace l2npc {

// Current initializer order is fixed by 0x005852a0, 0x005852c0 and
// 0x0058532c. Preserve it because shutdown runs the matching destructors in
// reverse order.
CDBNPC dbnpc;
std::map<int, int> g_dbsaving_map;
CIOCriticalSection g_dbsaving_map_lock;

CDBNPC::CDBNPC() {
    // Current EXE 0x0041e854 constructs CIOObject, both CYieldLocks and the
    // DBProcessQueue before publishing the exact TLS call-stack frame.
    L2_GUARD_FUNCTION();
    m_pSocket = nullptr;
    m_nDBSaveTick = 0;
    m_nDBSaveCount = 0;
}

CDBNPC::~CDBNPC() {
    // Current EXE 0x0041d6d4 clears the non-owning socket slot before
    // destroying DBProcessQueue and the CIOObject base.
    m_pSocket = nullptr;
}

bool CDBNPC::Init() {
    // Current EXE 0x0041ce6c is exactly this forwarding call.
    L2_GUARD_FUNCTION();
    return InitDBNPC();
}

bool CDBNPC::InitDBNPC() {
    // Current EXE 0x0041ccf0 performs socket/bind/connect first. The portable
    // binding replaces only those OS calls. Allocation, construction,
    // publication, Initialize and SendVersion remain in this target method.
    L2_GUARD_FUNCTION();
    const portable::DBNPCTransportConnection connection = portable::ConnectDBNPCTransport();
    if (!connection) {
        // if (connection.connectResult.failure ==
        //         network::SocketConnectFailure::CreateSocket ||
        //     connection.connectResult.failure ==
        //         network::SocketConnectFailure::BindSocket) {
        //     Log.Add(
        //         LOG_ERROR,
        //         L"Can't bind socket to cache server")
        // } else {
        //     Log.Add(
        //         LOG_ERROR,
        //         L"Can't connect to cache server")
        // }
        const char* stage = "preparing the transport binding";
        const char* errorDomain = "transport";
        switch (connection.connectResult.failure) {
            case network::SocketConnectFailure::Initialize:
                stage = "initializing the transport";
                break;
            case network::SocketConnectFailure::ResolveAddress:
                stage = "resolving the address";
                errorDomain = "getaddrinfo";
                break;
            case network::SocketConnectFailure::CreateSocket:
                stage = "creating the socket";
                break;
            case network::SocketConnectFailure::BindSocket:
                stage = "binding the local socket";
                break;
            case network::SocketConnectFailure::ConnectSocket:
                stage = "connecting the socket";
                break;
            case network::SocketConnectFailure::None:
                break;
        }
        // Log.Add(LOG_WARN,
        //     "[CacheD] Could not establish the connection to %s:%u while %s (%s code %d%s).",
        //     connection.endpointHost.empty() ? "(unavailable)" : connection.endpointHost.c_str(),
        //     static_cast<unsigned int>(connection.endpointPort), stage, errorDomain,
        //     connection.connectResult.error,
        //     connection.connectResult.error == 0 ? ", no error code was reported" : "")
        L2_LOG_WARN(
            Log, "[CacheD] Could not establish the connection to %s:%u while %s (%s code %d%s).",
            connection.endpointHost.empty() ? "(unavailable)" : connection.endpointHost.c_str(),
            static_cast<unsigned int>(connection.endpointPort), stage, errorDomain, connection.connectResult.error,
            connection.connectResult.error == 0 ? "; no error code was reported" : ""
        );
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to cache server")
    // Log.Add(LOG_NORMAL,
    //     "[CacheD] TCP connection established to %s:%u. Initializing the NPC connection.",
    //     connection.endpointHost.empty() ? "(unavailable)" : connection.endpointHost.c_str(),
    //     static_cast<unsigned int>(connection.endpointPort))
    L2_LOG_INFO(
        Log, "[CacheD] TCP connection established to %s:%u; initializing the NPC connection.",
        connection.endpointHost.empty() ? "(unavailable)" : connection.endpointHost.c_str(),
        static_cast<unsigned int>(connection.endpointPort)
    );
    auto* const socket = new CDBNPCSocket(connection.socket);
    if (!connection.Attach(*socket)) {
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Could not attach the NPC socket to the connection to %s:%u. Initialization failed.",
        //     connection.endpointHost.empty() ? "(unavailable)" : connection.endpointHost.c_str(),
        //     static_cast<unsigned int>(connection.endpointPort))
        L2_LOG_ERROR(
            Log, "[CacheD] Could not attach the NPC socket to the connection to %s:%u; initialization failed.",
            connection.endpointHost.empty() ? "(unavailable)" : connection.endpointHost.c_str(),
            static_cast<unsigned int>(connection.endpointPort)
        );
        L2_RELEASE(socket, ::ObjectRefType::CreateDelete);
        return false;
    }
    dbnpc.m_pSocket = socket;
    socket->Initialize();
    dbnpc.SendVersion();
    return true;
}

void CDBNPC::SendVersion() {
    // Current EXE 0x0041c110 parses "$Revision: #14 $" and sends the parsed
    // value through virtual CDBNPCSocket::Send.
    L2_GUARD_FUNCTION();
    char revisionLabel[80];
    int revision;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #14 $", "%s #%d", revisionLabel, static_cast<unsigned>(sizeof(revisionLabel)), &revision);
#else
    std::sscanf("$Revision: #14 $", "%s #%d", revisionLabel, &revision);
#endif
    m_pSocket->Send("cd", 1, revision);
}

void CDBNPC::LoadNpcInfo(std::u16string name, CNpcSpawnDefine* definition) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cSp", 2, name.c_str(), static_cast<long long>(reinterpret_cast<std::intptr_t>(definition)));
}

void CDBNPC::LoadNpcInfoEx(const char16_t* name, NpcSpawnDefineEx* definition, int argument) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cSpd", 10, name, static_cast<long long>(reinterpret_cast<std::intptr_t>(definition)), argument);
}

void CDBNPC::LoadLottoInfo() {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("c", 5);
}

void CDBNPC::Lotto_Drawing(
    int roundNumber, int totalCount, std::int64_t carriedAdena, int chosenNumber, int leftTime, std::int64_t presetReward
) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send(
        "cddQddQ", 6, roundNumber, totalCount, static_cast<long long>(carriedAdena), chosenNumber, leftTime,
        static_cast<long long>(presetReward)
    );
}

void CDBNPC::Lotto_StartNextRound(int roundNumber, int leftTime, std::int64_t carriedAdena) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cddQ", 7, roundNumber, leftTime, static_cast<long long>(carriedAdena));
}

void CDBNPC::Lotto_SaveRound(CLottoRound round) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send(
        "cddddddddddQ", 9, round.RoundNumber, round.State, round.LeftTime, round.ChosenNumber, round.RuleNumber,
        round.TotalCount, round.Winner1Count, round.Winner2Count, round.Winner3Count, round.Winner4Count,
        static_cast<long long>(round.CarriedAdena)
    );
}

void CDBNPC::GetEventRankerInfo(int eventId, int databaseId, int creatureObjectId, int npcObjectId, int value) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cddddd", 14, eventId, databaseId, creatureObjectId, npcObjectId, value);
}

void CDBNPC::DecreaseEventTopRankerReward(int eventId, int rewardId, int databaseId) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cddd", 15, eventId, rewardId, databaseId);
}

void CDBNPC::GiveEventData(int databaseId, int creatureIndex, int npcObjectId, int value) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cdddd", 16, databaseId, creatureIndex, npcObjectId, value);
}

void CDBNPC::RequestKamarokaCheckReward(int reward, int npcObjectId, int databaseId, int creatureObjectId) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cdddd", 17, reward, npcObjectId, databaseId, creatureObjectId);
}

void CDBNPC::RequestKamarokaSetReward(int reward, int databaseId, int count) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cddd", 18, reward, databaseId, count);
}

void CDBNPC::UpdateBossNpcValue(std::u16string databaseName, int value) {
    L2_GUARD_FUNCTION();
    m_pSocket->Send("cSd", 4, databaseName.c_str(), value);
}

void CDBNPC::SaveNpcInfo(CNPC* npc, const char16_t* databaseName) {
    L2_GUARD_FUNCTION();
    if (!databaseName) {
        return;
    }

    const CSharedCreatureData* const sharedData = npc->m_data;
    if (!sharedData->m_bIsAlive || !npc->m_OldNpcInfo.IsChanged()) {
        return;
    }

    const int hp = runtime::Win64CvttDoubleToInt32(sharedData->GetHP());
    const int mp = runtime::Win64CvttDoubleToInt32(sharedData->GetMP());
    const FVector position = sharedData->GetPos();
    const int npcClassId = npc->m_data->m_nClassID - 1000000;
    m_pSocket->Send(
        "cScdddddd", 3, databaseName, 1, hp, mp, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), npcClassId
    );
}

void CDBNPC::SaveNpcDie(const char16_t* databaseName, const char16_t* killerMasterName, int reason) {
    L2_GUARD_FUNCTION();
    if (!databaseName) {
        return;
    }

    // The VS2015 target calls _time32 inside CDBNPC::SaveNpcDie for both
    // CNPC::SaveDie overloads.
    const std::int32_t deathUnixSeconds = runtime::crt::Time32Now();
    m_pSocket->Send("cScdSd", 3, databaseName, 0, deathUnixSeconds, killerMasterName, reason);
}

bool CDBNPC::ValueChanged(int key, int value) {
    L2_GUARD_FUNCTION();
    bool changed;
    {
        L2_GUARD(g_dbsaving_map_lock);
        const auto found = g_dbsaving_map.find(key);
        changed = found == g_dbsaving_map.end() || found->second != value;
    }
    return changed;
}

void CDBNPC::SaveDBSavingMap(int key, int value) {
    L2_GUARD_FUNCTION();
    if (!ValueChanged(key, value)) {
        return;
    }
    L2_GUARD(m_Lock);
    m_DBProcessQueue.push({JobType::JOB_SAVE, {key, value}});
}

void CDBNPC::DeleteDBSavingMap(int key) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_DBProcessQueue.push({JobType::JOB_DELETE, {key, 0}});
}

void CDBNPC::LoadDBSavingMap(int npcObjectId, int key) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_DBProcessQueue.push({JobType::JOB_LOAD, {npcObjectId, key}});
}

void CDBNPC::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        // The target increments its guard after dispatch and stops when the
        // pre-increment value is greater than 100: jobs 0..101 are processed.
        for (int processed = 0; processed < 102 && !m_DBProcessQueue.empty(); ++processed) {
            const DBJob job = m_DBProcessQueue.front();
            const int first = job.second.first;
            const int second = job.second.second;
            switch (job.first) {
                case JobType::JOB_SAVE:
                    m_pSocket->Send("cdd", 11, first, second);
                    break;
                case JobType::JOB_DELETE:
                    // The shipping call supplies only the key for its "cdd"
                    // format. At 0x0041d988..0x0041d99c it never writes the
                    // final stack vararg, so keep the indeterminate field.
                    m_pSocket->Send("cdd", 13, first);
                    break;
                case JobType::JOB_LOAD:
                    m_pSocket->Send("cdd", 12, first, second);
                    break;
                case JobType::JOB_MAX:
                    break;
            }

            m_DBProcessQueue.pop();
        }
        if (!m_DBProcessQueue.empty()) {
            // The target reports 101 here even though entries 0..101 have
            // already been dispatched.
            // Log.Add(
            //     LOG_ERROR,
            //     L"Too many db jobs for dbnpc. %d",
            //     101)
            // Log.Add(LOG_WARN,
            //     "[CacheD] NPC database queue still has %zu jobs after processing 102 entries. Remaining jobs stay queued
            //     for a later timer tick.", m_DBProcessQueue.size())
            L2_LOG_WARN(
                Log,
                "[CacheD] NPC database queue still has %zu jobs after processing 102 entries; remaining jobs stay queued "
                "for a later timer tick.",
                m_DBProcessQueue.size()
            );
        }
    }
    AddTimer(1000U, 0);
}

} // namespace l2npc
