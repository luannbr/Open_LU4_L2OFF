#include "DBNPCSocket.h"

#include "DBNPC.h"
#include "LottoGame.h"
#include "NPC.h"
#include "NPCEventFactory.h"
#include "Portable/ProcessRuntime.h"
#include "NpcEvent.h"
#include "NpcMaker.h"
#include "NpcMakerCommon.h"
#include "NpcSpawnDefineEx.h"
#include "RespawnTimer.h"
#include "Runtime.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/TemplateHelper.h"
#include "TextRuntime.h"
#include "User.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <new>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/socket.h>
#endif

namespace l2npc {
namespace {

int CurrentLastError() {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

void AddDBNpcInfoVariable(ScriptTypeInfo& typeInfo, const wchar_t* name, int offset, ScriptTypeId typeId) {
    auto* const variable = new ScriptVarInfo;
    variable->SetScriptVarName(name);
    variable->Offset(offset);
    variable->Type(&GetBuiltinScriptType(typeId));
    typeInfo.AddVarInfo(variable);
}

void CopyDBNpcName(DBNpcInfo& info, const std::u16string& dbName) {
    const std::u16string_view name = runtime::StringView(dbName);
    // ReplyLoadNpcInfoEx uses wcscpy_s(dst, 25, src), not a truncating copy.
    // The process-wide CRT handler crashes on an oversized DB
    // name, so keep that boundary.
    if (name.size() >= info.dbname.size()) {
        info.dbname[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Buffer is too small\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        return;
    }
    for (std::size_t index = 0; index < name.size(); ++index) {
        info.dbname[index] = name[index];
    }
    info.dbname[name.size()] = u'\0';
}

} // namespace

// Current EXE initializers 0x0058534c..0x00585490 construct this exact
// FixedSizeCallbackMap<8> and bind all eight CacheD callbacks.
CacheDCallbackMap gPacketTableFromCache{DummyPacket, u"DBNPCProtocol"};

const bool IsRegistered_DN_SPAWN_DEFIN_RESPONSE = gPacketTableFromCache.Bind(0, ReplyLoadNpcInfo);
const bool IsRegistered_DN_LOAD_LOTTO_INFO_RESPONSE = gPacketTableFromCache.Bind(1, NpcLoadLottoInfo);
const bool IsRegistered_DN_UPDATE_LOTTO_GAME = gPacketTableFromCache.Bind(2, NpcUpdateLottoGame);
const bool IsRegistered_DN_SPAWN_DEFIN_RESPONSE_EX = gPacketTableFromCache.Bind(3, ReplyLoadNpcInfoEx);
const bool IsRegistered_DN_LOAD_DBSAVING_MAP = gPacketTableFromCache.Bind(4, LoadDBSavingMap);
const bool IsRegistered_DN_GET_EVENT_RANKER_INFO = gPacketTableFromCache.Bind(5, GetEventRankerInfo);
const bool IsRegistered_DN_GIVE_EVENT_DATA = gPacketTableFromCache.Bind(6, GiveEventData);
const bool IsRegistered_DN_CHECK_KAMAROKA_REWARD = gPacketTableFromCache.Bind(7, ReplyCheckKamarokaReward);

DBNpcInfo::~DBNpcInfo() = default;

void DBNpcInfo::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current EXE 0x00421eb4, in exact registration order.
    AddDBNpcInfoVariable(*typeInfo, L"dbname", 0x08, ScriptTypeId::String);
    AddDBNpcInfoVariable(*typeInfo, L"hp", 0x3c, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"mp", 0x40, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"x", 0x44, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"y", 0x48, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"z", 0x4c, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"death_time", 0x50, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"is_alive", 0x54, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"db_value", 0x58, ScriptTypeId::Int);
    AddDBNpcInfoVariable(*typeInfo, L"arg1", 0x5c, ScriptTypeId::Int);
}

ScriptAccessibleEx* DBNpcInfo::GetScriptAccessibleEx() const {
    // Current EXE 0x0041f7e4 is the four-byte return-this body.
    return const_cast<DBNpcInfo*>(this);
}

CDBNPCSocket::CDBNPCSocket(std::uint64_t socket) : CIOSocket(socket, 5000) {
    L2_GUARD_FUNCTION();
    sock = socket;
    threadCount = 0;
    packetTable = gPacketTableFromCache.GetData();
    mode = SocketMode::SM_READ_LEN;
    packetLen = 0;
}

CDBNPCSocket::~CDBNPCSocket() {
    // Current EXE 0x004216f0 clears the derived callback-table pointer before
    // entering the CIOSocketEx<CIOBufferEx<20480>> base destructor.
    packetTable = nullptr;
}

void CDBNPCSocket::Send(const char* format, ...) {
    // Current EXE 0x0041f4b4 is a thin virtual SendV forwarder.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CDBNPCSocket::SendV(const char* format, va_list arguments) {
    // Current EXE 0x00421b5c.
    L2_GUARD_FUNCTION();
    if (mode == SocketMode::SM_CLOSE) {
        return;
    }

    auto* const buffer = DefaultIOBuffer::Alloc();
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     runtime::LegacySocketImportAddressToken(),
        //     format)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] NPC message encoder returned an empty result (body buffer capacity 20478 bytes). Submitting the
        //     empty buffer. Format: %s", format ? format : "(null)")
        L2_LOG_ERROR(
            Log,
            "[CacheD] NPC message encoder returned an empty result (body buffer capacity 20478 bytes); submitting the empty "
            "buffer. Format: %s",
            format ? format : "(null)"
        );
        buffer->m_dwSize = 0;
    } else {
        const std::uint16_t frameSize = static_cast<std::uint16_t>(bodySize + 2);
        buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameSize);
        buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameSize >> 8U);
        buffer->m_dwSize = frameSize;
    }
    Write(buffer);
}

void CDBNPCSocket::OnRead() {
    // Current EXE 0x00421314. m_pReadBuf is unconditionally dereferenced.
    // construction guarantees it and the target has no defensive null path.
    L2_GUARD_FUNCTION();
    if (mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (mode == SocketMode::SM_READ_LEN) {
            if (readSize - packetOffset < 2) {
                Read(readSize - packetOffset);
                return;
            }

            const std::uint8_t* const length = m_pReadBuf->m_Buffer.data() + packetOffset;
            packetLen = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(length[0]) | (static_cast<std::uint32_t>(length[1]) << 8U)
                        ) -
                2;
            if (packetLen < 1 || packetLen > 0x5000) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"dbsocket %d: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     packetLen)
                // Log.Add(LOG_ERROR,
                //     "[CacheD] Invalid incoming body length %d bytes (accepted range 1..20480). Closing the connection.",
                //     packetLen)
                L2_LOG_ERROR(
                    Log, "[CacheD] Invalid incoming body length %d bytes (accepted range 1..20480); closing the connection.",
                    packetLen
                );
                mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        }

        if (mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }
        if (readSize - packetOffset < packetLen) {
            Read(readSize - packetOffset);
            return;
        }

        const std::uint8_t opcode = m_pReadBuf->m_Buffer[packetOffset];
        if (opcode >= 8) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[dbsocket]unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.Add(LOG_ERROR,
            //     "[CacheD] Unsupported NPC reply opcode 0x%02X (accepted range 0x00..0x07). Closing the connection.",
            //     static_cast<unsigned int>(opcode))
            L2_LOG_ERROR(
                Log, "[CacheD] Unsupported NPC reply opcode 0x%02X (accepted range 0x00..0x07); closing the connection.",
                static_cast<unsigned int>(opcode)
            );
            mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new CDBNPCPacket;
        packet->m_pSocket = this;
        m_pReadBuf->AddRef();
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = packetTable[opcode];
        L2_ADD_REF(this, ::ObjectRefType::IoRetain);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        // The shipping code ignores PostQueuedCompletionStatus' result.
        // Therefore a failed post leaves these keeps pending.
        packet->PostObject(static_cast<int>(packetOffset));

        packetOffset += packetLen;
        mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

void CDBNPCSocket::OnClose() {
    // Current L2NPC.exe 0x0041f394: a lost CacheD connection is fatal.
    L2_GUARD_FUNCTION();
    const int lastError = CurrentLastError();
    // Log.Add(
    //     LOG_ERROR,
    //     L"DBSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(sock),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(
    //             this)),
    //     lastError)
    // Log.Add(LOG_ERROR,
    //     "[CacheD] NPC connection %llu closed. Terminating because this connection is required. Ambient OS error %d at
    //     callback entry may not identify the cause.", static_cast<unsigned long long>(sock), lastError)
    L2_LOG_ERROR(
        Log,
        "[CacheD] NPC connection %llu closed; terminating because this connection is required. Ambient OS error %d at "
        "callback entry may not identify the cause.",
        static_cast<unsigned long long>(sock), lastError
    );
    // Original: ForceCrash(CrashInfo::DB_SOCKET_IS_CLOSED). A lost CacheD is an
    // expected stop, not a defect, so exit without a crash report.
    L2_LOG_ERROR(
        Log, "[shutdown] L2NPC is stopping with exit code %d. Start it again after CacheD and L2Server are running.",
        l2npc::portable::kRequiredPeerClosedExitCode
    );
    l2npc::portable::ExitAfterRequiredPeerClosed();
}

bool DummyPacket(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in DBSocket")
    // Log.Add(LOG_ERROR,
    //     "[CacheD] Unregistered NPC reply handler invoked, requesting connection closure.")
    L2_LOG_ERROR(Log, "[CacheD] Unregistered NPC reply handler invoked; requesting connection closure.");
    return true;
}

bool ReplyLoadNpcInfo(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 1000 / sizeof(char16_t)> dbNameBuffer{};
    CNpcSpawnDefine* definition = nullptr;
    char hasData = 0;
    packet = ::Disassemble(packet, "Spc", 1000, dbNameBuffer.data(), &definition, &hasData);

    if (hasData == 0) {
        int deathTime = 0;
        int ignored = 0;
        ::Disassemble(packet, "dd", &deathTime, &ignored);
        const int respawnTime = definition->GetRespawnTime();
        const std::uint32_t absoluteBits = static_cast<std::uint32_t>(deathTime) + static_cast<std::uint32_t>(respawnTime);
        std::int32_t absoluteTime = 0;
        std::memcpy(&absoluteTime, &absoluteBits, sizeof(absoluteTime));

        new CRespawnTimer(definition, definition->GetDBName(), absoluteTime);
        return false;
    }

    int dbValue = 0;
    int hp = 0;
    int mp = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "dddddd", &dbValue, &hp, &mp, &x, &y, &z);
    definition->SetDBLoaded(true);
    if ((definition->GetDBSavingFlag() & 4) == 0) {
        x = 0;
        y = 0;
        z = 0;
    }
    if ((definition->GetDBSavingFlag() & 2) == 0) {
        dbValue = 0;
        hp = 0;
        mp = 0;
    }
    SendCreateNpcPacket(
        definition->GetName(), definition, definition->GetPledgeId(), x, y, z, 0, hp, mp, dbValue,
        static_cast<std::uint32_t>(definition->GetInZoneID()), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    return false;
}

bool NpcLoadLottoInfo(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int expectedRoundCount = 0;
    int hasRow = 0;
    packet = ::Disassemble(packet, "dd", &expectedRoundCount, &hasRow);

    if (hasRow != 0) {
        CLottoRound round;
        ::Disassemble(
            packet, "ddddddddddQ", &round.RoundNumber, &round.State, &round.LeftTime, &round.ChosenNumber, &round.RuleNumber,
            &round.TotalCount, &round.Winner1Count, &round.Winner2Count, &round.Winner3Count, &round.Winner4Count,
            &round.CarriedAdena
        );
        g_LottoGameManager.AddLottoRoundFromDB(round);
        return false;
    }

    g_LottoGameManager.WaitForLoading(expectedRoundCount);
    return false;
}

bool NpcUpdateLottoGame(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int mode = 0;
    packet = ::Disassemble(packet, "d", &mode);

    if (mode == 0) {
        int roundNumber = 0;
        int totalCount = 0;
        ::Disassemble(packet, "dd", &roundNumber, &totalCount);
        g_LottoGameManager.Update(roundNumber, totalCount);
    } else if (mode == 1) {
        int roundNumber = 0;
        int winner1Count = 0;
        int winner2Count = 0;
        int winner3Count = 0;
        int winner4Count = 0;
        ::Disassemble(packet, "ddddd", &roundNumber, &winner1Count, &winner2Count, &winner3Count, &winner4Count);
        g_LottoGameManager.UpdateDrawingState(roundNumber, winner1Count, winner2Count, winner3Count, winner4Count);
    }
    return false;
}

bool ReplyLoadNpcInfoEx(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 1000 / sizeof(char16_t)> dbNameBuffer{};
    NpcSpawnDefineEx* definition = nullptr;
    int argument = 0;
    char hasData = 0;
    packet = ::Disassemble(packet, "Spdc", 1000, dbNameBuffer.data(), &definition, &argument, &hasData);

    DBNpcInfo info;
    const std::u16string dbName(dbNameBuffer.data());
    CopyDBNpcName(info, dbName);
    info.arg1 = argument;
    if (hasData == 0) {
        int ignored = 0;
        ::Disassemble(packet, "dd", &info.death_time, &ignored);
        info.is_alive = 0;
    } else {
        ::Disassemble(packet, "dddddd", &info.db_value, &info.hp, &info.mp, &info.x, &info.y, &info.z);
        info.is_alive = 1;
    }

    definition->OnDBNpcInfo(info);
    return false;
}

bool LoadDBSavingMap(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int npcId = 0;
    int key = 0;
    int value = 0;
    ::Disassemble(packet, "ddd", &npcId, &key, &value);
    CNPC* const npc = CNPC::GetObjectW(npcId);
    if (!npc) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateLoadDBSavingMapReturned(npc, key, value);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     __WFILE__,
        //     0xf2)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Could not create the DB-saving-map reply event for NPC %d, key %d. Skipping the map update and event
        //     delivery.", npcId, key)
        L2_LOG_ERROR(
            Log,
            "[CacheD] Could not create the DB-saving-map reply event for NPC %d, key %d; skipping the map update and event "
            "delivery.",
            npcId, key
        );
        return false;
    }

    {
        L2_GUARD(g_dbsaving_map_lock);
        g_dbsaving_map[key] = value;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GetEventRankerInfo(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int ignored0 = 0;
    int result2 = 0;
    int ignored2 = 0;
    int userId = 0;
    int result0 = 0;
    int result1 = 0;
    int npcId = 0;
    ::Disassemble(packet, "ddddddd", &ignored0, &result2, &ignored2, &userId, &result0, &result1, &npcId);

    User* const user = User::GetObjectW(userId);
    CNPC* const npc = CNPC::GetObjectW(npcId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateEventRankerInfo(npc, user->m_data, result0, result1, result2);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     __WFILE__,
        //     0x114)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Could not create the event-ranker reply event for NPC %d, user %d. Skipping event delivery.",
        //     npcId, userId)
        L2_LOG_ERROR(
            Log, "[CacheD] Could not create the event-ranker reply event for NPC %d, user %d; skipping event delivery.",
            npcId, userId
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GiveEventData(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // The shipping callback contains only tracing prologue/epilogue and does
    // not check the payload or mutate state.
    return false;
}

bool ReplyCheckKamarokaReward(CDBNPCSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int result = 0;
    int npcId = 0;
    int ignored = 0;
    int userId = 0;
    int reward = 0;
    ::Disassemble(packet, "ddddd", &result, &npcId, &ignored, &userId, &reward);

    User* const user = User::GetObjectW(userId);
    CNPC* const npc = CNPC::GetObjectW(npcId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCheckKamarokaRewardEvent(npc, user->m_data, result, reward);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     __WFILE__,
        //     0x15e)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Could not create the Kamaroka reward reply event for NPC %d, user %d. Skipping event delivery.",
        //     npcId, userId)
        L2_LOG_ERROR(
            Log, "[CacheD] Could not create the Kamaroka reward reply event for NPC %d, user %d; skipping event delivery.",
            npcId, userId
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

} // namespace l2npc
