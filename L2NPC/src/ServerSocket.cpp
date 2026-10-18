#include "ServerSocket.h"
#include "Protocol/src/L2Constants.h"

#include "AIManager.h"
#include "AdminRewardSystem.h"
#include "AirShipEventManager.h"
#include "Default.h"
#include "EventManager.h"
#include "GlobalObject.h"
#include "L2Core/src/inc/Guard.h"
#include "L2Core/src/inc/GuardProfile.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/Generated/L2ServerInboundTableGen.h"
#include "LottoGame.h"
#include "MakerEventFactory.h"
#include "NPC.h"
#include "NPCEventFactory.h"
#include "NPCServer.h"
#include "NpcEvent.h"
#include "NpcMakerCommon.h"
#include "NpcMakerDB.h"
#include "Portable/ProcessRuntime.h"
#include "RespawnTimer.h"
#include "SSQNPCServer.h"
#include "ScriptEngine.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/IOCPMessage.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/Systems.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"
#include "SuperPoint.h"
#include "TextRuntime.h"
#include "User.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <map>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/socket.h>
#endif

namespace l2npc {

bool g_bDBNPCReset = false;

namespace {

// Retained for the disabled shipping-bug call at RequestNPCSpawn.
[[maybe_unused]] char16_t* TargetWcsDup(const char16_t* value) {
    const std::size_t length = std::char_traits<char16_t>::length(value);
    auto* const duplicate = static_cast<char16_t*>(std::calloc(length + 1, sizeof(char16_t)));
    if (duplicate) {
        std::memcpy(duplicate, value, (length + 1) * sizeof(char16_t));
    }
    return duplicate;
}

#define L2NPC_BIND_SERVER_CALLBACK(Opcode, Callback) &Callback,
constexpr std::array<L2ServerCallback, 200> kServerPacketCallbacks{
    {L2NPC_FOR_EACH_L2SERVER_CALLBACK(L2NPC_BIND_SERVER_CALLBACK)}};
#undef L2NPC_BIND_SERVER_CALLBACK
#undef L2NPC_FOR_EACH_L2SERVER_CALLBACK

} // namespace

L2ServerCallbackMap gPacketTableFromServer{DummyPacket, u"NPCProtocol"};

namespace {

[[maybe_unused]] const bool kAllServerPacketsRegistered = [] {
    for (std::size_t opcode = 0; opcode < kServerPacketCallbacks.size(); ++opcode) {
        if (!gPacketTableFromServer.Bind(static_cast<int>(opcode), kServerPacketCallbacks[opcode])) {
            return false;
        }
    }
    return gPacketTableFromServer.TestRegistration();
}();

} // namespace

void CServerSocket::BroadcastSystemMessageW(char* format, ...) {
    // Current L2NPC.exe 0x00521608, PDB source serversocket.cpp: the
    // caller's system-message format is assembled into one embedded blob,
    // then broadcast through the ordinary CServerSocket send virtual.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> message;
    va_list arguments;
    va_start(arguments, format);
    const int messageSize = ::VAssemble(message.data(), static_cast<int>(message.size()), format, arguments);
    va_end(arguments);

    if (messageSize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BroadcastSystemMessage failed",
        //     L"serversocket.cpp",
        //     0x2260)
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Cannot encode the system-message broadcast, nothing sent.")
        // Original: L2_LOG_ERROR(Log,
        //     "[L2Server] Cannot encode the system-message broadcast, nothing sent.")
        // Previous: L2_LOG_ERROR(Log,
        //     "Cannot encode the system-message broadcast to L2Server (format '%s', buffer capacity %zu bytes). Nothing
        //     sent.", format != nullptr ? format : "<null>", message.size())
        L2_LOG_ERROR(
            Log,
            "[broadcast] Cannot encode the system message for L2Server (format '%s', buffer capacity %zu bytes); nothing "
            "sent.",
            format != nullptr ? format : "<null>", message.size()
        );
        return;
    }

    Send("cdddddb", 0x27, -1, -1, -1, 2, messageSize, messageSize, message.data());
}

bool VersionPacket(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    char protocolOk;
    char sharedFactoryOk;
    char sharedDataOk;
    int npcProtocolVersion;
    int serverType;
    int sharedFactoryVersion;
    int sharedDataSize;
    ::Disassemble(
        packet, "cccddddd", &protocolOk, &sharedFactoryOk, &sharedDataOk, &npcProtocolVersion, &g_nServerId, &serverType,
        &sharedFactoryVersion, &sharedDataSize
    );

    if (protocolOk != 1) {
        // Log.Add(LOG_ERROR,
        //     "[L2Server] NPC protocol was not accepted (local %d, peer %d, status %u). Terminating.",
        //     272, npcProtocolVersion, static_cast<unsigned int>(static_cast<unsigned char>(protocolOk)))
        L2_LOG_ERROR(
            Log, "[L2Server] NPC protocol was not accepted (local %d, peer %d, status %u); terminating.", 272,
            npcProtocolVersion, static_cast<unsigned int>(static_cast<unsigned char>(protocolOk))
        );
        // Noise: The rejection is already logged above. Retain the Windows modal copy.
        // PopUpWindow(
        //     L"npc protocol version "
        //     L"(NPC[%d], L2SERVER[%d])",
        //     272,
        //     npcProtocolVersion)
#if defined(_WIN32)
        PopUpWindow(
            L"npc protocol version "
            L"(NPC[%d], L2SERVER[%d])",
            272, npcProtocolVersion
        );
#endif
        ForceCrash(CrashInfo::MISMATCH_NPC_PROTOCOL);
        return true;
    }
    if (sharedFactoryOk != 1) {
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Shared-factory version was not accepted (local %d, peer %d, status %u). Terminating.",
        //     289, sharedFactoryVersion, static_cast<unsigned int>(static_cast<unsigned char>(sharedFactoryOk)))
        L2_LOG_ERROR(
            Log, "[L2Server] Shared-factory version was not accepted (local %d, peer %d, status %u); terminating.", 289,
            sharedFactoryVersion, static_cast<unsigned int>(static_cast<unsigned char>(sharedFactoryOk))
        );
        // Noise: The rejection is already logged above. Retain the Windows modal copy.
        // PopUpWindow(
        //     L"SharedFactory version is different. "
        //     L"NPC[%d], L2SERVER[%d]",
        //     289,
        //     sharedFactoryVersion)
#if defined(_WIN32)
        PopUpWindow(
            L"SharedFactory version is different. "
            L"NPC[%d], L2SERVER[%d]",
            289, sharedFactoryVersion
        );
#endif
        ForceCrash(CrashInfo::MISMATCH_SHARED_FACTORY_VERSION);
        return true;
    }
    if (sharedDataOk != 1) {
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Shared-data size was not accepted (local %d bytes, peer %d bytes, status %u). Terminating.",
        //     0x4b88, sharedDataSize, static_cast<unsigned int>(static_cast<unsigned char>(sharedDataOk)))
        L2_LOG_ERROR(
            Log, "[L2Server] Shared-data size was not accepted (local %d bytes, peer %d bytes, status %u); terminating.",
            0x4b88, sharedDataSize, static_cast<unsigned int>(static_cast<unsigned char>(sharedDataOk))
        );
        // Noise: The rejection is already logged above. Retain the Windows modal copy.
        // PopUpWindow(
        //     L"Shared Data size is different. "
        //     L"NPC[%d], L2SERVER[%d]",
        //     0x4b88,
        //     sharedDataSize)
#if defined(_WIN32)
        PopUpWindow(
            L"Shared Data size is different. "
            L"NPC[%d], L2SERVER[%d]",
            0x4b88, sharedDataSize
        );
#endif
        ForceCrash(CrashInfo::MISMATCH_SHARED_DATA_SIZE);
        return true;
    }
    if (serverType != g_L2ServerType) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "ServerType mismatch between L2Server [%d] and L2NPC [%d]",
        //     serverType,
        //     g_L2ServerType)
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Peer server type %d does not match L2NPC [Setting] server_type (%d). Terminating.",
        //     serverType, static_cast<int>(g_L2ServerType))
        L2_LOG_ERROR(
            Log, "[L2Server] Peer server type %d does not match L2NPC [Setting] server_type (%d); terminating.", serverType,
            static_cast<int>(g_L2ServerType)
        );
        // Noise: The rejection is already logged above. Retain the Windows modal copy.
        // PopUpWindow(
        //     L"ServerType mismatch between L2Server and L2NPC. "
        //     L"Please check the L2NPC.ini file settings.")
#if defined(_WIN32)
        PopUpWindow(
            L"ServerType mismatch between L2Server and L2NPC. "
            L"Please check the L2NPC.ini file settings."
        );
#endif
        ForceCrash(CrashInfo::SERVER_TYPE_MISMATCH);
        return true;
    }

    // Noise: The following handshake summary already identifies the accepted version reply.
    // Log.Add(
    //     LOG_NORMAL,
    //     "<- NS_VERSION")
    // Log.Add(
    //     LOG_NORMAL,
    //     "npc_protocol_ver=%d server_id=%d server_type=%d",
    //     npcProtocolVersion,
    //     g_nServerId,
    //     serverType)
    // Log.Add(LOG_NORMAL,
    //     "[L2Server] Handshake accepted: server %d, type %d, NPC protocol %d, shared-factory version %d, shared-data size
    //     %d bytes.", g_nServerId, serverType, npcProtocolVersion, sharedFactoryVersion, sharedDataSize)
    L2_LOG_INFO(
        Log,
        "[L2Server] Handshake accepted: server %d, type %d, NPC protocol %d, shared-factory version %d, shared-data size %d "
        "bytes.",
        g_nServerId, serverType, npcProtocolVersion, sharedFactoryVersion, sharedDataSize
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // Noise: A version reply does not mean NPC startup or spawning is complete. Handshake acceptance is reported above.
    // Log.Add(
    //     LOG_NORMAL,
    //     "npc server started")

    // Exact successful VersionPacket extension. The shipping callback builds
    // the quest-id blob in CPacketHelperEx<8192> and calls the Send virtual
    // on its incoming CSocket* argument.
    if (socket) {
        CPacketHelperEx<8192> questIds;
        for (const int quest : ::g_vF2PRewardPenaltyQuests) {
            questIds.Assemble("d", quest);
        }
        socket->Send(
            "chfdb", 0xfe, 0x29, ::g_dF2PQuestRewardPenaltyPer, static_cast<int>(::g_vF2PRewardPenaltyQuests.size()),
            questIds.GetLength(), questIds.GetPacket()
        );
    }
    // Exact successful VersionPacket tail.
    CNPCAIManager::CreateDominionNPCCreatedEvent();
    return false;
}

bool CreatePacket(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int sharedIndex;
    int objectId;
    std::uint64_t spawnToken;
    int makerId;
    int x;
    int y;
    int z;
    int heading = 0;
    int isMercenary = 0;
    int hp = 0;
    int mp = 0;
    std::array<char16_t, 200 / sizeof(char16_t)> aiNameBuffer;
    int parameterCount = 0;
    // Exact L2Server order, proven at CreatePacket's
    // 0x531051 call to CNPC::Set and by the older surviving sender:
    // pSD->nServerIndex first, nObjectID second. The NPC class id is already
    // present in the indexed CSharedCreatureData record.
    packet = ::Disassemble(
        packet, "ddpddddddddSd", &sharedIndex, &objectId, &spawnToken, &makerId, &x, &y, &z, &heading, &isMercenary, &hp,
        &mp, 200, aiNameBuffer.data(), &parameterCount
    );
    const std::u16string aiNameWide(aiNameBuffer.data());

    BaseSpawnDefine::AiParameterMap parameters;
    for (int index = 0; index < parameterCount; ++index) {
        std::array<char16_t, 400 / sizeof(char16_t)> name;
        std::array<char16_t, 8000 / sizeof(char16_t)> value;
        packet = ::Disassemble(packet, "SS", 400, name.data(), 8000, value.data());
        parameters[std::u16string(name.data())] = std::u16string(value.data());
    }
    int airShipServerId = 0;
    int instantZoneId = 0;
    ::Disassemble(packet, "dd", &airShipServerId, &instantZoneId);
    if (instantZoneId != 0) {
        NpcMakerEx* const instantZoneMaker = CNpcMakerDB::Inst().GetInstantZoneMakerEx(instantZoneId, u"", false);
        if (!instantZoneMaker || (instantZoneMaker->GetInZoneDeleteCount() != 0 && instantZoneMaker->GetInZoneID() != 0)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] try to enter deleted inzone.",
            //     Utf16ToWide(aiNameBuffer.data()).c_str())
            // Log.Add(LOG_WARN,
            //     "[NPC creation] Skipping object %u (AI '%s') in instance zone %d: %s.",
            //     static_cast<unsigned int>(objectId), Utf16ToUtf8(aiNameBuffer.data()).c_str(), instantZoneId,
            //     instantZoneMaker ? "maker is marked for deletion" : "no maker is registered")
            L2_LOG_WARN(
                Log, "[NPC creation] Skipping object %u (AI '%s') in instance zone %d: %s.",
                static_cast<unsigned int>(objectId), Utf16ToUtf8(aiNameBuffer.data()).c_str(), instantZoneId,
                instantZoneMaker ? "maker is marked for deletion" : "no maker is registered"
            );
            return false;
        }
    }

    // L2Server echoes the BaseSpawnDefine* sent by L2NPC. The callback
    // converts it straight back. Only a null token selects the world define.
    BaseSpawnDefine* const runtimeSpawnDefinition = spawnToken != 0
        ? reinterpret_cast<BaseSpawnDefine*>(static_cast<std::uintptr_t>(spawnToken))
        : static_cast<BaseSpawnDefine*>(&g_WorldSpawnDefine);
    CNPC* const npc = new CNPC();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too low memory, cannot spawn NPC",
        //     L"serversocket.cpp",
        //     0xbf)
        // Log.Add(LOG_ERROR,
        //     "[NPC creation] Allocation returned no NPC for object %u, shared index %d (AI '%s'). Skipping creation.",
        //     static_cast<unsigned int>(objectId), sharedIndex, Utf16ToUtf8(aiNameBuffer.data()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC creation] Allocation returned no NPC for object %u, shared index %d (AI '%s'); skipping creation.",
            static_cast<unsigned int>(objectId), sharedIndex, Utf16ToUtf8(aiNameBuffer.data()).c_str()
        );
        return false;
    }
    // The shipping handler consumes isMercenary and uses HP/MP only in its
    // response. None of the three values becomes CNPC state.
    NpcSpawnInfo* const spawnInfo = NpcSpawnInfo::Inst();
    base_structure_detail::AtomicIncrement(&spawnInfo->Npc_Create);
    npc->SetSpawnDefine(runtimeSpawnDefinition);

    // CreatePacket calls SetSpawnDefine and then CNPC::Set before it reads
    // m_data for movement-mode/owner state or calls GenerateSpawnPos.
    if (!npc->Set(static_cast<std::uint32_t>(objectId), sharedIndex)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Create failed",
        //     L"serversocket.cpp",
        //     0xd1)
        // Log.Add(LOG_ERROR,
        //     "[NPC creation] Cannot initialize object %u from shared index %d (AI '%s'). Releasing the allocation and
        //     skipping creation.", static_cast<unsigned int>(objectId), sharedIndex,
        //     Utf16ToUtf8(aiNameBuffer.data()).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC creation] Cannot initialize object %u from shared index %d (AI '%s'); releasing the allocation and "
            "skipping creation.",
            static_cast<unsigned int>(objectId), sharedIndex, Utf16ToUtf8(aiNameBuffer.data()).c_str()
        );
        L2_RELEASE(npc, ::ObjectRefType::CreateDelete);
        return false;
    }
    CSharedCreatureData* const sharedCreature = npc->m_data;

    npc->m_nMakerID = makerId;

    if (!runtimeSpawnDefinition->IsWorldSpawnDefine()) {
        TerritoryInfo* const territory = runtimeSpawnDefinition->GetTerritoryInfo();
        if (territory && territory->GetTerritorySize() == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] GetTerritory(0) is NULL",
            //     Utf16ToWide(aiNameBuffer.data()).c_str())
            // Log.Add(LOG_ERROR,
            //     "[NPC creation] Spawn territory is empty for object %u (AI '%s', maker %d). Releasing the NPC and skipping
            //     creation.", static_cast<unsigned int>(objectId), Utf16ToUtf8(aiNameBuffer.data()).c_str(), makerId)
            L2_LOG_ERROR(
                Log,
                "[NPC creation] Spawn territory is empty for object %u (AI '%s', maker %d); releasing the NPC and skipping "
                "creation.",
                static_cast<unsigned int>(objectId), Utf16ToUtf8(aiNameBuffer.data()).c_str(), makerId
            );
            L2_RELEASE(npc, ::ObjectRefType::CreateDelete);
            return false;
        }
    }

    FVector spawnedPosition{
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
    };
    if (x == 0 && y == 0 && z == 0) {
        spawnedPosition = runtimeSpawnDefinition->GenerateSpawnPos(heading, sharedCreature->IsFlying());
        x = static_cast<int>(spawnedPosition.x);
        y = static_cast<int>(spawnedPosition.y);
        z = static_cast<int>(spawnedPosition.z);
        if (spawnedPosition.x == 0.0 && spawnedPosition.y == 0.0 && spawnedPosition.z == 0.0) {
            L2_RELEASE(npc, ::ObjectRefType::CreateDelete);
            return false;
        }
    }

    npc->__x = x;
    npc->__y = y;
    npc->__z = z;
    npc->OnCreate();
    runtimeSpawnDefinition->OnNpcCreated(npc);
    runtimeSpawnDefinition->SetAIParameter(npc, aiNameWide.c_str(), parameters);
    L2_RELEASE(npc, ::ObjectRefType::CreateDelete);
    // The exact target releases ORT_CREATE_DELETE first, then publishes the
    // generated position to CNPC::m_vSpawnedPos at +0x13c8.
    npc->SetSpawnedPos(&spawnedPosition);
    npc->m_nAirShipServerID = static_cast<std::uint32_t>(airShipServerId);

    // Exact CreatePacket success response goes through the incoming socket's
    // Send virtual instead of the process-global NPCServer owner.
    socket->Send("cddddddddd", 0x02, sharedIndex, x, y, z, heading, hp > 0 || mp > 0 ? 1 : 0, hp, mp, instantZoneId);

    runtime::raw_atomic::IncrementI32(&g_nNPCCount);
    if (runtime::raw_atomic::LoadI32Relaxed(&g_nMaxNPCCount) < runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount)) {
        runtime::raw_atomic::StoreI32Relaxed(&g_nMaxNPCCount, runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount));
        g_tMaxNPCCount = runtime::GetLocalTimeNow();
    }

    return false;
}

// RTTI at 0x006b9df8 names CTestMessage and its vtable at 0x0063dfa8 has the
// exact CIOCPMessage/CIOObject/MemoryObject ancestry. There are no data
// members: the shipping object is exactly 0x18 bytes.
class CTestMessage final : public CIOCPMessage {
public:
    void OnReceive(int transferred) override;
};


namespace {

// Local overload recovered from serversocket.cpp. SuperPoint.cpp contains
// the distinct IPacketHelper overload with the same original function name.
int AssembleServerPrimitiveLine(
    char* buffer,
    unsigned int bufferSize,
    const wchar_t* name,
    int red,
    int green,
    int blue,
    int alpha,
    const FVector& first,
    const FVector& second
) {
    const std::u16string wireName = WideToUtf16(name);
    return packet_helper_detail::AssemblePacket(
        buffer, static_cast<int>(bufferSize), "cSdddddddddd", 2, wireName.c_str(), red, green, blue, alpha,
        runtime::Win64CvttDoubleToInt32(first.x), runtime::Win64CvttDoubleToInt32(first.y),
        runtime::Win64CvttDoubleToInt32(first.z), runtime::Win64CvttDoubleToInt32(second.x),
        runtime::Win64CvttDoubleToInt32(second.y), runtime::Win64CvttDoubleToInt32(second.z)
    );
}

void SendBuilderEmbeddedPacket(int builderObjectId, int embeddedSize, const char* embedded) {
    // BuilderCmdShowPacket in current L2NPC.exe 0x0052d4cc uses the
    // "cddb" envelope: the second size is consumed by format `b`.
    g_ns.Send("cddb", 0x7a, builderObjectId, embeddedSize, embeddedSize, embedded);
}

std::int64_t SignedI64Bits(std::uint64_t value) {
    std::int64_t result = 0;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

template <typename... Args>
std::u16string FormatTerritoryBuilderText(const std::u16string_view format, const Args&... arguments) {
    // FUN_00521994 receives the target's 0x400-wchar local buffer.
    std::array<char16_t, 0x400> buffer{};
    const int length = FormatUtf16To(buffer.data(), buffer.size(), format, arguments...);
    if (length < 0) {
        return {};
    }
    return std::u16string(buffer.data(), static_cast<std::size_t>(length));
}

void FormatServerSocketTimestamp(const std::tm* local, char16_t* output) {
    std::array<char, 0x14> buffer;
    std::snprintf(
        buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d:%02d", local->tm_year + 1900, local->tm_mon + 1,
        local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec
    );
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        output[index] = static_cast<unsigned char>(buffer[index]);
        if (buffer[index] == '\0') {
            break;
        }
    }
}

void SendTerritoryBuilderMessage(int builderObjectId, const std::u16string& message) {
    std::array<char, 0x5000> embedded{};
    const int embeddedSize = packet_helper_detail::AssemblePacket(
        embedded.data(), static_cast<int>(embedded.size()), "cddSdS", 0x4a, 0, 5, u"", -1, message.c_str()
    );
    SendBuilderEmbeddedPacket(builderObjectId, embeddedSize, embedded.data());
}

float MaximumDesirePriority() {
    const std::uint32_t bits = 0x72269b80U;
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

CNPC* ResolveMappedNpc(int sharedIndex) { return CNPC::GetObjectW(CCreatureFactory::Inst().GetIdByIndex(sharedIndex)); }

CCreature* ResolveMappedCreature(int sharedIndex) {
    return CCreature::GetObjectW(static_cast<std::uint32_t>(CCreatureFactory::Inst().GetIdByIndex(sharedIndex)));
}

std::uint64_t PackSkillNameId(int skillId, int skillLevel, int skillSubLevel) {
    const auto signedBits = [](int value) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(value)); };
    return ((signedBits(skillId) << 16) | signedBits(skillLevel)) << 16 | signedBits(skillSubLevel);
}

bool EqualLegacyWide(const NpcPosEventName& value, std::u16string_view expected) {
    return value.size() == expected.size() && std::equal(value.begin(), value.end(), expected.begin());
}

bool EqualLegacyWideCaseInsensitive(const NpcPosEventName& value, std::u16string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto foldAscii = [](char16_t codeUnit) {
            return codeUnit >= u'A' && codeUnit <= u'Z' ? static_cast<char16_t>(codeUnit + (u'a' - u'A')) : codeUnit;
        };
        if (foldAscii(value[index]) != foldAscii(expected[index])) {
            return false;
        }
    }
    return true;
}

int ParseLegacyOnOff(const NpcPosEventName& value) {
    if (EqualLegacyWideCaseInsensitive(value, u"on") || EqualLegacyWide(value, u"켬")) {
        return 1;
    }
    if (EqualLegacyWideCaseInsensitive(value, u"off") || EqualLegacyWide(value, u"끔")) {
        return 0;
    }
    return -1;
}

} // namespace

bool BuilderCmdShowPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int builderObjectId;
    int showClass;
    packet = ::Disassemble(packet, "dd", &builderObjectId, &showClass);

    User* const builder = User::GetObjectW(builderObjectId);
    if (!builder) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%s : no builder object[%x]",
        //     L"BuilderCmdShowPacket",
        //     builderObjectId)
        // Log.Add(LOG_WARN,
        //     "[builder] Cannot show NPC data for user object %u: user not found (show class %d).",
        //     static_cast<unsigned int>(builderObjectId), showClass)
        L2_LOG_WARN(
            Log, "[builder] Cannot show NPC data for user object %u: user not found (show class %d).",
            static_cast<unsigned int>(builderObjectId), showClass
        );
        return false;
    }

    if (showClass == 1) {
        std::array<char16_t, 20000 / sizeof(char16_t)> superPointNameBuffer;
        superPointNameBuffer.back() = u'\0';
        packet = ::Disassemble(packet, "S", 20000, superPointNameBuffer.data());
        const l2geo::CSuperPoint* const superPoint = l2geo::g_SuperPointManager.Find(superPointNameBuffer.data());
        if (!superPoint) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"%s : no superpoint[%s]",
            //     L"BuilderCmdShowPacket",
            //     Utf16ToWide(
            //         superPointNameBuffer.data()).c_str())
            // Log.Add(LOG_WARN,
            //     "[builder] Cannot show superpoint '%s' for user object %u: superpoint not found.",
            //     Utf16ToUtf8(superPointNameBuffer.data()).c_str(), static_cast<unsigned int>(builderObjectId))
            L2_LOG_WARN(
                Log, "[builder] Cannot show superpoint '%s' for user object %u: superpoint not found.",
                Utf16ToUtf8(superPointNameBuffer.data()).c_str(), static_cast<unsigned int>(builderObjectId)
            );
            return false;
        }
        superPoint->ShowSuperPointToBuilder(*builder);
        return false;
    }

    if (showClass == 2) {
        constexpr std::size_t packetCapacity = 0x5000;
        const FVector builderPosition = builder->GetPos();

        const auto* const territoryMap = g_NpcMakerDB.GetTerritoryMap();
        for (const auto& namedTerritory : *territoryMap) {
            CTerritory* const territory = namedTerritory.second;

            const FVector center = territory->GetCenterPos();
            const double deltaX = center.x - builderPosition.x;
            const double deltaY = center.y - builderPosition.y;
            const double deltaZ = center.z - builderPosition.z;
            if (std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) > 32768.0) {
                continue;
            }

            std::array<char, packetCapacity> primitives;
            int usedSize = 0;
            int primitiveCount = 0;
            const int pointCount = static_cast<int>(territory->m_TerritoryPoints.size());
            const std::u16string territoryNameUtf16 = SanitizeUtf16(territory->sName.data());
            const std::wstring territoryName = Utf16ToWide(territoryNameUtf16);
            for (int index = 0; index < pointCount; ++index) {
                const int nextIndex = (index + 1) % pointCount;
                const FVector2D& first = runtime::CheckedVectorAt(territory->m_TerritoryPoints, static_cast<std::size_t>(index));
                const FVector2D& second =
                    runtime::CheckedVectorAt(territory->m_TerritoryPoints, static_cast<std::size_t>(nextIndex));
                std::array<wchar_t, 10000> label;
                label.back() = L'\0';
                std::swprintf(label.data(), label.size(), L"%ls(%d->%d)", territoryName.c_str(), index, nextIndex);

                const FVector firstTop{
                    static_cast<double>(first.x),
                    static_cast<double>(first.y),
                    static_cast<double>(territory->m_Top),
                };
                const FVector secondTop{
                    static_cast<double>(second.x),
                    static_cast<double>(second.y),
                    static_cast<double>(territory->m_Top),
                };
                const FVector firstBottom{
                    static_cast<double>(first.x),
                    static_cast<double>(first.y),
                    static_cast<double>(territory->m_Bottom),
                };
                const FVector secondBottom{
                    static_cast<double>(second.x),
                    static_cast<double>(second.y),
                    static_cast<double>(territory->m_Bottom),
                };

                const auto appendLine = [&](const wchar_t* name, const FVector& from, const FVector& to) {
                    const int assembled = AssembleServerPrimitiveLine(
                        primitives.data() + usedSize,
                        static_cast<unsigned int>(packetCapacity - static_cast<std::size_t>(usedSize)), name, 0, 255, 0, 255,
                        from, to
                    );
                    if (assembled != 0) {
                        usedSize += assembled;
                        ++primitiveCount;
                    }
                };
                appendLine(label.data(), firstTop, secondTop);
                appendLine(label.data(), firstBottom, secondBottom);
                appendLine(L"", firstBottom, firstTop);
            }

            std::array<char, packetCapacity> embedded;
            const std::u16string wireTerritoryName = territoryNameUtf16;
            const int embeddedSize = packet_helper_detail::AssemblePacket(
                embedded.data(), static_cast<int>(embedded.size()), "chSddddddb", 0xfe, 0x11, wireTerritoryName.c_str(),
                runtime::Win64CvttDoubleToInt32(center.x), runtime::Win64CvttDoubleToInt32(center.y),
                runtime::Win64CvttDoubleToInt32(center.z), 0x8000, 0x8000, primitiveCount, usedSize, primitives.data()
            );
            SendBuilderEmbeddedPacket(builderObjectId, embeddedSize, embedded.data());
        }
        return false;
    }

    if (showClass == 3) {
        std::array<char16_t, 0x400 / sizeof(char16_t)> roomInfoListName;
        int roomIndex;
        packet = ::Disassemble(packet, "Sd", 0x400, roomInfoListName.data(), &roomIndex);
        RoomInfoList* const list = gGlobalObj.GetRoomInfoList(roomInfoListName.data());
        if (!list || roomIndex < 0 || roomIndex >= list->mSize) {
            return false;
        }
        RoomInfo* const room = list->GetRoomInfo(roomIndex);

        const DateTime dateTime(TimeValue(static_cast<std::int32_t>(room->mTime), 0));
        std::array<wchar_t, 0x200> message;
        std::swprintf(
            message.data(), message.size(),
            L"time[%02d:%02d:%02d], paty_id[%d], "
            L"member_count[%d], value[%d]",
            dateTime.Hour(), dateTime.Minute(), dateTime.Second(), room->mPartyID, room->mMemberCount,
            room->mStatus.GetValue()
        );
        const std::u16string wireMessage = WideToUtf16(message.data());

        std::array<char, 0x5000> embedded;
        const int embeddedSize = packet_helper_detail::AssemblePacket(
            embedded.data(), static_cast<int>(embedded.size()), "cddSdS", 0x4a, 0, 5, u"", -1, wireMessage.c_str()
        );
        SendBuilderEmbeddedPacket(builderObjectId, embeddedSize, embedded.data());
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"%s : invalid show class[%d]",
    //     L"BuilderCmdShowPacket",
    //     showClass)
    // Log.Add(LOG_WARN,
    //     "[builder] Unsupported NPC show class %d for user object %u. Request ignored.",
    //     showClass, static_cast<unsigned int>(builderObjectId))
    L2_LOG_WARN(
        Log, "[builder] Unsupported NPC show class %d for user object %u; request ignored.", showClass,
        static_cast<unsigned int>(builderObjectId)
    );
    return false;
}


void CDBNPCReset::TimerExpired(int) {
    g_bDBNPCReset = false;
    // 0x0051a690 releases the constructor's ORT_CREATE_DELETE owner here.
    // CIOObject's dispatch epilogue then releases ORT_TIMER and deletes this
    // independent one-shot object.
    L2_RELEASE(this, ::ObjectRefType::CreateDelete);
}

CServerSocket::CServerSocket(std::uint64_t socket) : CSocket(socket, 0x1000) {
    L2_GUARD_FUNCTION();
    m_pPacketTable = gPacketTableFromServer.GetData();
}

CServerSocket::~CServerSocket() {
    // Current EXE 0x005213ec records this frame before entering the
    // compiler-generated CSocket destructor chain.
    L2_GUARD_FUNCTION();
}

CSocket* CServerSocket::Allocate(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x005222b4.
    return new CServerSocket(socket);
}

void CServerSocket::OnCreate() {
    // Current EXE 0x0052cf04 calls CIOSocketEx::Read(0), then arms timer 1.
    L2_GUARD_FUNCTION();
    Read(0);
    AddTimer(60000U, 1);
}

void CServerSocket::TimerExpired(int timerId) {
    L2_GUARD_FUNCTION();
    if (timerId == 1) {
        CheckHourForPacketStat();
        AddTimer(60000U, 1);
    }
}

void CServerSocket::CheckHourForPacketStat() {
    L2_GUARD_FUNCTION();
    const std::int32_t currentTime = runtime::crt::Time32Now();
    const std::tm* const current = runtime::crt::LocalTime32(currentTime);
    const int currentHour = current->tm_hour;
    const int currentMinute = current->tm_min;
    const std::tm* const previous = runtime::crt::LocalTime32(m_nLastCheckedHourForPacketStat);
    if (previous->tm_hour == currentHour || currentMinute != 59) {
        return;
    }

    m_nLastCheckedHourForPacketStat = currentTime;
    // Exact CheckHourForPacketStat calls only ReportRank(LOG_ERROR,
    // PRED_CNT) for both directions, then clears both templates.
    // Routine hourly statistics use INFO. Ranking and reset order are unchanged.
    // ServerToNpcPacketStat::Inst().
    //     ReportRank(
    //         LOG_ERROR,
    //         ServerToNpcPacketStat::PRED_CNT)
    ServerToNpcPacketStat::Inst().ReportRank(LOG_NORMAL, ServerToNpcPacketStat::PRED_CNT);
    // NpcToServerPacketStat::Inst().
    //     ReportRank(
    //         LOG_ERROR,
    //         NpcToServerPacketStat::PRED_CNT)
    NpcToServerPacketStat::Inst().ReportRank(LOG_NORMAL, NpcToServerPacketStat::PRED_CNT);
    ServerToNpcPacketStat::Inst().Reset();
    NpcToServerPacketStat::Inst().Reset();
}

void CServerSocket::OnRead() {
    L2_GUARD_FUNCTION();
    if (m_Mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_Mode == SocketMode::SM_READ_LEN) {
            if (readSize - packetOffset < 2) {
                Read(readSize - packetOffset);
                return;
            }

            const std::uint8_t* const length = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_nPacketLen = static_cast<std::int32_t>(
                               static_cast<std::uint32_t>(length[0]) | (static_cast<std::uint32_t>(length[1]) << 8U)
                           ) -
                2;
            if (m_nPacketLen < 1 || m_nPacketLen > 0x4ffe) {
                // Noise: Read offset and buffered byte count are included in the failure record below.
                // Log.Add(
                //     LOG_ERROR,
                //     "pi[%d] ri[%d]",
                //     static_cast<int>(
                //         packetOffset),
                //     static_cast<int>(
                //         readSize))
                // Noise: One line per raw packet byte floods the crash log. The parsed frame/opcode context below identifies
                // this failure. for (std::uint32_t index = 0
                //      index < readSize.
                //      ++index) {
                //     Log.Add(
                //         LOG_ERROR,
                //         "data[%d]",
                //         static_cast<int>(
                //             static_cast<std::int8_t>(
                //                 m_pReadBuf->
                //                     m_Buffer[index])))
                // }
                // Log.Add(
                //     LOG_ERROR,
                //     "CServerSocket %x: bad packet size %d",
                //     static_cast<unsigned int>(
                //         runtime::LegacySocketImportAddressToken()),
                //     m_nPacketLen)
                // Log.Add(LOG_ERROR,
                //     "[L2Server] Invalid frame length %d bytes (payload %d. Allowed payload 1..20478) at offset %d of %d
                //     buffered bytes. IO buffers in pool %d. Terminating.", m_nPacketLen + 2, m_nPacketLen, packetOffset,
                //     readSize, GetIOBufferPoolCount())
                L2_LOG_ERROR(
                    Log,
                    "[L2Server] Invalid frame length %d bytes (payload %d; allowed payload 1..20478) at offset %d of %d "
                    "buffered bytes; IO buffers in pool %d. Terminating.",
                    m_nPacketLen + 2, m_nPacketLen, packetOffset, readSize, GetIOBufferPoolCount()
                );
                // Noise: The buffer pool count is included in the invalid-frame record above.
                // Log.Add(
                //     LOG_ERROR,
                //     "IOBufferPool(%d)",
                //     GetIOBufferPoolCount())
                ForceCrash(CrashInfo::SERVER_PACKET_SIZE_IS_INVALID);
                // Current EXE 0x0052d314..0x0052d419 keeps this path after
                // the deliberate null write. A host signal handler may
                // return, so keep the target's close transition too.
                m_Mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_Mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        }

        if (m_Mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }
        if (readSize - packetOffset < m_nPacketLen) {
            Read(readSize - packetOffset);
            return;
        }

        ++m_nPacketCount;
        const std::uint8_t opcode = m_pReadBuf->m_Buffer[packetOffset];
        if (opcode >= 200) {
            // Noise: Read offset and buffered byte count are included in the failure record below.
            // Log.Add(
            //     LOG_ERROR,
            //     "pi[%d] ri[%d]",
            //     static_cast<int>(packetOffset),
            //     static_cast<int>(readSize))
            // Noise: One line per raw packet byte floods the crash log. The parsed frame/opcode context below identifies
            // this failure. for (std::uint32_t index = 0
            //      index < readSize.
            //      ++index) {
            //     Log.Add(
            //         LOG_ERROR,
            //         "data[%d]",
            //         static_cast<int>(
            //             static_cast<std::int8_t>(
            //                 m_pReadBuf->
            //                     m_Buffer[index])))
            // }
            // Log.Add(
            //     LOG_ERROR,
            //     "CServerSocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.Add(LOG_ERROR,
            //     "[L2Server] Unsupported opcode 0x%02X (allowed 0x00..0xC7), payload %d bytes at offset %d of %d buffered
            //     bytes, terminating.", static_cast<unsigned int>(opcode), m_nPacketLen, packetOffset, readSize)
            L2_LOG_ERROR(
                Log,
                "[L2Server] Unsupported opcode 0x%02X (allowed 0x00..0xC7), payload %d bytes at offset %d of %d buffered "
                "bytes; terminating.",
                static_cast<unsigned int>(opcode), m_nPacketLen, packetOffset, readSize
            );
            ForceCrash(CrashInfo::SERVER_UNKNOWN_PROTOCOL);
            // Both fatal parser branches share the same target tail:
            // SM_CLOSE is published before the virtual Close call.
            m_Mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }
        auto* const packet = new CServerPacket;
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = static_cast<L2ServerCallback*>(m_pPacketTable)[opcode];
        L2_ADD_REF(this, ::ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();

        // Current EXE 0x0052d175..0x0052d1ea updates the process-global
        // CPacketStat<200,200> before posting the completion.
        // count complete frames, including the length prefix,
        // consistently with the sender and both NPC-protocol directions.
        // ServerToNpcPacketStat::Inst().Accumulate(
        //     static_cast<int>(opcode), m_nPacketLen)
        ServerToNpcPacketStat::Inst().Accumulate(static_cast<int>(opcode), m_nPacketLen + 2);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        // The shipping code ignores PostQueuedCompletionStatus' result.
        // A failed post so leaves the target keeps pending.
        packet->PostObject(static_cast<int>(packetOffset));

        packetOffset += m_nPacketLen;
        m_Mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

void CServerSocket::OnClose() {
    // Current EXE 0x00521488.
    L2_GUARD_FUNCTION();
    m_Mode = SocketMode::SM_CLOSE;
    const std::int32_t closedSocket =
        runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_Socket), 0);
    const int lastError = GetSocketLastError();
    // Log.Add(
    //     LOG_ERROR,
    //     "server socket close %x(%x) error(%d)",
    //     static_cast<unsigned int>(
    //         closedSocket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<
    //             std::uintptr_t>(this)),
    //     lastError)
    // Log.Add(LOG_ERROR,
    //     "[L2Server] NPC connection %u closed. Terminating because this connection is required. Ambient socket error %d at
    //     callback entry may not identify the cause.", static_cast<unsigned int>(closedSocket), lastError)
    // name the peer and show the process-local socket in decimal.
    // L2_LOG_ERROR(Log,
    //     "[L2Server] NPC connection %u closed. Terminating because this connection is required. Ambient socket error %d at
    //     callback entry may not identify the cause.", static_cast<unsigned int>(closedSocket), lastError)
    L2_LOG_ERROR(
        Log,
        "[L2Server] Connection closed: peer=L2Server socket=%u. Terminating because this connection is required. Ambient "
        "socket error %d at callback entry may not identify the cause.",
        static_cast<unsigned int>(closedSocket), lastError
    );
    // Original: ReportCallStack, then ForceCrash(CrashInfo::SERVER_SOCKET_CLOSE).
    // A lost L2Server is an expected stop, not a defect, so exit without a crash report.
    L2_LOG_ERROR(
        Log, "[shutdown] L2NPC is stopping with exit code %d. Start it again after L2Server is running.",
        l2npc::portable::kRequiredPeerClosedExitCode
    );
    l2npc::portable::ExitAfterRequiredPeerClosed();
}

bool CServerSocket::CheckPendingWrite() {
    // Current EXE 0x00521738.
    L2_GUARD_FUNCTION();
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid server socket.",
        //     L"serversocket.cpp",
        //     0x2273)
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Cannot send: the socket handle is invalid, rejecting this write.")
        L2_LOG_ERROR(Log, "[L2Server] Cannot send: the socket handle is invalid; rejecting this write.");
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x1000001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"server socket pending write > %d KB, Close!",
    //     0x4000)
    // Log.Add(LOG_ERROR,
    //     "[L2Server] Pending output exceeded 16777216 bytes. Rejecting this write (observed now %d bytes).",
    //     m_nPendingWrite.load(std::memory_order_relaxed))
    L2_LOG_ERROR(
        Log, "[L2Server] Pending output exceeded 16777216 bytes; rejecting this write (observed now %d bytes).",
        m_nPendingWrite.load(std::memory_order_relaxed)
    );
    ReportCallStack(LOG_ERROR, false);
    return false;
}

void CServerSocket::OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const {
    L2_GUARD_FUNCTION();
    int packetNo = static_cast<int>(opcode);
    // use the outgoing frame size supplied by CSocket::Send
    // and SendV. The current EXE at 0x0052c830 reads +0xd0 (the last incoming
    // payload length) for ordinary packets. Preserve its expression below.
    // int recordedSize = m_nPacketLen
    const int recordedSize = packetSize;
    if (opcode >= 0xfe) {
        packetNo += static_cast<int>(extendedOpcode);
        // recordedSize = packetSize
    }
    // Current EXE 0x0052c760: ordinary packets use CSocket's
    // +0xd0 m_nPacketLen. Extended packets use the explicit total size.
    NpcToServerPacketStat::Inst().Accumulate(packetNo, recordedSize);
}

bool CServerSocket::IsServerSocket() { return true; }

bool CServerSocket::NpcPosEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 2000 / sizeof(char16_t)> eventName;
    int enabled;
    ::Disassemble(packet, "Sd", 2000, eventName.data(), &enabled);
    const std::string hostName = Utf16ToUtf8(eventName.data());
    // Log.Add(
    //     LOG_NORMAL,
    //     L"+ NPC POS EVENT : NAME=[%s], "
    //     L"ENABLED=[%d]",
    //     hostName.c_str(),
    //     enabled)
    // Log.Add(LOG_NORMAL,
    //     "[NPC event] Received request to %s position event '%s'.",
    //     enabled != 0 ? "enable" : "disable", WideToUtf8(hostName).c_str())
    L2_LOG_INFO(
        Log, "[NPC event] Received request to %s position event '%s'.", enabled != 0 ? "enable" : "disable",
        hostName.c_str()
    );
    CNpcMakerDB::Inst().OnNpcPosEvent(eventName.data(), enabled != 0);
    return false;
}

bool CServerSocket::NpcPosesEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int despawnCount = 0;
    int spawnCount = 0;
    packet = ::Disassemble(packet, "dd", &despawnCount, &spawnCount);

    NpcPosEventNameList despawn;
    NpcPosEventNameList spawn;
    std::array<char16_t, 2000 / sizeof(char16_t)> name;
    for (int index = 0; index < despawnCount; ++index) {
        packet = ::Disassemble(packet, "S", 2000, name.data());
        despawn.emplace_back(name.data());
    }
    for (int index = 0; index < spawnCount; ++index) {
        packet = ::Disassemble(packet, "S", 2000, name.data());
        spawn.emplace_back(name.data());
    }

    // The shipping callback logs only after both vectors have been
    // constructed, immediately before CNpcMakerDB::OnNpcPosesEvent.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"+ NPC POSES EVENT : despawn=[%d], spawn=[%d]",
    //     despawnCount,
    //     spawnCount)
    // Log.Add(LOG_NORMAL,
    //     "[NPC event] Dispatching position-event batch: %zu disable entries and %zu enable entries.",
    //     despawn.size(), spawn.size())
    L2_LOG_INFO(
        Log, "[NPC event] Dispatching position-event batch: %zu disable entries and %zu enable entries.", despawn.size(),
        spawn.size()
    );
    CNpcMakerDB::Inst().OnNpcPosesEvent(despawn, spawn);
    return false;
}

// Current EXE 0x0051c998. The adjacent matching PDB identifies the original
// serversocket.cpp callback as FortressMakerEvent.
bool FortressMakerEvent(CSocket*, const unsigned char* packet) {
    int residenceId = 0;
    int eventId = 0;
    int argument0 = 0;
    int argument1 = 0;
    int argument2 = 0;
    std::array<char16_t, 100 / sizeof(char16_t)> text;
    ::Disassemble(packet, "dddddS", &residenceId, &eventId, &argument0, &argument1, &argument2, 100, text.data());

    CNpcMakerDB::Inst().FortressEvent(residenceId, eventId, argument0, argument1, argument2, text.data());
    return false;
}

// Current EXE 0x0051ca38. Original PDB name FortressAIEvent.
bool FortressAIEvent(CSocket*, const unsigned char* packet) {
    int ignoredResidenceId = 0;
    int eventId = 0;
    int npcObjectId = 0;
    int userObjectId = 0;
    int reply = 0;
    int ignoredArgument0 = 0;
    int ignoredArgument1 = 0;
    ::Disassemble(
        packet, "ddddddd", &ignoredResidenceId, &eventId, &npcObjectId, &userObjectId, &reply, &ignoredArgument0,
        &ignoredArgument1
    );

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    CSharedCreatureData* const userData = user ? user->m_data : nullptr;
    if (!npc || !userData) {
        return false;
    }

    CNPCEvent* event = nullptr;
    switch (eventId) {
        case 0:
            event = CNPCEventFactory::CreateFortressSiegeRegisterReturned(npc, userData, reply);
            break;
        case 1:
            event = CNPCEventFactory::CreateFortressSiegeUnregisterReturned(npc, userData, reply);
            break;
        case 2:
            event = CNPCEventFactory::CreateFortressContractCastleReturned(npc, userData, reply);
            break;
        case 3:
            event = CNPCEventFactory::CreateFortressUpgradeFacilityReturned(npc, userData, reply);
            break;
        default:
            break;
    }

    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fortress AI Event failed. event id[%d]",
        //     L"serversocket.cpp",
        //     0x1567,
        //     eventId)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot dispatch fortress event %d to NPC %u for user %u (reply %d): %s.",
        //     eventId, static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
        //     eventId < 0 || eventId > 3 ? "unsupported event ID" : "event creation returned no event")
        L2_LOG_ERROR(
            Log, "[AI event] Cannot dispatch fortress event %d to NPC %u for user %u (reply %d): %s.", eventId,
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
            eventId < 0 || eventId > 3 ? "unsupported event ID" : "event creation returned no event"
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

// Current EXE 0x0051cba8. Original PDB name PledgeAIEvent.
bool PledgeAIEvent(CSocket*, const unsigned char* packet) {
    int ignoredResidenceId = 0;
    int eventId = 0;
    int npcObjectId = 0;
    int userObjectId = 0;
    int reply = 0;
    ::Disassemble(packet, "ddddd", &ignoredResidenceId, &eventId, &npcObjectId, &userObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    CSharedCreatureData* const userData = user ? user->m_data : nullptr;
    if (!npc || !userData) {
        return false;
    }

    CNPCEvent* const event =
        eventId == 0 ? CNPCEventFactory::CreateCastleSiegeDefenceCountDecreaseReturned(npc, userData, reply) : nullptr;
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Pledge AI Event failed. event id[%d]",
        //     L"serversocket.cpp",
        //     0x15a2,
        //     eventId)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot dispatch pledge event %d to NPC %u for user %u (reply %d): %s.",
        //     eventId, static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
        //     eventId != 0 ? "unsupported event ID" : "event creation returned no event")
        L2_LOG_ERROR(
            Log, "[AI event] Cannot dispatch pledge event %d to NPC %u for user %u (reply %d): %s.", eventId,
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
            eventId != 0 ? "unsupported event ID" : "event creation returned no event"
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

// Current EXE 0x0051ccc0. Original PDB name InstantZoneMakerEvent.
bool InstantZoneMakerEvent(CSocket*, const unsigned char* packet) {
    int eventId = 0;
    int inzoneTypeId = 0;
    int clusterId = 0;
    int inzoneId = 0;
    const std::uint8_t* const packetTail = ::Disassemble(packet, "dddd", &eventId, &inzoneTypeId, &clusterId, &inzoneId);
    CNpcMakerDB::Inst().InstantZoneEvent(inzoneTypeId, clusterId, inzoneId, eventId, 0, packetTail);
    return false;
}

// Current EXE 0x0051cd38. Original PDB name InstantZoneAIEvent.
bool InstantZoneAIEvent(CSocket*, const unsigned char* packet) {
    int eventId = 0;
    int inzoneId = 0;
    int ignoredClusterId = 0;
    int npcObjectId = 0;
    int userObjectId = 0;
    int replyOrDuration = 0;
    int stateOrUserCount = 0;
    packet = ::Disassemble(
        packet, "ddddddd", &eventId, &inzoneId, &ignoredClusterId, &npcObjectId, &userObjectId, &replyOrDuration,
        &stateOrUserCount
    );

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npc is null! event[%d] inzone [%d] npc [%d]",
        //     L"serversocket.cpp",
        //     0x15da,
        //     eventId,
        //     inzoneId,
        //     npcObjectId)
        // Log.Add(LOG_WARN,
        //     "[AI event] Ignoring instance-zone event %d for zone %d: NPC %u was not found.",
        //     eventId, inzoneId, static_cast<unsigned int>(npcObjectId))
        L2_LOG_WARN(
            Log, "[AI event] Ignoring instance-zone event %d for zone %d: NPC %u was not found.", eventId, inzoneId,
            static_cast<unsigned int>(npcObjectId)
        );
        return false;
    }

    if (eventId == 0) {
        int integer0 = 0;
        packet = ::Disassemble(packet, "d", &integer0);
        User* const user = User::GetObjectW(userObjectId);
        CSharedCreatureData* const userData = user ? user->m_data : nullptr;
        if (!userData) {
            return false;
        }

        CNPCEvent* const event =
            CNPCEventFactory::CreateInstantZoneEnterReturned(npc, userData, replyOrDuration, stateOrUserCount, integer0);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0x15f4)
            // Log.Add(LOG_ERROR,
            //     "[AI event] Cannot create the instance-zone entry reply for NPC %u, user %u, zone %d. Event not
            //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), inzoneId)
            L2_LOG_ERROR(
                Log,
                "[AI event] Cannot create the instance-zone entry reply for NPC %u, user %u, zone %d; event not dispatched.",
                static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), inzoneId
            );
            return false;
        }

        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    if (eventId == 1) {
        CNPCEvent* const event = CNPCEventFactory::CreateInstantZoneRemainDuration(npc, replyOrDuration);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0x1603)
            // Log.Add(LOG_ERROR,
            //     "[AI event] Cannot create the remaining-duration event for NPC %u, zone %d (duration %d). Event not
            //     dispatched.", static_cast<unsigned int>(npcObjectId), inzoneId, replyOrDuration)
            L2_LOG_ERROR(
                Log,
                "[AI event] Cannot create the remaining-duration event for NPC %u, zone %d (duration %d); event not "
                "dispatched.",
                static_cast<unsigned int>(npcObjectId), inzoneId, replyOrDuration
            );
            return false;
        }

        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    if (eventId != 2 || stateOrUserCount <= 0) {
        return false;
    }

    int listedUserObjectId = 0;
    for (int index = 0; index < stateOrUserCount; ++index) {
        packet = ::Disassemble(packet, "d", &listedUserObjectId);
        User* const user = User::GetObjectW(listedUserObjectId);
        CSharedCreatureData* const userData = user ? user->m_data : nullptr;
        if (!userData) {
            continue;
        }

        CNPCEvent* const event = CNPCEventFactory::CreateInZoneUserEvent(npc, userData, stateOrUserCount);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0x161c)
            // Log.Add(LOG_ERROR,
            //     "[AI event] Cannot create the instance-zone user event for NPC %u, user %u, zone %d (entry %d of %d)
            //     skipping this entry.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned
            //     int>(listedUserObjectId), inzoneId, index + 1, stateOrUserCount)
            L2_LOG_ERROR(
                Log,
                "[AI event] Cannot create the instance-zone user event for NPC %u, user %u, zone %d (entry %d of %d); "
                "skipping this entry.",
                static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(listedUserObjectId), inzoneId, index + 1,
                stateOrUserCount
            );
            continue;
        }

        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    }
    return false;
}

bool RegisterPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply = 0;
    int declaredCount = 0;
    int ignoredTail = 0;
    std::array<int, 5> values{};
    packet = ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    packet = ::Disassemble(packet, "dd", &reply, &declaredCount);
    for (int index = 0; index < std::min(declaredCount, 5); ++index) {
        packet = ::Disassemble(packet, "d", &values[static_cast<std::size_t>(index)]);
    }
    packet = ::Disassemble(packet, "d", &ignoredTail);

    CNPCEvent* const event = CNPCEventFactory::CreateRegisterPVPMatchResultEvent(
        npc, user->m_data, reply, static_cast<std::uint32_t>(values[0]), static_cast<std::uint32_t>(values[1]),
        static_cast<std::uint32_t>(values[2]), static_cast<std::uint32_t>(values[3]), static_cast<std::uint32_t>(values[4])
    );
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool UnRegisterPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int declaredCount = 0;
    int reply = 0;
    std::array<int, 5> values{};
    packet = ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &declaredCount);
    for (int index = 0; index < std::min(declaredCount, 5); ++index) {
        packet = ::Disassemble(packet, "d", &values[static_cast<std::size_t>(index)]);
    }
    packet = ::Disassemble(packet, "d", &reply);

    CNPCEvent* const event = CNPCEventFactory::CreateUnRegisterPVPMatchResultEvent(
        npc, user->m_data, static_cast<std::uint32_t>(values[0]), static_cast<std::uint32_t>(values[1]),
        static_cast<std::uint32_t>(values[2]), static_cast<std::uint32_t>(values[3]), static_cast<std::uint32_t>(values[4]),
        reply
    );
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool StartPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int argument0 = 0;
    int argument1 = 0;
    int reply = 0;
    packet = ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    packet = ::Disassemble(packet, "ddd", &argument0, &argument1, &reply);

    CNPCEvent* const event = CNPCEventFactory::CreateStartPVPMatchResult(
        npc, reply, static_cast<std::uint32_t>(argument0), static_cast<std::uint32_t>(argument1)
    );
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool EndPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int declaredCount = 0;
    int reply = 0;
    std::array<int, 5> values{};
    packet = ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &declaredCount);
    for (int index = 0; index < std::min(declaredCount, 5); ++index) {
        packet = ::Disassemble(packet, "d", &values[static_cast<std::size_t>(index)]);
    }
    packet = ::Disassemble(packet, "d", &reply);

    CNPCEvent* const event = CNPCEventFactory::CreateEndPVPMatchResult(
        npc, static_cast<std::uint32_t>(values[0]), static_cast<std::uint32_t>(values[1]),
        static_cast<std::uint32_t>(values[2]), static_cast<std::uint32_t>(values[3]), static_cast<std::uint32_t>(values[4]),
        reply
    );
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CheckRegisterPartyResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int selector = 0;
    packet = ::Disassemble(packet, "dd", &npcObjectId, &selector);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    if (selector == 1) {
        int argument;
        int reply;
        packet = ::Disassemble(packet, "dd", &argument, &reply);
        CNPCEvent* const event =
            CNPCEventFactory::CreateCheckRegisterPartyResult(npc, static_cast<std::uint32_t>(argument), reply);
        if (!event) {
            return false;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    if (selector == 2) {
        int argument0 = 0;
        int argument1 = 0;
        int argument2 = 0;
        int argument3 = 0;
        packet = ::Disassemble(packet, "dddd", &argument0, &argument1, &argument2, &argument3);
        CNPCEvent* const event = CNPCEventFactory::CreateCheckRegisterPartyResult2(
            npc, static_cast<std::uint32_t>(argument0), argument1, static_cast<std::uint32_t>(argument2), argument3
        );
        if (!event) {
            return false;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    }
    return false;
}

bool RegisterResurrectionTowerResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int argument;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &argument, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event =
        CNPCEventFactory::CreateRegisterResurrectionTowerResult(npc, static_cast<std::uint32_t>(argument), reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CheckRegisterUserResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int argument = 0;
    int reply = 0;
    packet = ::Disassemble(packet, "dd", &npcObjectId, &argument);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &reply);
    CNPCEvent* const event =
        CNPCEventFactory::CreateCheckRegisterUserResult(npc, static_cast<std::uint32_t>(argument), reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool IsUserPVPMatchingResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int argument = 0;
    int reply = 0;
    packet = ::Disassemble(packet, "dd", &npcObjectId, &argument);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &reply);
    CNPCEvent* const event =
        CNPCEventFactory::CreateIsUserPVPMatchingResult(npc, static_cast<std::uint32_t>(argument), reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RegisterUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply = 0;
    packet = ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &reply);
    CNPCEvent* const event = CNPCEventFactory::CreateRegisterUserPVPMatchResultEvent(npc, user->m_data, reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool UnregisterUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateUnregisterUserPVPMatchResultEvent(npc, user->m_data, 0);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RegisterUserResurrectionTowerResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int argument;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &argument, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event =
        CNPCEventFactory::CreateRegisterUserResurrectionTowerResult(npc, static_cast<std::uint32_t>(argument), reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GetRankUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int reply = 0;
    packet = ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &reply);
    CNPCEvent* const event = CNPCEventFactory::CreateGetRankUserPVPMatchResultEvent(npc, user->m_data, reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool LetInUserPVPMatch(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateLetInUserPVPMatchEvent(npc, user->m_data);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool killedUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int talkerObjectId = 0;
    int attackerObjectId = 0;
    int reply = 0;
    packet = ::Disassemble(packet, "dd", &npcObjectId, &talkerObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const talker = User::GetObjectW(talkerObjectId);
    if (!npc || !talker) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &attackerObjectId);
    User* const attacker = User::GetObjectW(attackerObjectId);
    if (!attacker) {
        return false;
    }
    packet = ::Disassemble(packet, "d", &reply);
    CNPCEvent* const event =
        CNPCEventFactory::CreatekilledUserPVPMatchResultEvent(npc, talker->m_data, attacker->m_data, reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool StartUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateStartUserPVPMatchResult(npc);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool WithdrawUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateWithdrawUserPVPMatchResult(npc);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool EndUserPVPMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    std::array<int, 5> userObjectIds;
    packet = ::Disassemble(packet, "d", &npcObjectId);
    packet = ::Disassemble(
        packet, "ddddd", &userObjectIds[0], &userObjectIds[1], &userObjectIds[2], &userObjectIds[3], &userObjectIds[4]
    );

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    std::array<CSharedCreatureData*, 5> users{};
    for (std::size_t index = 0; index < userObjectIds.size(); ++index) {
        User* const user = User::GetObjectW(userObjectIds[index]);
        users[index] = user ? user->m_data : nullptr;
    }
    CNPCEvent* const event =
        CNPCEventFactory::CreateEndUserPVPMatchResult(npc, users[0], users[1], users[2], users[3], users[4]);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ChangeNPCStatusResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int state = 0;
    ::Disassemble(packet, "dd", &npcObjectId, &state);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateChangeNPCStatusReturned(npc, state);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x18ad)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the NPC status reply for NPC %u (state %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), state)
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the NPC status reply for NPC %u (state %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), state
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RegisterDominionSiege(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int requestArgument = 0;
    int userObjectId = 0;
    int state = 0;
    int resultArgument = 0;
    int success = 0;
    ::Disassemble(packet, "dddddd", &npcObjectId, &requestArgument, &userObjectId, &state, &resultArgument, &success);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateRegisterDominionSiegeReturned(
        npc, user->m_data, requestArgument, state, resultArgument, success
    );
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x18d1)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the dominion registration reply for NPC %u, user %u (state %d, result %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), state,
        //     success)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the dominion registration reply for NPC %u, user %u (state %d, result %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), state, success
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DeclareLord(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int argument = 0;
    int userObjectId = 0;
    int success = 0;
    ::Disassemble(packet, "dddd", &npcObjectId, &argument, &userObjectId, &success);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateDeclareLordReturned(npc, user->m_data, argument, success);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x18f2)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the lord declaration reply for NPC %u, user %u (result %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), success)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the lord declaration reply for NPC %u, user %u (result %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), success
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GetRegistrySize(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int requestArgument = 0;
    int userObjectId = 0;
    int state = 0;
    int registrySize = 0;
    ::Disassemble(packet, "ddddd", &npcObjectId, &requestArgument, &userObjectId, &state, &registrySize);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event =
        CNPCEventFactory::CreateGetRegistrySizeReturned(npc, user->m_data, requestArgument, state, registrySize);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1914)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the registration-list size reply for NPC %u, user %u (size %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), registrySize)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the registration-list size reply for NPC %u, user %u (size %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), registrySize
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DominionSiegeEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int selector = 0;
    int attackerObjectId = 0;
    int argument = 0;
    int talkerObjectId = 0;
    ::Disassemble(packet, "dddd", &selector, &attackerObjectId, &argument, &talkerObjectId);

    CNPC* const dominion = g_DominionNPC;
    if (!dominion) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Dominion NPC doesn't exist! cannot handle event.")
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot dispatch dominion siege event %d: the dominion NPC is unavailable.",
        //     selector)
        L2_LOG_WARN(Log, "[AI event] Cannot dispatch dominion siege event %d: the dominion NPC is unavailable.", selector);
        return false;
    }
    User* const talker = User::GetObjectW(talkerObjectId);
    if (!talker) {
        return false;
    }

    CNPCEvent* event = nullptr;
    switch (selector) {
        case 0:
            event = CNPCEventFactory::CreateDominionSiegeStart(dominion, talker->m_data, argument);
            break;
        case 1:
            event = CNPCEventFactory::CreateDominionSiegeEnd(dominion, talker->m_data, argument);
            break;
        case 2: {
            User* const attacker = User::GetObjectW(attackerObjectId);
            if (attacker) {
                event = CNPCEventFactory::CreateDominionSiegePCKilled(dominion, talker->m_data, attacker->m_data, argument);
            }
            break;
        }
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"Unknown dominion event %d.",
            //     selector)
            // Log.Add(LOG_ERROR,
            //     "[AI event] Unsupported dominion siege event %d for user %u. Event not dispatched.",
            //     selector, static_cast<unsigned int>(talkerObjectId))
            L2_LOG_ERROR(
                Log, "[AI event] Unsupported dominion siege event %d for user %u; event not dispatched.", selector,
                static_cast<unsigned int>(talkerObjectId)
            );
            return false;
    }

    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1950)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot dispatch dominion siege event %d for user %u, attacker %u: %s.",
        //     selector, static_cast<unsigned int>(talkerObjectId), static_cast<unsigned int>(attackerObjectId),
        //     selector == 2 ? "attacker lookup or event creation failed" : "event creation returned no event")
        L2_LOG_ERROR(
            Log, "[AI event] Cannot dispatch dominion siege event %d for user %u, attacker %u: %s.", selector,
            static_cast<unsigned int>(talkerObjectId), static_cast<unsigned int>(attackerObjectId),
            selector == 2 ? "attacker lookup or event creation failed" : "event creation returned no event"
        );
        return false;
    }
    dominion->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool UserLevelUpEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex;
    int level;
    ::Disassemble(packet, "dd", &userSharedIndex, &level);

    CNPC* const tutorial = g_TutorialNPC;
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!tutorial || !user || !user->m_bIsActive || !user->m_bIsUser) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateUserLevelUpEvent(tutorial, user, level);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xed0)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the level-up notification for user shared index %d (level %d). Event not
        //     dispatched.", userSharedIndex, level)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the level-up notification for user shared index %d (level %d); event not dispatched.",
            userSharedIndex, level
        );
        return false;
    }
    tutorial->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::CleftStateChangedEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateCleftStateChangedEvent(npc);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xef3)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the cleft-state notification for NPC %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the cleft-state notification for NPC %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::BlockUpsetStartedEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateBlockUpsetStartedEvent(npc);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xf11)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the block-upset start notification for NPC %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the block-upset start notification for NPC %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::BlockUpsetFinishedEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateBlockUpsetFinishedEvent(npc);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xf2f)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the block-upset finish notification for NPC %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the block-upset finish notification for NPC %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DominionScriptEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    int scriptArgument1;
    int scriptArgument2;
    int scriptArgument3;
    ::Disassemble(packet, "dddd", &userObjectId, &scriptArgument1, &scriptArgument2, &scriptArgument3);

    User* const user = User::GetObjectW(userObjectId);
    if (!user || !user->m_data) {
        return false;
    }
    CNPC* const dominion = g_DominionNPC;
    CNPCEvent* const event = CNPCEventFactory::CreateDominionScriptEvent(
        dominion, user->m_data, scriptArgument1, scriptArgument2, scriptArgument3
    );
    if (!event) {
        return false;
    }
    dominion->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DieSet(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateDieSetEvent(npc, user->m_data, npc->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x198d)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the DieSet notification for NPC %u, user %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the DieSet notification for NPC %u, user %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RestartSet(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateRestartSetEvent(npc, user->m_data, npc->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x19ac)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the RestartSet notification for NPC %u, user %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the RestartSet notification for NPC %u, user %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::SocialActionEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int action = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &action);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateSocialActionEvent(npc, user->m_data, action);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x19cd)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the social-action notification for NPC %u, user %u (action %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), action)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the social-action notification for NPC %u, user %u (action %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), action
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::PetDiedEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int petType = 0;
    int ownerObjectId = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &petType, &ownerObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreatePetDiedEvent(npc, petType, ownerObjectId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x19eb)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the pet-death notification for NPC %u (pet type %d, owner %u). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), petType, static_cast<unsigned int>(ownerObjectId))
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the pet-death notification for NPC %u (pet type %d, owner %u); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), petType, static_cast<unsigned int>(ownerObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::WasCollectedEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int argument = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &argument);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateWasCollectedEvent(npc, user->m_data, argument);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1a0a)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the collection notification for NPC %u, user %u (argument %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), argument)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the collection notification for NPC %u, user %u (argument %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), argument
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool AirShipJobEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    CAirShipEventManager::Inst().OnPacketArrived(packet);
    return false;
}

bool SetTopDesire(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    int actorObjectId;
    int actorSharedIndex;
    ::Disassemble(packet, "dddd", &ignored, &npcSharedIndex, &actorObjectId, &actorSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }
    CCreature* const actor = ResolveMappedCreature(actorSharedIndex);
    if (!actor) {
        return false;
    }

    const int argument = static_cast<int>(npc->m_data->m_nClass) == 0x51
        ? 0
        : runtime::Win64CvttFloatToInt32(
              static_cast<float>(static_cast<double>(npc->GetTopDesireValue(DESIRE_ATTACK)) + 1.0)
          );
    return npc->OnPacketDesireManipulation(actorObjectId, actorSharedIndex, argument, 0.0);
}

bool NpcPing(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int value;
    ::Disassemble(packet, "d", &value);
    socket->Send("cd", 0xcd, value);
    return false;
}

bool ServerPing(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int first;
    int second;
    ::Disassemble(packet, "dd", &first, &second);

    // The target samples the active NPCServer socket's queued-byte count
    // before it samples GetTickCount.
    const int pendingWrite = g_ns.PendingWrite();
    const int tick = static_cast<int>(runtime::GetTickCount());
    g_ns.Send("cdddd", 0x23, first, tick, second, pendingWrite);
    return false;
}

bool SiegeEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int residenceId;
    int eventId;
    int databaseRespawn;
    packet = ::Disassemble(packet, "ddd", &residenceId, &eventId, &databaseRespawn);

    switch (eventId) {
        case 1:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] Siege started!!! castle id[%d]",
            //     residenceId)
            // Log.Add(LOG_NORMAL,
            //     "[siege] Received castle %d siege-start notification (database respawn %s).",
            //     residenceId, databaseRespawn != 0 ? "enabled" : "disabled")
            L2_LOG_INFO(
                Log, "[siege] Received castle %d siege-start notification (database respawn %s).", residenceId,
                databaseRespawn != 0 ? "enabled" : "disabled"
            );
            break;
        case 2:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] Siege ended!!! castle id[%d]",
            //     residenceId)
            // Log.Add(LOG_NORMAL,
            //     "[siege] Received castle %d siege-end notification (database respawn %s).",
            //     residenceId, databaseRespawn != 0 ? "enabled" : "disabled")
            L2_LOG_INFO(
                Log, "[siege] Received castle %d siege-end notification (database respawn %s).", residenceId,
                databaseRespawn != 0 ? "enabled" : "disabled"
            );
            break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 8:
        case 9:
        case 10:
            break;
        case 7: {
            CNpcMakerDB::Inst().SiegeEvent(residenceId, eventId, databaseRespawn != 0);

            // The target zero-initializes this optional tail before its second
            // Disassemble call. A missing dword therefore performs GetObjectW(0)
            // and exits silently after the maker notification above.
            int userObjectId = 0;
            packet = ::Disassemble(packet, "d", &userObjectId);
            User* const user = User::GetObjectW(userObjectId);
            CNPC* const dominion = g_DominionNPC;
            if (!user || !dominion) {
                return false;
            }

            CNPCEvent* const event = CNPCEventFactory::CreateDominionSupplyDestructed(dominion, user->m_data, residenceId);
            if (!event) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Event failed",
                //     L"serversocket.cpp",
                //     0x8e2)
                // Log.Add(LOG_ERROR,
                //     "[siege] Cannot create the dominion supply-destruction notification for residence %d, user %u. Makers
                //     were notified, but the dominion NPC event was not dispatched.", residenceId, static_cast<unsigned
                //     int>(userObjectId))
                L2_LOG_ERROR(
                    Log,
                    "[siege] Cannot create the dominion supply-destruction notification for residence %d, user %u; makers "
                    "were notified, but the dominion NPC event was not dispatched.",
                    residenceId, static_cast<unsigned int>(userObjectId)
                );
                return false;
            }

            dominion->HandleEvent(event);
            L2_RELEASE(event, ::ObjectRefType::CreateDelete);
            return false;
        }
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Castle[%d] Undefined siege event id[%d]",
            //     L"serversocket.cpp",
            //     0x8f9,
            //     residenceId,
            //     eventId)
            // Log.Add(LOG_ERROR,
            //     "[siege] Ignoring unsupported event %d for castle %d (database respawn %s).",
            //     eventId, residenceId, databaseRespawn != 0 ? "enabled" : "disabled")
            L2_LOG_ERROR(
                Log, "[siege] Ignoring unsupported event %d for castle %d (database respawn %s).", eventId, residenceId,
                databaseRespawn != 0 ? "enabled" : "disabled"
            );
            return false;
    }

    // Current instructions at 0x0051a3af and 0x0051a42e move the decoded
    // database-respawn flag from BL into R9B. Ghidra incorrectly merges that
    // byte with the switch's event-id temporary in its C output.
    CNpcMakerDB::Inst().SiegeEvent(residenceId, eventId, databaseRespawn != 0);
    return false;
}

bool HolyThingSpelled(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredFirst;
    int ignoredSecond;
    int ignoredThird;
    ::Disassemble(packet, "ddd", &ignoredFirst, &ignoredSecond, &ignoredThird);
    return false;
}

bool PingForCastleSiege(CSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    CNpcMakerDB& makerDatabase = CNpcMakerDB::Inst();
    if (!makerDatabase.IsSiegeRelatedCompleted()) {
        return false;
    }

    const int loadingFinished = makerDatabase.m_stLoadingProcess.bIsFinished ? 1 : 0;
    const std::int32_t now = runtime::crt::Time32Now();
    g_ns.Send("cdd", 0x2f, now, loadingFinished);
    return false;
}

bool DeleteHatePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    int mode;
    ::Disassemble(packet, "ddd", &ignored, &npcSharedIndex, &mode);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }

    npc->RemoveAllAttackDesire();
    npc->RemoveAllHateInfoIF(0);
    if (mode == 0) {
        npc->StopMove();
        return false;
    }
    if (mode != 1) {
        return false;
    }

    npc->m_bBetrayOwnMaster = true;
    CCreature* const master = npc->GetMaster();
    if (!master || !master->m_data->m_bIsActive || !master->m_data->m_bIsAlive) {
        return false;
    }

    g_ns.Send("cdddd", 6, npc->m_data->index, static_cast<std::int32_t>(master->Id()), 1, 1);
    return false;
}

bool DeleteHateOfUserPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    int attackTargetId;
    int userObjectId;
    ::Disassemble(packet, "dddd", &ignored, &npcSharedIndex, &attackTargetId, &userObjectId);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }

    npc->RemoveAttackDesire(attackTargetId);
    if (User* const user = User::GetObjectW(userObjectId)) {
        npc->RemoveHateInfoByCreature(user->m_data);
    }
    npc->StopMove();
    return false;
}

bool RandomizeHatePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    ::Disassemble(packet, "dd", &ignored, &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }
    npc->RandomizeAttackDesire();
    npc->StopMove();
    return false;
}

bool ConfusePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    int ignoredThird;
    int targetSharedIndex;
    int seconds;
    ::Disassemble(packet, "ddddd", &ignored, &npcSharedIndex, &ignoredThird, &targetSharedIndex, &seconds);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CCreature* const targetObject = ResolveMappedCreature(targetSharedIndex);
    if (!npc || !targetObject) {
        return false;
    }

    npc->AddAttackDesire(targetObject->m_data, 1, MaximumDesirePriority());
    npc->SetTopDesireTime(seconds);
    return false;
}

bool RunAwayPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    int ignoredThird;
    int targetSharedIndex;
    int seconds;
    ::Disassemble(packet, "ddddd", &ignored, &npcSharedIndex, &ignoredThird, &targetSharedIndex, &seconds);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CCreature* const targetObject = ResolveMappedCreature(targetSharedIndex);
    if (!npc || !targetObject) {
        return false;
    }

    npc->AddFleeDesire(targetObject->m_data, MaximumDesirePriority());
    npc->SetTopDesireTime(seconds);
    return false;
}

bool ContributionWinnerPledgePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredContext;
    int npcSharedIndex;
    int ignoredThird;
    int pledgeSharedIndex;
    ::Disassemble(packet, "dddd", &ignoredContext, &npcSharedIndex, &ignoredThird, &pledgeSharedIndex);

    if (npcSharedIndex == -1 || pledgeSharedIndex == -1) {
        return false;
    }

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedPledgeData* const pledge = CPledgeFactory::Inst().GetData(pledgeSharedIndex);
    if (!npc || !pledge || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateAttackedByPledgeEvent(npc, pledgeSharedIndex);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x9d3)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the contribution-winner notification for NPC shared index %d, pledge shared index
        //     %d, event not dispatched.", npcSharedIndex, pledgeSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the contribution-winner notification for NPC shared index %d, pledge shared index %d; "
            "event not dispatched.",
            npcSharedIndex, pledgeSharedIndex
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GeneralEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int generalEventId;
    packet = ::Disassemble(packet, "d", &generalEventId);

    if (generalEventId == -3) {
        int period;
        packet = ::Disassemble(packet, "d", &period);
        g_LookNeighborTick = static_cast<std::uint32_t>(period);
        // Current EXE 0x0051aa50 falls through to the
        // DoorEvent payload below for event -3 as well as event 0.
    } else if (generalEventId == -2) {
        std::array<char16_t, 0xa000 / sizeof(char16_t)> dbName;
        ::Disassemble(packet, "S", 0xa000, dbName.data());
        CRespawnTimer::ResetRespawn(dbName.data());
        return false;
    } else if (generalEventId == -1) {
        CDBNPCReset* const reset = new CDBNPCReset();
        if (reset) {
            g_bDBNPCReset = true;
            reset->AddTimer(10000U, 0);
        }
        return false;
    } else if (generalEventId != 0) {
        if (generalEventId != 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid event id[%d]",
            //     L"serversocket.cpp",
            //     0xa85,
            //     generalEventId)
            // Log.Add(LOG_ERROR,
            //     "[L2Server] Ignoring unsupported general event %d. Supported IDs are -3, -2, -1, 0 and 1.",
            //     generalEventId)
            L2_LOG_ERROR(
                Log, "[L2Server] Ignoring unsupported general event %d; supported IDs are -3, -2, -1, 0 and 1.",
                generalEventId
            );
            return false;
        }

        int requesterId;
        int subcommand;
        packet = ::Disassemble(packet, "dd", &requesterId, &subcommand);

        switch (subcommand) {
            case 0:
                g_LottoGameManager.Pause = true;
                break;

            case 1:
                g_LottoGameManager.Pause = false;
                break;

            case 2: {
                int roundNumber;
                ::Disassemble(packet, "d", &roundNumber);
                g_LottoGameManager.CancelRound(requesterId, roundNumber);
                break;
            }

            case 3: {
                int roundNumber;
                ::Disassemble(packet, "d", &roundNumber);
                g_LottoGameManager.ShowRoundInfo(requesterId, roundNumber);
                break;
            }

            case 4: {
                int reward;
                ::Disassemble(packet, "d", &reward);
                g_LottoGameManager.SetPresetReward(requesterId, static_cast<std::int64_t>(reward));
                break;
            }

            case 5: {
                std::array<int, 5> numbers;
                ::Disassemble(packet, "ddddd", &numbers[0], &numbers[1], &numbers[2], &numbers[3], &numbers[4]);

                std::uint32_t numberFlag = 0;
                for (const int number : numbers) {
                    // SHL on the target masks its count to five bits.
                    const std::uint32_t bit = (static_cast<std::uint32_t>(number) - 1U) & 0x1fU;
                    numberFlag |= 1U << bit;
                }
                if (numberFlag > 0x000fffffU) {
                    g_ns.Send(
                        "cdS", 0x54, requesterId,
                        u"lotto_set_drawing_time : "
                        u"invalid number"
                    );
                }
                g_LottoGameManager.SetNumberFlag(requesterId, static_cast<std::int32_t>(numberFlag));
                break;
            }

            case 7: {
                int weekday;
                int hour;
                ::Disassemble(packet, "dd", &weekday, &hour);
                g_LottoGameManager.SetDrawingTime(requesterId, weekday, hour);
                break;
            }

            default:
                break;
        }
        return false;
    }

    std::array<char16_t, 0x800 / sizeof(char16_t)> doorName;
    int enabled;
    int inzoneId = 0;
    ::Disassemble(packet, "Sdd", 0x800, doorName.data(), &enabled, &inzoneId);
    CNpcMakerDB::Inst().DoorEvent(doorName.data(), enabled, inzoneId);
    return false;
}

bool AgitEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int agitId;
    std::array<char16_t, 2000 / sizeof(char16_t)> agitNameBuffer;
    int variant;
    int enabled;
    int databaseRespawn;
    ::Disassemble(packet, "dSddd", &agitId, 2000, agitNameBuffer.data(), &variant, &enabled, &databaseRespawn);
    if (enabled == 0) {
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"agit[%s] ended!!! ",
        //     hostAgitName.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[clan hall] Received end notification for hall %d ('%s', variant %d, database respawn %s).",
        //     agitId, WideToUtf8(hostAgitName).c_str(), variant, databaseRespawn != 0 ? "enabled" : "disabled")
        L2_LOG_INFO(
            Log, "[clan hall] Received end notification for hall %d ('%s', variant %d, database respawn %s).", agitId,
            hostAgitName.c_str(), variant, databaseRespawn != 0 ? "enabled" : "disabled"
        );
        CNpcMakerDB::Inst().AgitEvent(agitId, std::u16string(agitNameBuffer.data()), enabled, variant, databaseRespawn != 0);
    } else if (enabled == 1) {
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"agit[%s] started!!! ",
        //     hostAgitName.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[clan hall] Received start notification for hall %d ('%s', variant %d, database respawn %s).",
        //     agitId, WideToUtf8(hostAgitName).c_str(), variant, databaseRespawn != 0 ? "enabled" : "disabled")
        L2_LOG_INFO(
            Log, "[clan hall] Received start notification for hall %d ('%s', variant %d, database respawn %s).", agitId,
            hostAgitName.c_str(), variant, databaseRespawn != 0 ? "enabled" : "disabled"
        );
        CNpcMakerDB::Inst().AgitEvent(agitId, std::u16string(agitNameBuffer.data()), enabled, variant, databaseRespawn != 0);
    } else {
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Agit[%s] Undefined",
        //     L"serversocket.cpp",
        //     0xb18,
        //     hostAgitName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[clan hall] Ignoring unsupported state %d for hall %d ('%s', variant %d). Expected 0 (end) or 1 (start).",
        //     enabled, agitId, WideToUtf8(hostAgitName).c_str(), variant)
        L2_LOG_ERROR(
            Log, "[clan hall] Ignoring unsupported state %d for hall %d ('%s', variant %d); expected 0 (end) or 1 (start).",
            enabled, agitId, hostAgitName.c_str(), variant
        );
        return false;
    }
    return false;
}

bool TutorialMenuSelectEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex;
    int ask;
    int reply;
    ::Disassemble(packet, "ddd", &userSharedIndex, &ask, &reply);

    CCreature* const userObject = ResolveMappedCreature(userSharedIndex);
    CNPC* const tutorial = g_TutorialNPC;
    if (!userObject || !userObject->m_data->m_bIsUser) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;

    CNPCEvent* const event =
        CNPCEventFactory::CreateMenuSelectedEvent(tutorial, user, ask, static_cast<std::int64_t>(reply), 0, 0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xb33)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the tutorial menu-selection notification for user shared index %d (ask %d, reply
        //     %d), event not dispatched.", userSharedIndex, ask, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the tutorial menu-selection notification for user shared index %d (ask %d, reply %d); "
            "event not dispatched.",
            userSharedIndex, ask, reply
        );
        return false;
    }
    tutorial->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool TutorialMarkClicked(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex;
    std::uint8_t eventIdByte = 0;
    int questionId;
    ::Disassemble(packet, "dcd", &userSharedIndex, reinterpret_cast<char*>(&eventIdByte), &questionId);

    CCreature* const userObject = ResolveMappedCreature(userSharedIndex);
    CNPC* const tutorial = g_TutorialNPC;
    if (!userObject || !userObject->m_data->m_bIsUser) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;

    const int eventId = static_cast<int>(static_cast<std::int8_t>(eventIdByte));
    CNPCEvent* const event = CNPCEventFactory::CreateTutorialMarkClickedEvent(tutorial, user, eventId, questionId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xb51)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the tutorial mark-click notification for user shared index %d (event %d, question
        //     %d), event not dispatched.", userSharedIndex, eventId, questionId)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the tutorial mark-click notification for user shared index %d (event %d, question "
            "%d); event not dispatched.",
            userSharedIndex, eventId, questionId
        );
        return false;
    }
    tutorial->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool TutorialEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex;
    int eventId;
    ::Disassemble(packet, "dd", &userSharedIndex, &eventId);

    CCreature* const userObject = ResolveMappedCreature(userSharedIndex);
    CNPC* const tutorial = g_TutorialNPC;
    if (!userObject || !userObject->m_data->m_bIsUser) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;

    CNPCEvent* const event = CNPCEventFactory::CreateTutorialEventEvent(tutorial, user, eventId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xb6b)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the tutorial notification for user shared index %d (event %d). Event not
        //     dispatched.", userSharedIndex, eventId)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the tutorial notification for user shared index %d (event %d); event not dispatched.",
            userSharedIndex, eventId
        );
        return false;
    }
    tutorial->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::UserObjectEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int objectId;
    int sharedIndex;
    int action;
    ::Disassemble(packet, "ddd", &objectId, &sharedIndex, &action);
    if (action == 1) {
        // 0x00527d70 always allocates a fresh User. User::Set overwrites the
        // SmartID slot directly. A duplicate create is not
        // converted into reuse or cleanup of the older object.
        User* const userObject = new User();
        if (!userObject->Set(static_cast<unsigned int>(objectId), static_cast<unsigned int>(sharedIndex))) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Create failed",
            //     L"serversocket.cpp",
            //     0xaa0)
            // Log.Add(LOG_ERROR,
            //     "[player] Cannot initialize user object %u from shared index %d. Releasing the allocation.",
            //     static_cast<unsigned int>(objectId), sharedIndex)
            L2_LOG_ERROR(
                Log, "[player] Cannot initialize user object %u from shared index %d; releasing the allocation.",
                static_cast<unsigned int>(objectId), sharedIndex
            );
            L2_RELEASE(userObject, ::ObjectRefType::CreateDelete);
            return false;
        }
        CSharedCreatureData* const user = userObject->m_data;

        CNPC* const tutorial = g_TutorialNPC;
        if (!tutorial) {
            return false;
        }
        CNPCEvent* const event = CNPCEventFactory::CreateUserConnectedEvent(tutorial, user);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0xaaf)
            // Log.Add(LOG_ERROR,
            //     "[tutorial] Cannot create the user-connected notification for user object %u, shared index %d. The user
            //     object was initialized, but the tutorial event was not dispatched.", static_cast<unsigned int>(objectId),
            //     sharedIndex)
            L2_LOG_ERROR(
                Log,
                "[tutorial] Cannot create the user-connected notification for user object %u, shared index %d; the user "
                "object was initialized, but the tutorial event was not dispatched.",
                static_cast<unsigned int>(objectId), sharedIndex
            );
            return false;
        }
        tutorial->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    User* const userObject = User::GetObjectW(objectId);
    if (!userObject) {
        if (action == 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user [%x]",
            //     L"serversocket.cpp",
            //     0xacd,
            //     objectId)
            // Log.Add(LOG_WARN,
            //     "[player] Cannot process death notification for user object %u (shared index %d): user object not found.",
            //     static_cast<unsigned int>(objectId), sharedIndex)
            L2_LOG_WARN(
                Log,
                "[player] Cannot process death notification for user object %u (shared index %d): user object not found.",
                static_cast<unsigned int>(objectId), sharedIndex
            );
        } else if (action == 3) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user [%x]",
            //     L"serversocket.cpp",
            //     0xaef,
            //     objectId)
            // Log.Add(LOG_WARN,
            //     "[player] Cannot process reverse-hate clear notification for user object %u (shared index %d): user object
            //     not found.", static_cast<unsigned int>(objectId), sharedIndex)
            L2_LOG_WARN(
                Log,
                "[player] Cannot process reverse-hate clear notification for user object %u (shared index %d): user object "
                "not found.",
                static_cast<unsigned int>(objectId), sharedIndex
            );
        }
        return false;
    }

    if (action == 0) {
        L2_RELEASE(userObject, ::ObjectRefType::CreateDelete);
        userObject->Delete();
        if (g_nMaxChaseCount > 0) {
            ReverseHateManager::inst().ResetHate(static_cast<std::uint32_t>(objectId));
        }
        return false;
    }

    if (action == 2) {
        if (g_nMaxChaseCount < 1) {
            userObject->ResetReverseHate();
        } else {
            ReverseHateManager::inst().ResetHate(static_cast<std::uint32_t>(objectId));
        }

        const FVector position = userObject->m_data->GetPos();
        // The target reloads the process vector's end on each iteration.
        // Indexing keeps that behavior if a handler appends another
        // RegistCheckPCDie entry while the callback is running.
        for (std::size_t index = 0; index < gGlobalObj.m_CheckPCDieMakerList.size(); ++index) {
            NpcMakerEx* const maker = runtime::CheckedVectorAt(gGlobalObj.m_CheckPCDieMakerList, index);
            if (!maker->mTerritoryInfo.IsInTerritory(position, true)) {
                continue;
            }
            maker->OnPCDieEvent(*userObject);
        }
        return false;
    }

    if (action == 3 && g_nMaxChaseCount == 0) {
        userObject->ClearReverseHate();
    }
    return false;
}

bool ManageCastleSiege(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredContext;
    int userSharedIndex;
    int npcSharedIndex;
    int action;
    int reply;
    ::Disassemble(packet, "ddddd", &ignoredContext, &userSharedIndex, &npcSharedIndex, &action, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"ManageCastleSiegePacket",
        //     L"serversocket.cpp",
        //     0xbb2)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process castle-siege management for NPC shared index %d, user shared index %d (action %d,
        //     reply %d): %s.", npcSharedIndex, userSharedIndex, action, reply, !npc ? "NPC not found" : !user ? "user shared
        //     data unavailable" : "user failed the active-player check")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process castle-siege management for NPC shared index %d, user shared index %d (action %d, "
            "reply %d): %s.",
            npcSharedIndex, userSharedIndex, action, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user failed the active-player check"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateManageCastleSiegeEvent(npc, user, action, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xbad)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the castle-siege management event for NPC shared index %d, user shared index %d
        //     (action %d, reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, action, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the castle-siege management event for NPC shared index %d, user shared index %d "
            "(action %d, reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, action, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool PetAction(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int command;
    int targetObjectOrItemIndex;
    int ask;
    int inverseReply;
    int actionId;
    ::Disassemble(packet, "dddddd", &npcObjectId, &command, &targetObjectOrItemIndex, &ask, &inverseReply, &actionId);

    CCreature* const object = CCreature::GetObjectW(static_cast<std::uint32_t>(npcObjectId));
    CNPC* const npc = object && object->IsNPC() ? object->CastNPC() : nullptr;
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PetAction failed. cannot find npc",
        //     L"serversocket.cpp",
        //     0xbca)
        // Log.Add(LOG_WARN,
        //     "[pet] Cannot process command %d for NPC object %u: %s.",
        //     command, static_cast<unsigned int>(npcObjectId), object ? "object could not be resolved as an NPC" : "object
        //     not found")
        L2_LOG_WARN(
            Log, "[pet] Cannot process command %d for NPC object %u: %s.", command, static_cast<unsigned int>(npcObjectId),
            object ? "object could not be resolved as an NPC" : "object not found"
        );
        return false;
    }

    CSharedCreatureData& creature = *npc->m_data;
    const auto summonOption = [npc](int index) { return npc->GetOption(index); };
    const auto distance = [](const FVector& left, const FVector& right) {
        const double deltaX = left.x - right.x;
        const double deltaY = left.y - right.y;
        const double deltaZ = left.z - right.z;
        return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    };

    if (creature.m_bPetActionBlocked) {
        // Current EXE 0x00528b31 and 0x00528b3e performs two independent
        // virtual GetMaster calls. Preserve the possible intervening change.
        if (npc->GetMaster()) {
            npc->ShowSystemMessage(npc->GetMaster()->m_data, 0xc8c);
        }
        return false;
    }
    if (command == -1) {
        return false;
    }
    if (command == 1 && summonOption(3) == 0) {
        return false;
    }
    if (command == 3 && summonOption(4) == 0) {
        return false;
    }

    if (command == 5) {
        npc->RemoveDesire(DESIRE_ATTACK);
        const FVector position = creature.GetPos();
        npc->ChangePetMoveMode(1, position);
        npc->RemoveDesire(DESIRE_USE_SKILL);
        npc->RemoveDesire(DESIRE_GET_ITEM);
        npc->RemoveDesire(DESIRE_MOVE_TO_TARGET);
    }
    if (command == 6) {
        npc->RemoveDesire(DESIRE_ATTACK);
        npc->RemoveDesire(DESIRE_USE_SKILL);
        npc->RemoveDesire(DESIRE_GET_ITEM);
        npc->RemoveDesire(DESIRE_MOVE_TO_TARGET);
    }

    if (command >= 7 && command <= 9) {
        CNPCEvent* const event = CNPCEventFactory::CreatePetAIEvent(npc, command - 7);
        if (event) {
            npc->HandleEvent(event);
            L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        }
        return false;
    }
    if (command >= 10 && command <= 12) {
        CNPCEvent* const event = CNPCEventFactory::CreatePetReactionEvent(npc, command - 10);
        if (event) {
            npc->HandleEvent(event);
            L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        }
        return false;
    }

    if (summonOption(1) > 0) {
        CCreature* const masterObject = npc->GetMaster();
        if (!masterObject ||
            distance(creature.GetPos(), masterObject->m_data->GetPos()) > static_cast<double>(summonOption(1))) {
            return false;
        }
    }

    if (command != 4 && command != 1) {
        npc->RemoveDesire(DESIRE_ATTACK);
    }
    npc->RemoveDesire(DESIRE_USE_SKILL);
    npc->RemoveDesire(DESIRE_GET_ITEM);
    npc->RemoveDesire(DESIRE_MOVE_TO_TARGET);

    switch (command) {
        case -1:
        case 5:
            break;
        case 0:
            npc->TogglePetMoveMode();
            break;
        case 1: {
            CDesire topDesire;
            if (npc->GetTopDesire(topDesire) &&
                (topDesire.type != DESIRE_ATTACK || topDesire.targetId != targetObjectOrItemIndex)) {
                npc->RemoveDesire(DESIRE_ATTACK);
            }
            if (summonOption(2) == 0) {
                npc->AddAttackDesireEx(targetObjectOrItemIndex, inverseReply == 0 ? 1 : 0, ask != 0 ? 1 : 0, 1000.0F);
                break;
            }

            CNPCEvent* const event =
                CNPCEventFactory::CreateMenuSelectedEvent(npc, nullptr, ask, inverseReply == 0 ? 1 : 0, 0, actionId);
            if (!event) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Event failed",
                //     L"serversocket.cpp",
                //     0xc8d)
                // Log.Add(LOG_ERROR,
                //     "[pet] Cannot create the menu-selection event for NPC %u (command %d, ask %d, reply %d, action %d)
                //     event not dispatched.", static_cast<unsigned int>(npcObjectId), command, ask, inverseReply == 0 ? 1 :
                //     0, actionId)
                L2_LOG_ERROR(
                    Log,
                    "[pet] Cannot create the menu-selection event for NPC %u (command %d, ask %d, reply %d, action %d); "
                    "event not dispatched.",
                    static_cast<unsigned int>(npcObjectId), command, ask, inverseReply == 0 ? 1 : 0, actionId
                );
                break;
            }
            npc->HandleEvent(event);
            L2_RELEASE(event, ::ObjectRefType::CreateDelete);
            break;
        }
        case 2: {
            npc->StopMove();
            CNPCEvent* const event = CNPCEventFactory::CreatePetAIEvent(npc, 2);
            if (event) {
                npc->HandleEvent(event);
                L2_RELEASE(event, ::ObjectRefType::CreateDelete);
            }
            break;
        }
        case 3: {
            if (!creature.m_bIsAlive) {
                break;
            }
            npc->AddGetItemDesire(CItemFactory::Inst().GetData(targetObjectOrItemIndex), 1000.0F);
            break;
        }
        case 4: {
            CNPCEvent* const event =
                CNPCEventFactory::CreateMenuSelectedEvent(npc, nullptr, ask, inverseReply == 0 ? 1 : 0, 0, actionId);
            if (!event) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Event failed",
                //     L"serversocket.cpp",
                //     0xcb8)
                // Log.Add(LOG_ERROR,
                //     "[pet] Cannot create the menu-selection event for NPC %u (command %d, ask %d, reply %d, action %d)
                //     event not dispatched.", static_cast<unsigned int>(npcObjectId), command, ask, inverseReply == 0 ? 1 :
                //     0, actionId)
                L2_LOG_ERROR(
                    Log,
                    "[pet] Cannot create the menu-selection event for NPC %u (command %d, ask %d, reply %d, action %d); "
                    "event not dispatched.",
                    static_cast<unsigned int>(npcObjectId), command, ask, inverseReply == 0 ? 1 : 0, actionId
                );
                break;
            }
            npc->HandleEvent(event);
            L2_RELEASE(event, ::ObjectRefType::CreateDelete);
            break;
        }
        case 6: {
            FVector position = creature.GetPos();
            CCreature* const target = CCreature::GetObjectW(static_cast<std::uint32_t>(targetObjectOrItemIndex));
            if (target) {
                position = target->m_data->GetPos();
            }
            npc->ChangePetMoveMode(0, position);
            npc->AddMoveToTargetDesire(targetObjectOrItemIndex, 30, 0, 1000.0F);
            break;
        }
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Unkown pet action command=[%d]",
            //     L"serversocket.cpp",
            //     0xcbd,
            //     command)
            // Log.Add(LOG_ERROR,
            //     "[pet] Unsupported command %d for NPC %u. No command handler was run.",
            //     command, static_cast<unsigned int>(npcObjectId))
            L2_LOG_ERROR(
                Log, "[pet] Unsupported command %d for NPC %u; no command handler was run.", command,
                static_cast<unsigned int>(npcObjectId)
            );
            break;
    }
    return false;
}

bool RequestNPCInfo(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int builderObjectId = 0;
    int physicalState;
    ::Disassemble(packet, "ddd", &npcObjectId, &builderObjectId, &physicalState);

    CCreature* const object = CCreature::GetObjectW(static_cast<std::uint32_t>(npcObjectId));
    CNPC* const npc = object && object->IsNPC() ? object->CastNPC() : nullptr;
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestNPCInfo failed. cannot find npc",
        //     L"serversocket.cpp",
        //     0xcd1)
        // Log.Add(LOG_WARN,
        //     "[builder] Cannot report desires for NPC object %u to user %u (physical state %d): %s.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId), physicalState,
        //     object ? "object could not be resolved as an NPC" : "object not found")
        L2_LOG_WARN(
            Log, "[builder] Cannot report desires for NPC object %u to user %u (physical state %d): %s.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId), physicalState,
            object ? "object could not be resolved as an NPC" : "object not found"
        );
        return false;
    }
    npc->ReportDesireToBuilder(builderObjectId, static_cast<PhysicalState>(physicalState));
    return false;
}

bool DoorHpLevelInfo(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int talkerSharedIndex;
    int level;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &talkerSharedIndex, &level);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto talkerOwner = CCreatureFactory::Inst().GetData(talkerSharedIndex);
    CSharedCreatureData* const talker = talkerOwner;
    if (!npc || !talker || !talker->m_bIsActive || !talker->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateDoorHpLevelInformed(npc, talker, level);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xcf7)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the door HP-level reply for NPC shared index %d, user shared index %d (level %d)
        //     event not dispatched.", npcSharedIndex, talkerSharedIndex, level)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the door HP-level reply for NPC shared index %d, user shared index %d (level %d); "
            "event not dispatched.",
            npcSharedIndex, talkerSharedIndex, level
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ConrolTowerLevelInfo(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int level;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &userSharedIndex, &level);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateControlTowerLevelInformed(npc, user, level);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xd21)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the control-tower level reply for NPC shared index %d, user shared index %d (level
        //     %d), event not dispatched.", npcSharedIndex, userSharedIndex, level)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the control-tower level reply for NPC shared index %d, user shared index %d (level "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, level
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool NpcDoNothing(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CCreature* const object = CCreature::GetObjectW(static_cast<std::uint32_t>(npcObjectId));
    if (object && object->IsNPC()) {
        if (CNPC* const npc = object->CastNPC()) {
            npc->CancelTopDesireSkill();
        }
    }
    return false;
}

bool BroadcastScriptEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int senderObjectId;
    int argument1;
    int argument2;
    int argument3;
    packet = ::Disassemble(packet, "dddd", &senderObjectId, &argument1, &argument2, &argument3);

    CNPC* const sender = CNPC::GetObjectW(senderObjectId);
    if (!sender) {
        return false;
    }

    int delivered = 0;
    int targetObjectId;
    packet = ::Disassemble(packet, "d", &targetObjectId);
    while (targetObjectId >= 0) {

        CNPC* const target = CNPC::GetObjectW(targetObjectId);
        if (target) {
            const FVector senderPositionForZ = sender->m_data->GetPos();
            const FVector targetPositionForZ = target->m_data->GetPos();
            if (std::abs(static_cast<int>(senderPositionForZ.z) - static_cast<int>(targetPositionForZ.z)) < 501) {
                const FVector targetPositionForXY = target->m_data->GetPos();
                const FVector senderPositionForXY = sender->m_data->GetPos();
                const double deltaX = senderPositionForXY.x - targetPositionForXY.x;
                const double deltaY = senderPositionForXY.y - targetPositionForXY.y;
                if (deltaX * deltaX + deltaY * deltaY <= 67108864.0) {
                    gGlobalObj.SendScriptEventEx(target->m_data, argument1, argument2, argument3);
                    ++delivered;
                } else {
                    // Noise: Targets beyond the broadcast radius are routinely filtered. Logging each excluded target floods
                    // normal script activity. Log.Add(
                    //     LOG_NORMAL,
                    //     L"[%s][%d] too far broadcast script event",
                    //     L"serversocket.cpp",
                    //     0xd63)
                }
            }
        }

        packet = ::Disassemble(packet, "d", &targetObjectId);
    }

    if (delivered > 100) {
        // The target evaluates the three GetPos() expressions in compiled
        // right-to-left order: z first, then y, then x.
        const FVector senderPositionForZ = sender->m_data->GetPos();
        const FVector senderPositionForY = sender->m_data->GetPos();
        const FVector senderPositionForX = sender->m_data->GetPos();
        const std::string senderName = Utf16ToUtf8(sender->m_data->Name());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too many broadcast event occured. "
        //     L"Sender NPC [%s] count [%d] position[%d][%d][%d] "
        //     L"args(%d %d %d)",
        //     L"serversocket.cpp",
        //     0xd6e,
        //     senderName.c_str(),
        //     delivered,
        //     static_cast<int>(
        //         senderPositionForX.x),
        //     static_cast<int>(
        //         senderPositionForY.y),
        //     static_cast<int>(
        //         senderPositionForZ.z),
        //     argument1,
        //     argument2,
        //     argument3)
        // Log.Add(LOG_WARN,
        //     "[script] Broadcast from NPC %u ('%s') targeted %d NPCs, exceeding the advisory threshold of 100. Position
        //     (%d, %d, %d), arguments (%d, %d, %d).", static_cast<unsigned int>(senderObjectId),
        //     WideToUtf8(senderName).c_str(), delivered, static_cast<int>(senderPositionForX.x),
        //     static_cast<int>(senderPositionForY.y), static_cast<int>(senderPositionForZ.z), argument1, argument2,
        //     argument3)
        L2_LOG_WARN(
            Log,
            "[script] Broadcast from NPC %u ('%s') targeted %d NPCs, exceeding the advisory threshold of 100; position (%d, "
            "%d, %d), arguments (%d, %d, %d).",
            static_cast<unsigned int>(senderObjectId), senderName.c_str(), delivered,
            static_cast<int>(senderPositionForX.x), static_cast<int>(senderPositionForY.y),
            static_cast<int>(senderPositionForZ.z), argument1, argument2, argument3
        );
    }
    return false;
}

bool TeamBattleAgitEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int agitId;
    std::array<char16_t, 0x200 / sizeof(char16_t)> agitNameBuffer;
    int event;
    int databaseRespawn;
    int teamCount;
    packet = ::Disassemble(packet, "dSddd", &agitId, 0x200, agitNameBuffer.data(), &event, &databaseRespawn, &teamCount);
    std::map<int, std::pair<int, int>> teams;
    for (int index = 0; index < teamCount; ++index) {
        int slot;
        int type;
        int pledgeId;
        packet = ::Disassemble(packet, "ddd", &slot, &type, &pledgeId);
        teams.emplace(slot, std::make_pair(type, pledgeId));
    }

    CNpcMakerDB& makerDatabase = CNpcMakerDB::Inst();
    if (event == 1) {
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"agit[%s] battle royal started!!! ",
        //     hostAgitName.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[team battle] Received battle-royal start notification for hall %d ('%s', %d team entries, database respawn
        //     %s).", agitId, WideToUtf8(hostAgitName).c_str(), teamCount, databaseRespawn != 0 ? "enabled" : "disabled")
        L2_LOG_INFO(
            Log,
            "[team battle] Received battle-royal start notification for hall %d ('%s', %d team entries, database respawn "
            "%s).",
            agitId, hostAgitName.c_str(), teamCount, databaseRespawn != 0 ? "enabled" : "disabled"
        );
        makerDatabase.TB_StartBatleRoyal(agitId, agitNameBuffer.data(), teams, databaseRespawn != 0);
        makerDatabase.AgitEvent(agitId, std::u16string(agitNameBuffer.data()), 1, -1, databaseRespawn != 0);
    } else if (event == 2) {
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"agit[%s] final started!!! ",
        //     hostAgitName.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[team battle] Received final start notification for hall %d ('%s', %d team entries, database respawn %s).",
        //     agitId, WideToUtf8(hostAgitName).c_str(), teamCount, databaseRespawn != 0 ? "enabled" : "disabled")
        L2_LOG_INFO(
            Log, "[team battle] Received final start notification for hall %d ('%s', %d team entries, database respawn %s).",
            agitId, hostAgitName.c_str(), teamCount, databaseRespawn != 0 ? "enabled" : "disabled"
        );
        makerDatabase.TB_StartFinal(agitId, agitNameBuffer.data(), teams, databaseRespawn != 0);
        makerDatabase.AgitEvent(agitId, std::u16string(agitNameBuffer.data()), 2, -1, databaseRespawn != 0);
    } else if (event == 3) {
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"agit[%s] team battle ended!!! ",
        //     hostAgitName.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[team battle] Received battle end notification for hall %d ('%s', %d team entries, database respawn %s).",
        //     agitId, WideToUtf8(hostAgitName).c_str(), teamCount, databaseRespawn != 0 ? "enabled" : "disabled")
        L2_LOG_INFO(
            Log, "[team battle] Received battle end notification for hall %d ('%s', %d team entries, database respawn %s).",
            agitId, hostAgitName.c_str(), teamCount, databaseRespawn != 0 ? "enabled" : "disabled"
        );
        makerDatabase.TB_EndBattle(agitId, agitNameBuffer.data());
        makerDatabase.AgitEvent(agitId, std::u16string(agitNameBuffer.data()), 0, -1, databaseRespawn != 0);
    } else {
        // R9 still contains the name-buffer address at 0x0052f8fb.
        // Ghidra omitted that kept vararg from its CLog rendering.
        const std::string hostAgitName = Utf16ToUtf8(agitNameBuffer.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"undefiend team battle event [%s]",
        //     hostAgitName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[team battle] Ignoring unsupported event %d for hall %d ('%s'). Expected 1 (battle-royal start), 2 (final
        //     start) or 3 (end).", event, agitId, WideToUtf8(hostAgitName).c_str())
        L2_LOG_ERROR(
            Log,
            "[team battle] Ignoring unsupported event %d for hall %d ('%s'); expected 1 (battle-royal start), 2 (final "
            "start) or 3 (end).",
            event, agitId, hostAgitName.c_str()
        );
        return false;
    }
    return false;
}

bool BuyLottoTicketEvent(CSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool RegisterTeamBattleEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int operation;
    int reply;
    ::Disassemble(packet, "dddd", &npcSharedIndex, &userSharedIndex, &operation, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* event = nullptr;
    if (operation == 1) {
        event = CNPCEventFactory::CreateTB_RegisterPledgeReturned(npc, user, reply);
    } else if (operation == 2) {
        event = CNPCEventFactory::CreateTB_RegisterMemberReturned(npc, user, reply);
    }
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xde4)
        // Log.Add(LOG_ERROR,
        //     "[team battle] Cannot dispatch %s reply for NPC shared index %d, user shared index %d (operation %d, reply
        //     %d): %s.", operation == 1 ? "pledge registration" : operation == 2 ? "member registration" : "unsupported
        //     operation", npcSharedIndex, userSharedIndex, operation, reply, operation == 1 || operation == 2 ? "event
        //     creation returned no event" : "expected operation 1 or 2")
        L2_LOG_ERROR(
            Log,
            "[team battle] Cannot dispatch %s reply for NPC shared index %d, user shared index %d (operation %d, reply %d): "
            "%s.",
            operation == 1       ? "pledge registration"
                : operation == 2 ? "member registration"
                                 : "unsupported operation",
            npcSharedIndex, userSharedIndex, operation, reply,
            operation == 1 || operation == 2 ? "event creation returned no event" : "expected operation 1 or 2"
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool QueryTeamBattleNpcEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int operation;
    int reply;
    ::Disassemble(packet, "dddd", &npcSharedIndex, &userSharedIndex, &operation, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* event = nullptr;
    if (operation == 1) {
        event = CNPCEventFactory::CreateTB_GetNpcTypeInformed(npc, user, reply);
    } else if (operation == 2) {
        event = CNPCEventFactory::CreateTB_SetNpcTypeReturned(npc, user, reply);
    }
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xe13)
        // Log.Add(LOG_ERROR,
        //     "[team battle] Cannot dispatch %s reply for NPC shared index %d, user shared index %d (operation %d, reply
        //     %d): %s.", operation == 1 ? "NPC-type query" : operation == 2 ? "NPC-type update" : "unsupported operation",
        //     npcSharedIndex, userSharedIndex, operation, reply,
        //     operation == 1 || operation == 2 ? "event creation returned no event" : "expected operation 1 or 2")
        L2_LOG_ERROR(
            Log,
            "[team battle] Cannot dispatch %s reply for NPC shared index %d, user shared index %d (operation %d, reply %d): "
            "%s.",
            operation == 1       ? "NPC-type query"
                : operation == 2 ? "NPC-type update"
                                 : "unsupported operation",
            npcSharedIndex, userSharedIndex, operation, reply,
            operation == 1 || operation == 2 ? "event creation returned no event" : "expected operation 1 or 2"
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool QueryTeamBattlePledgeEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int reply;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &userSharedIndex, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTB_GetPledgeRegisterStatusInformed(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xe3b)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the team-battle pledge registration-status reply for NPC shared index %d, user
        //     shared index %d (reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the team-battle pledge registration-status reply for NPC shared index %d, user shared "
            "index %d (reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GetBattleRoyalPledgeList(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    std::array<int, 12> values;
    ::Disassemble(
        packet, "dddddddddddddd", &npcSharedIndex, &userSharedIndex, &values[0], &values[1], &values[2], &values[3],
        &values[4], &values[5], &values[6], &values[7], &values[8], &values[9], &values[10], &values[11]
    );

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTB_GetBattleRoyalPledgeListInformed(
        npc, user, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8],
        values[9], values[10], values[11]
    );
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xe75)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the battle-royal pledge-list reply for NPC shared index %d, user shared index %d.
        //     event not dispatched.", npcSharedIndex, userSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the battle-royal pledge-list reply for NPC shared index %d, user shared index %d; "
            "event not dispatched.",
            npcSharedIndex, userSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GetTerritoryInfo(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int builderObjectId;
    ::Disassemble(packet, "dd", &npcObjectId, &builderObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }

    BaseSpawnDefine* const spawnDefinition = npc->SpawnDefine();
    TerritoryInfo* const territoryInfo =
        // Current 0x0052e198 dereferences m_pSpawnDefine without checking it.
        spawnDefinition->GetTerritoryInfo();
    if (!territoryInfo) {
        return false;
    }
    CTerritory* const territory = territoryInfo->GetTerritory(0);
    if (!territory) {
        SendTerritoryBuilderMessage(builderObjectId, u"No territory");
        return false;
    }

    const FVector center = territory->GetCenterPos();
    // DateTime's default constructor follows the target's
    // GetSystemTimeAsFileTime -> TimeValue -> local DateTime path.
    const DateTime currentDateTime;
    const std::wstring currentTime = currentDateTime.ToString();
    SendTerritoryBuilderMessage(
        builderObjectId,
        FormatTerritoryBuilderText(
            u"First territory[%ls], Territory center pos(%d, %d, %d), "
            u"Current time[%ls]",
            territory->sName.data(), runtime::Win64CvttDoubleToInt32(center.x), runtime::Win64CvttDoubleToInt32(center.y),
            runtime::Win64CvttDoubleToInt32(center.z), currentTime.c_str()
        )
    );

    const TimeValue spawnTimeValue(npc->m_nSpawnTime, 0);
    const DateTime spawnDateTime = TimeValueToDateTime(spawnTimeValue);
    const FVector spawnPosition{
        static_cast<double>(npc->__x),
        static_cast<double>(npc->__y),
        static_cast<double>(npc->__z),
    };
    const auto distanceFromCenter = [&center](const FVector& position) {
        const double deltaX = center.x - position.x;
        const double deltaY = center.y - position.y;
        const double deltaZ = center.z - position.z;
        return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    };
    const bool spawnIsInXY = spawnDefinition->IsInTerritory(spawnPosition, false);
    const bool spawnIsInZ = spawnDefinition->IsInTerritory(spawnPosition, true);
    const std::wstring spawnTime = spawnDateTime.ToString();
    const double spawnDistance = distanceFromCenter(spawnPosition);
    SendTerritoryBuilderMessage(
        builderObjectId,
        FormatTerritoryBuilderText(
            u"Spawnpos(%d,%d,%d), SpawnTime[%ls], "
            u"Distfrom territory[%0.1f], IsIn(x,y)[%d], IsIn(z)[%d]",
            runtime::Win64CvttDoubleToInt32(spawnPosition.x), runtime::Win64CvttDoubleToInt32(spawnPosition.y),
            runtime::Win64CvttDoubleToInt32(spawnPosition.z), spawnTime.c_str(), spawnDistance, spawnIsInXY ? 1 : 0,
            spawnIsInZ ? 1 : 0
        )
    );

    const FVector currentPosition = npc->m_data->GetPos();
    const bool currentIsInXY = spawnDefinition->IsInTerritory(currentPosition, false);
    const bool currentIsInZ = spawnDefinition->IsInTerritory(currentPosition, true);
    const double currentDistance = distanceFromCenter(currentPosition);
    SendTerritoryBuilderMessage(
        builderObjectId,
        FormatTerritoryBuilderText(
            u"CurrentPos(%d,%d,%d), Distfrom territory[%0.1f], "
            u"IsIn(x,y)[%d], IsIn(z)[%d]",
            runtime::Win64CvttDoubleToInt32(currentPosition.x), runtime::Win64CvttDoubleToInt32(currentPosition.y),
            runtime::Win64CvttDoubleToInt32(currentPosition.z), currentDistance, currentIsInXY ? 1 : 0, currentIsInZ ? 1 : 0
        )
    );
    return false;
}

bool EnchantSkillRequest(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int userSharedIndex;
    int npcSharedIndex;
    int action = 0;
    ::Disassemble(packet, "dddd", &ignoredObjectId, &userSharedIndex, &npcSharedIndex, &action);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"EnchantSkillRequest",
        //     L"serversocket.cpp",
        //     0x503)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process skill-enchant request for NPC shared index %d, user shared index %d (action %d):
        //     %s.", npcSharedIndex, userSharedIndex, action, !npc ? "NPC not found" : !user ? "user shared data unavailable"
        //     : "user failed the active-player check")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process skill-enchant request for NPC shared index %d, user shared index %d (action %d): %s.",
            npcSharedIndex, userSharedIndex, action,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user failed the active-player check"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateEnchantSkillRequestedEvent(npc, user, action);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x4fd)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the skill-enchant request event for NPC shared index %d, user shared index %d
        //     (action %d), event not dispatched.", npcSharedIndex, userSharedIndex, action)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the skill-enchant request event for NPC shared index %d, user shared index %d (action "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, action
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool OneEnchantSkillSelect(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int userSharedIndex;
    int npcSharedIndex;
    std::uint64_t skillNameId;
    int action;
    ::Disassemble(packet, "dddQd", &ignoredObjectId, &userSharedIndex, &npcSharedIndex, &skillNameId, &action);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"OneEnchantSkillSelect",
        //     L"serversocket.cpp",
        //     0x494)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process skill-enchant selection for NPC shared index %d, user shared index %d (skill ID
        //     0x%016llX, action %d): %s.", npcSharedIndex, userSharedIndex, static_cast<unsigned long long>(skillNameId),
        //     action, !npc ? "NPC not found" : !user ? "user shared data unavailable" : "user failed the active-player
        //     check")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process skill-enchant selection for NPC shared index %d, user shared index %d (skill ID "
            "0x%016llX, action %d): %s.",
            npcSharedIndex, userSharedIndex, static_cast<unsigned long long>(skillNameId), action,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user failed the active-player check"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event =
        CNPCEventFactory::CreateShowEnchantSkillDrawerEvent(npc, user, SignedI64Bits(skillNameId), action);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x48e)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the skill-enchant selection event for NPC shared index %d, user shared index %d
        //     (skill ID 0x%016llX, action %d), event not dispatched.", npcSharedIndex, userSharedIndex, static_cast<unsigned
        //     long long>(skillNameId), action)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the skill-enchant selection event for NPC shared index %d, user shared index %d "
            "(skill ID 0x%016llX, action %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, static_cast<unsigned long long>(skillNameId), action
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SetNoblessReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user || !user->m_data->m_bIsActive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSetNoblessReturned(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x124b)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the Noblesse status reply for NPC %u, user %u (reply %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the Noblesse status reply for NPC %u, user %u (reply %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SetHeroReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user || !user->m_data->m_bIsActive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSetHeroReturned(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x126f)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the hero status reply for NPC %u, user %u (reply %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the hero status reply for NPC %u, user %u (reply %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DeletePreviousOlympiadPointReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int ask;
    int reply;
    ::Disassemble(packet, "dddd", &npcObjectId, &userObjectId, &ask, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user || !user->m_data->m_bIsActive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateDeletePreviousOlympiadPointReturned(npc, user, ask, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1295)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the previous Olympiad points reply for NPC %u, user %u (ask %d, reply %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), ask, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the previous Olympiad points reply for NPC %u, user %u (ask %d, reply %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), ask, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DeleteOlympiadTradePointReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int deletedPoint;
    int reply;
    ::Disassemble(packet, "dddd", &npcObjectId, &userObjectId, &deletedPoint, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!user) {
        return false;
    }
    if (!npc || !user->m_data->m_bIsActive || npc->m_data->m_bNotFunctionable) {
        user->m_data->SetNPCGiveItemJobDone();
        return false;
    }
    if (deletedPoint == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Olympiad Token Error!! - Deleted Point 0.",
        //     L"serversocket.cpp",
        //     0x12c2)
        // Log.Add(LOG_ERROR,
        //     "[Olympiad] Point-exchange reply reported zero deleted points for NPC %u, user %u (reply %d). Marking the NPC
        //     item job done without dispatching the exchange event.", static_cast<unsigned int>(npcObjectId),
        //     static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log,
            "[Olympiad] Point-exchange reply reported zero deleted points for NPC %u, user %u (reply %d); marking the NPC "
            "item job done without dispatching the exchange event.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        user->m_data->SetNPCGiveItemJobDone();
        return false;
    }
    user->m_data->SetNPCGiveItemJobDone();

    CNPCEvent* const event = CNPCEventFactory::CreateDeleteOlympiadTradePointReturned(npc, user, deletedPoint, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x12d4)
        // Log.Add(LOG_ERROR,
        //     "[Olympiad] Cannot create the point-exchange reply for NPC %u, user %u (deleted points %d, reply %d). The NPC
        //     item job was marked done, but the AI event was not dispatched.", static_cast<unsigned int>(npcObjectId),
        //     static_cast<unsigned int>(userObjectId), deletedPoint, reply)
        L2_LOG_ERROR(
            Log,
            "[Olympiad] Cannot create the point-exchange reply for NPC %u, user %u (deleted points %d, reply %d); the NPC "
            "item job was marked done, but the AI event was not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), deletedPoint, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CreateAcademy(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int userSharedIndex;
    int npcSharedIndex;
    int reply;
    std::array<char16_t, 0x32 / sizeof(char16_t)> academyName{};
    ::Disassemble(packet, "ddddS", &ignoredObjectId, &userSharedIndex, &npcSharedIndex, &reply, 0x32, academyName.data());

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"CreateAcademyPacket",
        //     L"serversocket.cpp",
        //     0x78e)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the academy creation reply for NPC shared index %d, user shared index %d (name
        //     '%s', reply %d): %s.", npcSharedIndex, userSharedIndex, Utf16ToUtf8(academyName.data()).c_str(), reply, !npc ?
        //     "NPC not found" : !user ? "user shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the academy creation reply for NPC shared index %d, user shared index %d (name '%s', "
            "reply %d): %s.",
            npcSharedIndex, userSharedIndex, Utf16ToUtf8(academyName.data()).c_str(), reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCreateAcademyEvent(npc, user, academyName.data(), reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x789)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the academy creation reply event for NPC shared index %d, user shared index %d (name
        //     '%s', reply %d). Event not dispatched.", npcSharedIndex, userSharedIndex,
        //     Utf16ToUtf8(academyName.data()).c_str(), reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the academy creation reply event for NPC shared index %d, user shared index %d (name "
            "'%s', reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, Utf16ToUtf8(academyName.data()).c_str(), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool MiniGameAgitEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint8_t rawSubtype;
    int ignoredObjectId;
    int reply;
    int userSharedIndex;
    int npcSharedIndex;
    ::Disassemble(
        packet, "cdddd", reinterpret_cast<char*>(&rawSubtype), &ignoredObjectId, &reply, &userSharedIndex, &npcSharedIndex
    );

    const std::int8_t subtype = static_cast<std::int8_t>(rawSubtype);
    if (subtype >= 5) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown event id(%d)",
        //     L"serversocket.cpp",
        //     0x1352,
        //     static_cast<int>(subtype))
        // Log.Add(LOG_ERROR,
        //     "[clan hall] Ignoring unsupported mini-game subtype %d for NPC shared index %d, user shared index %d (reply
        //     %d), defined subtypes are 0..4.", static_cast<int>(subtype), npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[clan hall] Ignoring unsupported mini-game subtype %d for NPC shared index %d, user shared index %d (reply "
            "%d); defined subtypes are 0..4.",
            static_cast<int>(subtype), npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }

    CCreature* const userCreature = CCreatureFactory::Inst().GetObjectW(userSharedIndex);
    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!userCreature || !npc || !userCreature->m_data->m_bIsActive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event =
        CNPCEventFactory::CreateMiniGameAgitEvent(static_cast<char>(subtype), npc, userCreature->CastUser(), reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x134e)
        // Log.Add(LOG_ERROR,
        //     "[clan hall] Cannot create the mini-game reply for NPC shared index %d, user shared index %d (subtype %d,
        //     reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, static_cast<int>(subtype), reply)
        L2_LOG_ERROR(
            Log,
            "[clan hall] Cannot create the mini-game reply for NPC shared index %d, user shared index %d (subtype %d, reply "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, static_cast<int>(subtype), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CreateSubPledge(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int userSharedIndex;
    int npcSharedIndex;
    int reply;
    int pledgeType;
    int pledgeMasterDbId = 0;
    std::array<char16_t, 0x32 / sizeof(char16_t)> subPledgeName{};
    ::Disassemble(
        packet, "ddddddS", &ignoredObjectId, &userSharedIndex, &npcSharedIndex, &reply, &pledgeType, &pledgeMasterDbId, 0x32,
        subPledgeName.data()
    );

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"CreateSubPledgePacket",
        //     L"serversocket.cpp",
        //     0x7b3)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the clan subdivision creation reply for NPC shared index %d, user shared index %d
        //     (name '%s', type %d, leader DB ID %u, reply %d): %s.", npcSharedIndex, userSharedIndex,
        //     Utf16ToUtf8(subPledgeName.data()).c_str(), pledgeType, static_cast<unsigned int>(pledgeMasterDbId), reply,
        //     !npc ? "NPC not found" : !user ? "user shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the clan subdivision creation reply for NPC shared index %d, user shared index %d "
            "(name '%s', type %d, leader DB ID %u, reply %d): %s.",
            npcSharedIndex, userSharedIndex, Utf16ToUtf8(subPledgeName.data()).c_str(), pledgeType,
            static_cast<unsigned int>(pledgeMasterDbId), reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event =
        CNPCEventFactory::CreateCreateSubPledgeEvent(npc, user, reply, pledgeType, pledgeMasterDbId, subPledgeName.data());
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x7ae)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan subdivision creation reply event for NPC shared index %d, user shared index
        //     %d (name '%s', type %d, leader DB ID %u, reply %d). Event not dispatched.", npcSharedIndex, userSharedIndex,
        //     Utf16ToUtf8(subPledgeName.data()).c_str(), pledgeType, static_cast<unsigned int>(pledgeMasterDbId), reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan subdivision creation reply event for NPC shared index %d, user shared index "
            "%d (name '%s', type %d, leader DB ID %u, reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, Utf16ToUtf8(subPledgeName.data()).c_str(), pledgeType,
            static_cast<unsigned int>(pledgeMasterDbId), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CheckCursedUserReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user || !user->m_data->m_bIsActive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCheckCursedUserReturned(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1375)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the cursed-user check reply for NPC %u, user %u (reply %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the cursed-user check reply for NPC %u, user %u (reply %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool UpdateSubPledgeMaster(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int userSharedIndex;
    int pledgeType;
    int npcSharedIndex;
    int reply;
    ::Disassemble(packet, "ddddd", &ignoredObjectId, &userSharedIndex, &pledgeType, &npcSharedIndex, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"UpdateSubPledgeMasterPacket",
        //     L"serversocket.cpp",
        //     0x7d7)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the clan subdivision leader update reply for NPC shared index %d, user shared index
        //     %d (type %d, reply %d): %s.", npcSharedIndex, userSharedIndex, pledgeType, reply, !npc ? "NPC not found" :
        //     !user ? "user shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the clan subdivision leader update reply for NPC shared index %d, user shared index "
            "%d (type %d, reply %d): %s.",
            npcSharedIndex, userSharedIndex, pledgeType, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateUpdateSubPledgeMasterEvent(npc, user, reply, pledgeType);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x7d2)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan subdivision leader update reply event for NPC shared index %d, user shared
        //     index %d (type %d, reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, pledgeType, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan subdivision leader update reply event for NPC shared index %d, user shared "
            "index %d (type %d, reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, pledgeType, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool MPCCTeleported(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateMPCCTeleportedEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1392)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the command-channel teleport reply for NPC %u, user %u (reply %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the command-channel teleport reply for NPC %u, user %u (reply %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool PledgeMasterTransfer(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId = 0;
    int ignoredObjectId = 0;
    int npcObjectId = 0;
    std::array<char16_t, 0x32 / sizeof(char16_t)> newMasterName{};
    int reply = 0;
    ::Disassemble(packet, "dddSd", &userObjectId, &ignoredObjectId, &npcObjectId, 0x32, newMasterName.data(), &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreatePledgeMasterTransferEvent(npc, user, newMasterName.data(), reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x13bb)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan-leader transfer reply for NPC %u, user %u (new leader '%s', reply %d)
        //     event not dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId),
        //     Utf16ToUtf8(newMasterName.data()).c_str(), reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan-leader transfer reply for NPC %u, user %u (new leader '%s', reply %d); event "
            "not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId),
            Utf16ToUtf8(newMasterName.data()).c_str(), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool PledgeMasterTransferCancel(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId = 0;
    int ignoredObjectId = 0;
    int npcObjectId = 0;
    int reply = 0;
    // 0x0051bf2c initializes all four destinations and ignores
    // Disassemble's return value.
    ::Disassemble(packet, "dddd", &userObjectId, &ignoredObjectId, &npcObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreatePledgeMasterTransferCancelEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x13e0)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan-leader transfer cancellation reply for NPC %u, user %u (reply %d). Event
        //     not dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan-leader transfer cancellation reply for NPC %u, user %u (reply %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool TBCheckMemberRegisterStatus(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int reply;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTB_CheckMemberRegisterStatusReturned(npc, user, reply);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RenameSubPledge(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId = 0;
    int ignoredObjectId = 0;
    int npcObjectId = 0;
    int pledgeType;
    std::array<char16_t, 0x32 / sizeof(char16_t)> subPledgeName{};
    int reply = 0;
    ::Disassemble(
        packet, "ddddSd", &userObjectId, &ignoredObjectId, &npcObjectId, &pledgeType, 0x32, subPledgeName.data(), &reply
    );

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateRenameSubPledgeEvent(
        npc, user, static_cast<PledgeType>(pledgeType), subPledgeName.data(), reply
    );
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool AuctionAgitGetCostInfoReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int packedUserSharedIndex;
    std::uint64_t costBits;
    std::int64_t expirationEpoch;
    ::Disassemble(packet, "ddQd", &npcObjectId, &packedUserSharedIndex, &costBits, &expirationEpoch);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    const int userSharedIndex = static_cast<int>(static_cast<std::uint32_t>(packedUserSharedIndex) & 0x000fffffU);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    std::int64_t cost = SignedI64Bits(costBits);
    if (cost < 0) {
        cost = 0;
    }
    std::tm* const expiration = runtime::crt::LocalTime64(expirationEpoch);
    char16_t* const expirationText = AllocTmpString(0x14);
    if (!expirationText) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't allocate buffer for fstring",
        //     L"serversocket.cpp",
        //     0x104d)
        // Log.Add(LOG_ERROR,
        //     "[auction] Cannot allocate the temporary expiration-date string for NPC %u, user shared index %d (cost %lld)
        //     clan-hall price reply not dispatched.", static_cast<unsigned int>(npcObjectId), userSharedIndex,
        //     static_cast<long long>(cost))
        L2_LOG_ERROR(
            Log,
            "[auction] Cannot allocate the temporary expiration-date string for NPC %u, user shared index %d (cost %lld); "
            "clan-hall price reply not dispatched.",
            static_cast<unsigned int>(npcObjectId), userSharedIndex, static_cast<long long>(cost)
        );
        return false;
    }
    FormatServerSocketTimestamp(expiration, expirationText);
    CNPCEvent* const event = CNPCEventFactory::CreateAuctionAgitGetCostInfoReturned(npc, user, cost, expirationText);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x105c)
        // Log.Add(LOG_ERROR,
        //     "[auction] Cannot create the clan-hall price reply for NPC %u, user shared index %d (cost %lld). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), userSharedIndex, static_cast<long long>(cost))
        L2_LOG_ERROR(
            Log,
            "[auction] Cannot create the clan-hall price reply for NPC %u, user shared index %d (cost %lld); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), userSharedIndex, static_cast<long long>(cost)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool AbnormalStatusChangedPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int skillId;
    int skillLevel;
    int skillSubLevel;
    int spellerObjectId;
    ::Disassemble(packet, "ddddd", &npcObjectId, &skillId, &skillLevel, &skillSubLevel, &spellerObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->AbnormalStatusChanged(skillId, skillLevel, skillSubLevel, spellerObjectId);
    }
    return false;
}

bool EscapeFromBetraying(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->m_bBetrayOwnMaster = false;
    }
    return false;
}

bool TeleportToUserRes(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int creatureObjectId = 0;
    int argument = 0;
    int success = 0;
    // 0x0051c4fc initializes all four destinations and ignores
    // Disassemble's return value.
    ::Disassemble(packet, "dddd", &npcObjectId, &creatureObjectId, &argument, &success);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->OnTeleportToUserRes(creatureObjectId, argument, success);
    }
    return false;
}

bool TrapCreatureStepInOut(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int creatureObjectId = 0;
    int stepIn = 0;
    int creatureCount = 0;
    // 0x0051c5f0 initializes all four destinations and ignores
    // Disassemble's return value.
    ::Disassemble(packet, "dddd", &npcObjectId, &creatureObjectId, &stepIn, &creatureCount);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    if (stepIn == 1) {
        npc->OnTrapStepIn(creatureObjectId, creatureCount);
    } else if (stepIn == 0) {
        npc->OnTrapStepOut(creatureObjectId, creatureCount);
    }
    return false;
}

bool TrapInteracted(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int argument = 0;
    int interaction = 0;
    // 0x0051c6f8 initializes all three destinations and ignores
    // Disassemble's return value.
    ::Disassemble(packet, "ddd", &npcObjectId, &argument, &interaction);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    switch (interaction) {
        case 0:
            npc->OnTrapDefused(argument);
            break;
        case 1:
            npc->OnTrapActivated(argument);
            break;
        case 2:
            npc->OnTrapDetected(argument);
            break;
        default:
            break;
    }
    return false;
}

bool FieldCycleChanged(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int fieldId = -1;
    int step = -1;
    ::Disassemble(packet, "dd", &fieldId, &step);
    CNpcMakerDB::Inst().FieldCycleEvent(fieldId, step);
    return false;
}

bool TransterHate(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int ignored;
    int sourceUserObjectId;
    int destinationUserObjectId;
    ::Disassemble(packet, "dddd", &npcSharedIndex, &ignored, &sourceUserObjectId, &destinationUserObjectId);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    return npc ? npc->OnPacketTransferHate(ignored, sourceUserObjectId, destinationUserObjectId) : false;
}

bool OneEnchantSkillSelectDetail(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int userSharedIndex;
    int npcSharedIndex;
    std::uint64_t skillNameIdBits;
    int action;
    ::Disassemble(packet, "dddQd", &ignored, &userSharedIndex, &npcSharedIndex, &skillNameIdBits, &action);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"OneEnchantSkillSelectDetail",
        //     L"serversocket.cpp",
        //     0x4b9)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the skill-enchant detail request for NPC shared index %d, user shared index %d
        //     (skill ID 0x%016llX, action %d): %s.", npcSharedIndex, userSharedIndex, static_cast<unsigned long
        //     long>(skillNameIdBits), action, !npc ? "NPC not found" : !user ? "user shared data unavailable" : "user was
        //     inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the skill-enchant detail request for NPC shared index %d, user shared index %d "
            "(skill ID 0x%016llX, action %d): %s.",
            npcSharedIndex, userSharedIndex, static_cast<unsigned long long>(skillNameIdBits), action,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event =
        CNPCEventFactory::CreateOneEnchantSkillSelectedEvent(npc, user, SignedI64Bits(skillNameIdBits), action);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x4b3)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the skill-enchant detail request event for NPC shared index %d, user shared index %d
        //     (skill ID 0x%016llX, action %d), event not dispatched.", npcSharedIndex, userSharedIndex, static_cast<unsigned
        //     long long>(skillNameIdBits), action)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the skill-enchant detail request event for NPC shared index %d, user shared index %d "
            "(skill ID 0x%016llX, action %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, static_cast<unsigned long long>(skillNameIdBits), action
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool UpgradeSubPledgeMemberCount(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int ignored;
    int userSharedIndex;
    int argument;
    int reply;
    ::Disassemble(packet, "ddddd", &npcSharedIndex, &ignored, &userSharedIndex, &argument, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in "
        //     L"UpgradeSubPledgeMemberCount",
        //     L"serversocket.cpp",
        //     0x7fb)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the clan subdivision capacity reply for NPC shared index %d, user shared index %d
        //     (argument %d, reply %d): %s.", npcSharedIndex, userSharedIndex, argument, reply, !npc ? "NPC not found" :
        //     !user ? "user shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the clan subdivision capacity reply for NPC shared index %d, user shared index %d "
            "(argument %d, reply %d): %s.",
            npcSharedIndex, userSharedIndex, argument, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateUpgradeSubPledgeMemberCountEvent(npc, user, argument, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x7f6)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan subdivision capacity reply event for NPC shared index %d, user shared index
        //     %d (argument %d, reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, argument, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan subdivision capacity reply event for NPC shared index %d, user shared index "
            "%d (argument %d, reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, argument, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool OlympiadFieldStepChanged(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int fieldIndex = -1;
    int packetStep = 0;
    int state = -1;
    ::Disassemble(packet, "ddd", &fieldIndex, &packetStep, &state);
    CNpcMakerDB::Inst().OlympiadFieldEvent(fieldIndex, packetStep, state);
    return false;
}

bool FireCannon(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    ::Disassemble(packet, "d", &npcObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateFireEvent(npc);
    if (!event) {
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool GetSubJobList(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int ask;
    int integer8;
    std::array<int, 8> integers;
    ::Disassemble(
        packet, "dddddddddddd", &npcSharedIndex, &userSharedIndex, &ask, &integer8, &integers[0], &integers[1], &integers[2],
        &integers[3], &integers[4], &integers[5], &integers[6], &integers[7]
    );

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSubJobListInformed(
        npc, user, ask, integer8, integers[0], integers[1], integers[2], integers[3], integers[4], integers[5], integers[6],
        integers[7]
    );
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xeab)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the subclass-list reply for NPC shared index %d, user shared index %d (ask %d)
        //     event not dispatched.", npcSharedIndex, userSharedIndex, ask)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the subclass-list reply for NPC shared index %d, user shared index %d (ask %d); event "
            "not dispatched.",
            npcSharedIndex, userSharedIndex, ask
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CreateSubJob(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int reply;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &userSharedIndex, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSubJobCreated(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xf53)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the subclass creation reply for NPC shared index %d, user shared index %d (reply
        //     %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the subclass creation reply for NPC shared index %d, user shared index %d (reply %d); "
            "event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ChangeSubJob(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int reply;
    int level;
    ::Disassemble(packet, "dddd", &npcSharedIndex, &userSharedIndex, &reply, &level);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSubJobChanged(npc, user, reply, level);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xfa6)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the subclass change reply for NPC shared index %d, user shared index %d (reply %d,
        //     level %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply, level)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the subclass change reply for NPC shared index %d, user shared index %d (reply %d, "
            "level %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply, level
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RenewSubJob(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int integer0;
    int reply;
    ::Disassemble(packet, "dddd", &npcSharedIndex, &userSharedIndex, &integer0, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSubJobRenewed(npc, user, reply, integer0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xfd1)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the subclass renewal reply for NPC shared index %d, user shared index %d (argument
        //     %d, reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, integer0, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the subclass renewal reply for NPC shared index %d, user shared index %d (argument "
            "%d, reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, integer0, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SetAgitDecoReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int reply;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &userSharedIndex, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSetAgitDecoReturned(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xffb)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan-hall decoration update reply for NPC shared index %d, user shared index %d
        //     (reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan-hall decoration update reply for NPC shared index %d, user shared index %d "
            "(reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ResetAgitDecoReturned(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int reply;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &userSharedIndex, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    const auto userOwner = CCreatureFactory::Inst().GetData(userSharedIndex);
    CSharedCreatureData* const user = userOwner;
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateResetAgitDecoReturned(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1026)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan-hall decoration reset reply for NPC shared index %d, user shared index %d
        //     (reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan-hall decoration reset reply for NPC shared index %d, user shared index %d "
            "(reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool NpcAddTimer(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int npcSharedIndex = 0;
    int timerId = 0;
    int milliseconds = 0;
    ::Disassemble(packet, "dddd", &npcObjectId, &npcSharedIndex, &timerId, &milliseconds);

    if (CCreatureFactory::Inst().GetIdByIndex(npcSharedIndex) != npcObjectId) {
        return false;
    }
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->AddTimerEx(timerId, milliseconds);
    }
    return false;
}

bool OnSSQSysEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ask;
    int reply;
    ::Disassemble(packet, "dd", &ask, &reply);

    ListenerListType listeners;
    g_EventListener.GetListenerList(listeners, 1);

    for (const int npcSmartId : listeners) {
        CNPC* const npc = CNPC::GetObjectW(npcSmartId);
        if (!npc) {
            continue;
        }
        CNPCEvent* const event = CNPCEventFactory::CreateSSQSystemEvent(npc, ask, reply);
        if (!event) {
            continue;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    }
    return false;
}

bool CreateNpcToWinner(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int targetSharedIndex;
    int npcObjectId;
    int ignored;
    ::Disassemble(packet, "ddd", &targetSharedIndex, &npcObjectId, &ignored);

    CCreature* const targetObject = ResolveMappedCreature(targetSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!targetObject || !npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateNpcToWinner(npc, targetObject->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1baf)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the winner NPC notification for NPC %u, target shared index %d. Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), targetSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the winner NPC notification for NPC %u, target shared index %d; event not dispatched.",
            static_cast<unsigned int>(npcObjectId), targetSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool NpcCreateCow(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int targetSharedIndex;
    int npcObjectId;
    ::Disassemble(packet, "dd", &targetSharedIndex, &npcObjectId);

    CCreature* const targetObject = ResolveMappedCreature(targetSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!targetObject || !npc) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateCow(npc, targetObject->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1b8b)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the cow creation notification for NPC %u, target shared index %d. Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), targetSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the cow creation notification for NPC %u, target shared index %d; event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), targetSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::DebugAIEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int type = -1;
    packet = ::Disassemble(packet, "d", &type);

    if (type == 0) {
        int npcObjectId = 0;
        int reply = 0;
        int userObjectId = 0;
        packet = ::Disassemble(packet, "ddd", &npcObjectId, &reply, &userObjectId);
        CNPC* const npc = CNPC::GetObjectW(npcObjectId);
        User* const user = User::GetObjectW(userObjectId);
        if (!npc || !user) {
            return false;
        }
        CNPCEvent* const event = CNPCEventFactory::CreateDebugAIEvent(*npc, reply, *user);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0x1a4c)
            // Log.Add(LOG_ERROR,
            //     "[builder] Cannot create the AI debug event for NPC %u, user %u (reply %d). Event not dispatched.",
            //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
            L2_LOG_ERROR(
                Log, "[builder] Cannot create the AI debug event for NPC %u, user %u (reply %d); event not dispatched.",
                static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
            );
            return false;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    if (type == 1) {
        std::array<char16_t, 0xa000 / sizeof(char16_t)> makerName;
        int reply = 0;
        int userObjectId = 0;
        packet = ::Disassemble(packet, "Sdd", 0xa000, makerName.data(), &reply, &userObjectId);
        NpcMakerEx* const maker = gGlobalObj.GetNpcMaker(makerName.data());
        User* const user = User::GetObjectW(userObjectId);
        if (!maker || !user) {
            return false;
        }
        MakerEvent* const event = MakerEventFactory::CreateDebugMakerEvent(*maker, reply, *user);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0x1a61)
            // Log.Add(LOG_ERROR,
            //     "[builder] Cannot dispatch the AI debug event for user %u (reply %d): event creation failed for maker
            //     '%s'.", static_cast<unsigned int>(userObjectId), reply, Utf16ToUtf8(makerName.data()).c_str())
            L2_LOG_ERROR(
                Log,
                "[builder] Cannot dispatch the AI debug event for user %u (reply %d): event creation failed for maker '%s'.",
                static_cast<unsigned int>(userObjectId), reply, Utf16ToUtf8(makerName.data()).c_str()
            );
            return false;
        }
        maker->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"debug_ai type [%d] failed",
    //     type)
    // Log.Add(LOG_WARN,
    //     "[builder] Unsupported AI debug target type %d. Expected 0 (NPC) or 1 (maker). Request ignored.",
    //     type)
    L2_LOG_WARN(Log, "[builder] Unsupported AI debug target type %d; expected 0 (NPC) or 1 (maker). Request ignored.", type);
    return false;
}

bool CServerSocket::IsToggleSkillOnOff(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int targetObjectId = 0;
    int argument = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &targetObjectId, &argument);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    CCreature* const targetObject = CCreature::GetObjectW(static_cast<std::uint32_t>(targetObjectId));
    if (!npc || !targetObject) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateIsToggleSkillOnOffEvent(npc, targetObject->m_data, argument);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1a29)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the toggle-skill query reply for NPC %u, target %u (argument %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(targetObjectId), argument)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the toggle-skill query reply for NPC %u, target %u (argument %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(targetObjectId), argument
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::RequestNPCTimerInfo(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSmartId = 0;
    int builderObjectId = 0;
    ::Disassemble(packet, "dd", &npcSmartId, &builderObjectId);

    CCreature* const object = CCreature::GetObjectW(static_cast<std::uint32_t>(npcSmartId));
    CNPC* const npc = object && object->IsNPC() ? object->CastNPC() : nullptr;
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestNPCTimerInfo failed. cannot find npc",
        //     L"serversocket.cpp",
        //     0x1a7a)
        // Log.Add(LOG_WARN,
        //     "[builder] Cannot report timers for NPC object %u to user %u: %s.",
        //     static_cast<unsigned int>(npcSmartId), static_cast<unsigned int>(builderObjectId),
        //     object ? "object could not be resolved as an NPC" : "object not found")
        L2_LOG_WARN(
            Log, "[builder] Cannot report timers for NPC object %u to user %u: %s.", static_cast<unsigned int>(npcSmartId),
            static_cast<unsigned int>(builderObjectId),
            object ? "object could not be resolved as an NPC" : "object not found"
        );
        return false;
    }

    npc->ReportTimerInfoToBuilder(builderObjectId);
    return false;
}

bool CServerSocket::BuilderCmd(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int builderObjectId = 0;
    std::array<char16_t, 2000 / sizeof(char16_t)> commandBuffer{};
    std::array<char16_t, 2000 / sizeof(char16_t)> argumentsBuffer{};
    packet = ::Disassemble(
        packet, "ddSS", &npcObjectId, &builderObjectId, 2000, commandBuffer.data(), 2000, argumentsBuffer.data()
    );
    NpcPosEventName command(commandBuffer.data());
    NpcPosEventName arguments(argumentsBuffer.data());

    const auto sendBuilderText = [&](const std::string& text) {
        const std::u16string wide = Utf8ToUtf16(text);
        g_ns.Send("cdS", 0x54, builderObjectId, wide.c_str());
    };
    const auto commandIs = [&command](std::u16string_view expected) { return EqualLegacyWide(command, expected); };

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        if (commandIs(u"debug_maker_api1")) {
            NpcPosEventNameList tokens;
            L2Util::GetTokenizedStrings(arguments.data(), tokens);
            if (tokens.size() != 2) {
                return false;
            }
            NpcMakerEx* maker =
                CNpcMakerDB::Inst().FindNpcMakerEx(std::u16string(std::u16string_view(runtime::CheckedVectorAt(tokens, 0))));
            if (!maker) {
                return false;
            }
            const int state = ParseLegacyOnOff(runtime::CheckedVectorAt(tokens, 1));
            if (state >= 0) {
                maker->m_bNpcMakerExDebugOn = state != 0;
            }
            return false;
        }
        if (commandIs(u"npcserver_stat")) {
            // CBuildDate and "$Revision: #272 $" are static data in
            // the shipping executable.
            sendBuilderText(
                "Npc Server Build date : May 30 2018 10:06:06 "
                "Npc Protocol Version : 272"
            );
        }
        return false;
    }

    CSharedCreatureData* shared = npc->m_data;

    if (commandIs(u"return_start_pos")) {
        npc->ReturnStartPoint();
        return false;
    }

    if (commandIs(u"tel_random_pos")) {
        User* const builder = User::GetObjectW(builderObjectId);
        int minimumDistance = 0;
        int maximumDistance = 0;
        if (!builder) {
            return false;
        }
        ::Disassemble(packet, "dd", &minimumDistance, &maximumDistance);

        Position* const destination = gGlobalObj.GetRandomPosInCreature(shared, minimumDistance, maximumDistance);
        const FVector targetPosition{
            static_cast<double>(destination->m_X),
            static_cast<double>(destination->m_Y),
            static_cast<double>(destination->m_Z),
        };
        NPCServer::SendInstantTeleport(
            *builder->m_data, targetPosition, builder->m_data->GetDirByInt(), 0, 0,
            TeleportSourceType::TeleportRandomPos, nullptr, 0
        );
        return false;
    }

    if (commandIs(u"fix_move_type")) {
        int enabled = 0;
        ::Disassemble(packet, "d", &enabled);
        npc->FixMoveType(enabled != 0);
        return false;
    }

    if (commandIs(u"attack_me")) {
        User* const builder = User::GetObjectW(builderObjectId);
        int priority = 0;
        if (!builder) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ComeToMe failed. cannot find user",
            //     L"serversocket.cpp",
            //     0x1aaf)
            // Log.Add(LOG_WARN,
            //     "[builder] Cannot run 'attack_me' on NPC %u for user %u: user object not found.",
            //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId))
            L2_LOG_WARN(
                Log, "[builder] Cannot run 'attack_me' on NPC %u for user %u: user object not found.",
                static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId)
            );
            return false;
        }
        npc->m_nBuilerIdForDebug = shared->m_EvolutionId == 0x309 ? builderObjectId : 0;
        ::Disassemble(packet, "d", &priority);
        npc->AddAttackDesire(builder->m_data, 1, static_cast<float>(priority));
        return false;
    }

    if (commandIs(u"quest_npc_log npc_get")) {
        User* const builder = User::GetObjectW(builderObjectId);
        int ignored = 0;
        int questId = 0;
        int logType = 0;
        if (!builder) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] quest_npc_log failed. cannot find user",
            //     L"serversocket.cpp",
            //     0x1ac6)
            // Log.Add(LOG_WARN,
            //     "[builder] Cannot run 'quest_npc_log npc_get' on NPC %u for user %u: user object not found.",
            //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId))
            L2_LOG_WARN(
                Log, "[builder] Cannot run 'quest_npc_log npc_get' on NPC %u for user %u: user object not found.",
                static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId)
            );
            return false;
        }
        ::Disassemble(packet, "ddd", &ignored, &questId, &logType);
        npc->GetNPCLog(builder->m_data, questId, logType);
        return false;
    }

    if (commandIs(u"quest_npc_log npc_inc")) {
        User* const builder = User::GetObjectW(builderObjectId);
        int ignored = 0;
        int questId = 0;
        int logType = 0;
        if (!builder) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] quest_npc_log failed. cannot find user",
            //     L"serversocket.cpp",
            //     0x1ad8)
            // Log.Add(LOG_WARN,
            //     "[builder] Cannot run 'quest_npc_log npc_inc' on NPC %u for user %u: user object not found.",
            //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId))
            L2_LOG_WARN(
                Log, "[builder] Cannot run 'quest_npc_log npc_inc' on NPC %u for user %u: user object not found.",
                static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(builderObjectId)
            );
            return false;
        }
        ::Disassemble(packet, "ddd", &ignored, &questId, &logType);
        npc->IncreaseNPCLog(builder->m_data, questId, logType, 10);
        return false;
    }

    if (commandIs(u"debug_maker_api")) {
        NpcMakerEx* const maker = npc->GetMyMaker();
        if (!maker) {
            return false;
        }
        const int state = ParseLegacyOnOff(arguments);
        if (state >= 0) {
            maker->m_bNpcMakerExDebugOn = state != 0;
        }
        return false;
    }

    if (commandIs(u"show_ai_class")) {
        npc->ReportAIClassHierarchy(builderObjectId);
        return false;
    }

    if (commandIs(u"show_maker_class")) {
        if (NpcMakerEx* const maker = npc->GetMyMaker()) {
            maker->ReportMakerClassHierarchy(builderObjectId);
        }
        return false;
    }

    if (commandIs(u"desire_clear")) {
        npc->RemoveAllDesire();
        return false;
    }

    if (commandIs(u"desire_freeze")) {
        NpcPosEventNameList tokens;
        L2Util::GetTokenizedStrings(arguments.data(), tokens);
        if (tokens.size() != 1) {
            return false;
        }
        const int state = ParseLegacyOnOff(runtime::CheckedVectorAt(tokens, 0));
        if (state >= 0) {
            npc->m_bDesireFreeze = state != 0;
        }
        return false;
    }

    return false;
}

bool FieldCycleStepExpired(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int cycleId = -1;
    int eventId = -1;
    int step = -1;
    ::Disassemble(packet, "ddd", &cycleId, &eventId, &step);
    CNpcMakerDB::Inst().FieldCycleStepExpired(cycleId, eventId, step);
    return false;
}

bool GetEventScheduledTime(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int eventId = 0;
    int count = 0;
    packet = ::Disassemble(packet, "dd", &eventId, &count);
    for (int index = 0; index < count; ++index) {
        int value = 0;
        packet = ::Disassemble(packet, "d", &value);
        gGlobalObj.m_EventScheduledTimeMap[eventId].push_back(value);
    }
    return false;
}

bool LoadNPCAI(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 0x800 / sizeof(char16_t)> fileName16;
    ::Disassemble(packet, "S", 0x800, fileName16.data());

    if (!CNPCAIManager::LoadNPCAI(fileName16.data())) {
        const std::string fileName = Utf16ToUtf8(fileName16.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] LoadNPCAI() failed [%s]",
        //     L"serversocket.cpp",
        //     0x1bef,
        //     fileName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[AI load] L2Server-requested load of '%s' failed. See the loader diagnostics.",
        //     WideToUtf8(fileName).c_str())
        L2_LOG_ERROR(
            Log, "[AI load] L2Server-requested load of '%s' failed; see the loader diagnostics.",
            fileName.c_str()
        );
    } else {
        const std::string fileName = Utf16ToUtf8(fileName16.data());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[%s][%d] LoadNPCAI() succeeded [%s]",
        //     L"serversocket.cpp",
        //     0x1beb,
        //     fileName.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[AI load] L2Server-requested load of '%s' completed.",
        //     WideToUtf8(fileName).c_str())
        L2_LOG_INFO(Log, "[AI load] L2Server-requested load of '%s' completed.", fileName.c_str());
    }
    return false;
}

bool GetEventInfo(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 0x80 / sizeof(char16_t)> eventName;
    packet = ::Disassemble(packet, "S", 0x80, eventName.data());
    EventManager::Inst().SetEventData(eventName.data(), packet);
    return false;
}

bool GetDBSavingValue(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int builderId = 0;
    int key = 0;
    ::Disassemble(packet, "dd", &builderId, &key);

    const int value = gGlobalObj.GetDBSavingMap(key);
    std::array<char16_t, 0x40> message;
#if defined(_MSC_VER)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    ::_snwprintf_s(
        reinterpret_cast<wchar_t*>(message.data()), message.size(), _TRUNCATE, L"key = %d value = %d", key, value
    );
#else
    // Host transport for the target's fixed 64-WCHAR _snwprintf_s buffer.
    // This particular integer format always fits, so no secure-CRT failure
    // policy has to be emulated here.
    std::array<char, 0x40> narrow;
    std::snprintf(narrow.data(), narrow.size(), "key = %d value = %d", key, value);
    for (std::size_t index = 0; index < message.size(); ++index) {
        message[index] = static_cast<unsigned char>(narrow[index]);
        if (narrow[index] == '\0') {
            break;
        }
    }
#endif
    g_ns.Send("cdS", 0x54, builderId, message.data());
    return false;
}

bool RegisterOlympiadMaker(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int inzoneTypeId = 0;
    int inzoneId = 0;
    ::Disassemble(packet, "dd", &inzoneTypeId, &inzoneId);
    CNpcMakerDB::Inst().OlympiadInZoneMakerCreate(inzoneTypeId, inzoneId);
    return false;
}

bool OlympiadMatchResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    int reply;
    int result;
    ::Disassemble(packet, "ddd", &userObjectId, &reply, &result);

    User* const user = User::GetObjectW(userObjectId);
    if (!user) {
        return false;
    }

    CNPC* const operatorObject = CNpcMakerDB::Inst().GetOlympiadOperatorBase();
    if (!operatorObject || operatorObject->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateOlympiadMatchResultEvent(operatorObject, user, reply, result);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x12fe)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the Olympiad match-result notification for user %u (reply %d, result %d). Event not
        //     dispatched.", static_cast<unsigned int>(userObjectId), reply, result)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the Olympiad match-result notification for user %u (reply %d, result %d); event not "
            "dispatched.",
            static_cast<unsigned int>(userObjectId), reply, result
        );
        return false;
    }
    operatorObject->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SetAbsoluteDesire(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int targetObjectId;
    int npcObjectId;
    int enabled;
    ::Disassemble(packet, "ddd", &targetObjectId, &npcObjectId, &enabled);

    CCreature* const targetObject = CCreature::GetObjectW(targetObjectId);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc || !npc->m_data->m_bIsActive || !targetObject) {
        return false;
    }
    if (enabled == 0) {
        npc->RemoveAbsoluteDesire();
    } else {
        npc->AddAttackDesire(targetObject->m_data, 1, -1.0f);
    }
    return false;
}

bool DummyPacket(CSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket received from L2Server")
    // Log.Add(LOG_ERROR,
    //     "[L2Server] Unsupported packet reached DummyPacket. Requesting connection close. Loss of this required connection
    //     terminates the NPC service.")
    L2_LOG_ERROR(
        Log,
        "[L2Server] Unsupported packet reached DummyPacket; requesting connection close. Loss of this required connection "
        "terminates the NPC service."
    );
    return true;
}

bool SetEventValue(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint8_t rawIndex;
    int value;
    ::Disassemble(packet, "cd", reinterpret_cast<char*>(&rawIndex), &value);

    gGlobalObj.SetEventValue(static_cast<int>(static_cast<std::int8_t>(rawIndex)), value);
    return false;
}

bool StartUseSkill(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int targetObjectId;
    std::uint64_t skillNameId;
    ::Disassemble(packet, "ddQ", &npcObjectId, &targetObjectId, &skillNameId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    npc->UseSkillStarted(static_cast<std::uint32_t>(targetObjectId), SignedI64Bits(skillNameId));
    return false;
}

bool RegisterAIStatus(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int type;
    std::array<char16_t, 0xa000 / sizeof(char16_t)> name{};
    int inzoneId;
    int status;
    ::Disassemble(packet, "dSdd", &type, 0xa000, name.data(), &inzoneId, &status);

    gGlobalObj.RegisterEventStatus(static_cast<AreaDoorStautsType>(type), name.data(), inzoneId, status);
    return false;
}

bool ResetProfile(CSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // The shipping callback does not disassemble its packet.
    GuardProfileHelper::ResetGuardProfile();
    SystemMail::sendResetProfile(mailServer);
    return false;
}

bool ReportProfile(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int destination = 0;
    ::Disassemble(packet, "d", &destination);

    if (destination == 0) {
        ReportGuardProfile();
    } else if (destination == 1) {
        SystemMail::sendReportProfile(mailServer);
    }
    return false;
}

bool CServerSocket::DynamicQuestPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint8_t rawJob;
    packet = ::Disassemble(packet, "c", reinterpret_cast<char*>(&rawJob));

    const int job = static_cast<int>(static_cast<std::int8_t>(rawJob));
    if (job >= 4) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid job type[%d]",
        //     L"serversocket.cpp",
        //     0x1ccb,
        //     job)
        // Log.Add(LOG_ERROR,
        //     "[dynamic quest] Ignoring unsupported job %d from L2Server. This callback handles jobs 0 and 1.",
        //     job)
        L2_LOG_ERROR(
            Log, "[dynamic quest] Ignoring unsupported job %d from L2Server; this callback handles jobs 0 and 1.", job
        );
        return false;
    }
    if (job < 0 || job > 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid job type[%d]",
        //     L"serversocket.cpp",
        //     0x1ce8,
        //     job)
        // Log.Add(LOG_ERROR,
        //     "[dynamic quest] Ignoring unsupported job %d from L2Server. This callback handles jobs 0 and 1.",
        //     job)
        L2_LOG_ERROR(
            Log, "[dynamic quest] Ignoring unsupported job %d from L2Server; this callback handles jobs 0 and 1.", job
        );
        return false;
    }

    std::uint8_t rawSuccess;
    int makerCount;
    packet = ::Disassemble(packet, "cd", reinterpret_cast<char*>(&rawSuccess), &makerCount);
    const int success = static_cast<int>(static_cast<std::int8_t>(rawSuccess));
    for (int index = 0; index < makerCount; ++index) {
        std::array<char16_t, 0x62 / sizeof(char16_t)> makerName{};
        packet = ::Disassemble(packet, "S", 0x62, makerName.data());
        const std::u16string targetMakerName(makerName.data());
        NpcMakerEx* const maker = CNpcMakerDB::Inst().FindNpcMakerEx(targetMakerName);
        if (!maker) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid maker[%s]",
            //     L"serversocket.cpp",
            //     0x1ce2,
            //     Utf16ToWide(makerName.data()).c_str())
            // Log.Add(LOG_ERROR,
            //     "[dynamic quest] Skipping maker entry %d of %d for job %d (result %d): maker '%s' is not registered.
            //     continuing with the remaining entries.", index + 1, makerCount, job, success,
            //     Utf16ToUtf8(makerName.data()).c_str())
            L2_LOG_ERROR(
                Log,
                "[dynamic quest] Skipping maker entry %d of %d for job %d (result %d): maker '%s' is not registered; "
                "continuing with the remaining entries.",
                index + 1, makerCount, job, success, Utf16ToUtf8(makerName.data()).c_str()
            );
            continue;
        }
        maker->OnDynamicQuestEvent(static_cast<DynamicQuestType::NPCJobType>(job), static_cast<char>(success));
    }
    return false;
}

bool CServerSocket::DeleteTopAgroPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    ::Disassemble(packet, "dd", &ignored, &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (npc) {
        npc->RemoveAttackDesireOnTop();
    }
    return false;
}

bool CServerSocket::SceneStoppedPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int userObjectId;
    int sceneId;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &sceneId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        return false;
    }
    User* const user = User::GetObjectW(userObjectId);

    CNPCEvent* const event = CNPCEventFactory::CreateSceneStoppedEvent(npc, user->m_data, sceneId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1d56)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the scene-stop notification for NPC %u, user %u (scene %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), sceneId)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the scene-stop notification for NPC %u, user %u (scene %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), sceneId
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::GetPledgeRaidRewardRatePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int numerator = 0;
    int denominator = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &numerator, &denominator);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    // 0x005201bd..0x005201de converts the second packet dword to
    // xmm1, the third to xmm0, and executes divss xmm1,xmm0.
    const float rewardRate = denominator > 0 ? static_cast<float>(numerator) / static_cast<float>(denominator) : 0.0f;
    CNPCEvent* const event = CNPCEventFactory::CreateGetPledgeRaidRewardRateEvent(npc, rewardRate);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1d82)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan raid reward-rate reply for NPC %u (rate %g, source ratio %d/%d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<double>(rewardRate), numerator,
        //     denominator)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan raid reward-rate reply for NPC %u (rate %g, source ratio %d/%d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<double>(rewardRate), numerator, denominator
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::InstantAgitPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint8_t rawJob;
    int npcSharedIndex;
    int userSharedIndex;
    int argument;
    ::Disassemble(packet, "cddd", reinterpret_cast<char*>(&rawJob), &npcSharedIndex, &userSharedIndex, &argument);
    const int job = static_cast<int>(static_cast<std::int8_t>(rawJob));
    if (job > 13) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid job type[%d]",
        //     L"serversocket.cpp",
        //     0x1d95,
        //     job)
        // Log.Add(LOG_ERROR,
        //     "[clan hall] Ignoring job %d for NPC shared index %d, user shared index %d (argument %d): the job exceeds the
        //     maximum ID 13.", job, npcSharedIndex, userSharedIndex, argument)
        L2_LOG_ERROR(
            Log,
            "[clan hall] Ignoring job %d for NPC shared index %d, user shared index %d (argument %d): the job exceeds the "
            "maximum ID 13.",
            job, npcSharedIndex, userSharedIndex, argument
        );
        return false;
    }

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }

    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    const bool validUser = user && user->m_bIsActive && user->m_bIsUser;
    if (!validUser) {
        if (job != 6 || userSharedIndex < 1 || npc->m_data->m_bNotFunctionable) {
            return false;
        }
        CNPCEvent* const event =
            CNPCEventFactory::CreateInstantAgitOwnerEvent(npc, static_cast<char>(rawJob), userSharedIndex);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event failed",
            //     L"serversocket.cpp",
            //     0x1db6)
            // Log.Add(LOG_ERROR,
            //     "[clan hall] Cannot create the instance-hall owner notification for NPC shared index %d (job %d, owner
            //     reply %d), event not dispatched.", npcSharedIndex, job, userSharedIndex)
            L2_LOG_ERROR(
                Log,
                "[clan hall] Cannot create the instance-hall owner notification for NPC shared index %d (job %d, owner "
                "reply %d); event not dispatched.",
                npcSharedIndex, job, userSharedIndex
            );
            return false;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
        return false;
    }

    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::CreateInstantAgitEvent(npc, user, static_cast<char>(rawJob), argument);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1da6)
        // Log.Add(LOG_ERROR,
        //     "[clan hall] Cannot create the instance-hall reply for NPC shared index %d, user shared index %d (job %d,
        //     reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, job, argument)
        L2_LOG_ERROR(
            Log,
            "[clan hall] Cannot create the instance-hall reply for NPC shared index %d, user shared index %d (job %d, reply "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, job, argument
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::CallToChangeClass(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId = 0;
    ::Disassemble(packet, "d", &userObjectId);

    User* const user = User::GetObjectW(userObjectId);
    CNPC* const tutorialNpc = g_TutorialNPC;
    if (!user || !tutorialNpc) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCallToChangeClassEvent(tutorialNpc, user->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1dd3)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the tutorial class-change prompt for user %u. Event not dispatched.",
        //     static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the tutorial class-change prompt for user %u; event not dispatched.",
            static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    tutorialNpc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::ChangeToAwakenedClass(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int reply = 0;
    int userObjectId = 0;
    int npcObjectId = 0;
    int integer0 = 0;
    ::Disassemble(packet, "dddd", &reply, &userObjectId, &npcObjectId, &integer0);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateChangeToAwakenedClassEvent(npc, user->m_data, reply, integer0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1df4)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the awakened-class reply for NPC %u, user %u (reply %d, argument %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
        //     integer0)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the awakened-class reply for NPC %u, user %u (reply %d, argument %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply, integer0
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::DynamicQuestInfoPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int questId;
    int eventId;
    int makerCount;
    packet = ::Disassemble(packet, "ddd", &questId, &eventId, &makerCount);

    for (int index = 0; index < makerCount; ++index) {
        std::array<char16_t, 0x62 / sizeof(char16_t)> makerName{};
        packet = ::Disassemble(packet, "S", 0x62, makerName.data());
        const std::u16string targetMakerName(makerName.data());
        NpcMakerEx* const maker = CNpcMakerDB::Inst().FindNpcMakerEx(targetMakerName);
        if (!maker) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid maker[%s]",
            //     L"serversocket.cpp",
            //     0x1e15,
            //     Utf16ToWide(makerName.data()).c_str())
            // Log.Add(LOG_ERROR,
            //     "[dynamic quest] Skipping maker entry %d of %d for quest %d, event %d: maker '%s' is not registered.
            //     continuing with the remaining entries.", index + 1, makerCount, questId, eventId,
            //     Utf16ToUtf8(makerName.data()).c_str())
            L2_LOG_ERROR(
                Log,
                "[dynamic quest] Skipping maker entry %d of %d for quest %d, event %d: maker '%s' is not registered; "
                "continuing with the remaining entries.",
                index + 1, makerCount, questId, eventId, Utf16ToUtf8(makerName.data()).c_str()
            );
            continue;
        }
        maker->OnDynamicQuestInfo(questId, eventId);
    }
    return false;
}

bool CServerSocket::ReplyTotalSkillEnchantCount(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int integer0 = 0;
    int integer1 = 0;
    ::Disassemble(packet, "dddd", &npcObjectId, &userObjectId, &integer0, &integer1);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTotalSkillEnchantCountEvent(npc, user->m_data, integer0, integer1);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1e36)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the total skill-enchant count reply for NPC %u, user %u (values %d, %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), integer0,
        //     integer1)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the total skill-enchant count reply for NPC %u, user %u (values %d, %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), integer0, integer1
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::ChangeClassEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int reply = 0;
    int integer0 = 0;
    ::Disassemble(packet, "dddd", &npcObjectId, &userObjectId, &reply, &integer0);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateChangeClassEvent(npc, user->m_data, reply, integer0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1e57)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the class-change reply for NPC %u, user %u (reply %d, argument %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
        //     integer0)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the class-change reply for NPC %u, user %u (reply %d, argument %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply, integer0
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::CanChangeToSubclass(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int reply = 0;
    int integer0 = 0;
    ::Disassemble(packet, "dddd", &npcObjectId, &userObjectId, &reply, &integer0);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCanChangeToSubclassEvent(npc, user->m_data, reply, integer0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1e78)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the subclass eligibility reply for NPC %u, user %u (reply %d, argument %d). Event
        //     not dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply,
        //     integer0)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the subclass eligibility reply for NPC %u, user %u (reply %d, argument %d); event not "
            "dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply, integer0
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RegisterPledgeGameMaker(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int inzoneTypeId = 0;
    int inzoneId = 0;
    ::Disassemble(packet, "dd", &inzoneTypeId, &inzoneId);
    CNpcMakerDB::Inst().PledgeGameInZoneMakerCreate(inzoneTypeId, inzoneId);
    return false;
}

bool CServerSocket::StartDynamicQuestResult(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    int integer0 = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &userObjectId, &integer0);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateStartDynamicQuestResultEvent(npc, user->m_data, integer0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1e99)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the dynamic-quest start reply for NPC %u, user %u (result %d). Event not
        //     dispatched.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), integer0)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the dynamic-quest start reply for NPC %u, user %u (result %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), integer0
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::EventCampaignPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint8_t rawJob;
    packet = ::Disassemble(packet, "c", reinterpret_cast<char*>(&rawJob));

    const int job = static_cast<int>(static_cast<std::int8_t>(rawJob));
    if (job >= 4) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[EventCampaign][%s][%d] invalid job type[%d]",
        //     L"serversocket.cpp",
        //     0x1cfd,
        //     job)
        // Log.Add(LOG_ERROR,
        //     "[campaign] Ignoring unsupported job %d from L2Server. This callback handles jobs 0 and 1.",
        //     job)
        L2_LOG_ERROR(Log, "[campaign] Ignoring unsupported job %d from L2Server; this callback handles jobs 0 and 1.", job);
        return false;
    }
    if (job < 0 || job > 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[EventCampaign][%s][%d] invalid job type[%d]",
        //     L"serversocket.cpp",
        //     0x1d1a,
        //     job)
        // Log.Add(LOG_ERROR,
        //     "[campaign] Ignoring unsupported job %d from L2Server. This callback handles jobs 0 and 1.",
        //     job)
        L2_LOG_ERROR(Log, "[campaign] Ignoring unsupported job %d from L2Server; this callback handles jobs 0 and 1.", job);
        return false;
    }

    std::uint8_t rawSuccess;
    int makerCount;
    packet = ::Disassemble(packet, "cd", reinterpret_cast<char*>(&rawSuccess), &makerCount);
    const int success = static_cast<int>(static_cast<std::int8_t>(rawSuccess));
    for (int index = 0; index < makerCount; ++index) {
        std::array<char16_t, 0x62 / sizeof(char16_t)> makerName{};
        packet = ::Disassemble(packet, "S", 0x62, makerName.data());
        const std::u16string targetMakerName(makerName.data());
        NpcMakerEx* const maker = CNpcMakerDB::Inst().FindNpcMakerEx(targetMakerName);
        if (!maker) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[EventCampaign][%s][%d] invalid maker[%s]",
            //     L"serversocket.cpp",
            //     0x1d14,
            //     Utf16ToWide(makerName.data()).c_str())
            // Log.Add(LOG_ERROR,
            //     "[campaign] Skipping maker entry %d of %d for job %d (result %d): maker '%s' is not registered. Continuing
            //     with the remaining entries.", index + 1, makerCount, job, success, Utf16ToUtf8(makerName.data()).c_str())
            L2_LOG_ERROR(
                Log,
                "[campaign] Skipping maker entry %d of %d for job %d (result %d): maker '%s' is not registered; continuing "
                "with the remaining entries.",
                index + 1, makerCount, job, success, Utf16ToUtf8(makerName.data()).c_str()
            );
            continue;
        }
        maker->OnEventCampaignEvent(static_cast<EventCampaignType::NPCJobType>(job), static_cast<char>(success));
    }
    return false;
}

bool CServerSocket::AddUseSkillDesireFromServer(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int targetObjectId = 0;
    std::uint64_t skillNameId = 0;
    int unused = 0;
    int attackMoveType = 0;
    int priority = 0;
    ::Disassemble(packet, "ddQddd", &npcObjectId, &targetObjectId, &skillNameId, &unused, &attackMoveType, &priority);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->AddUseSkillDesireEx2(
            targetObjectId, SignedI64Bits(skillNameId), unused, attackMoveType, 1, static_cast<float>(priority), 0, 0, 0
        );
    }
    return false;
}

bool CreateDualJob(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int userSharedIndex;
    int reply;
    ::Disassemble(packet, "ddd", &npcSharedIndex, &userSharedIndex, &reply);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateDualJobCreated(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xf7c)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the dual-class creation reply for NPC shared index %d, user shared index %d (reply
        //     %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the dual-class creation reply for NPC shared index %d, user shared index %d (reply "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::FinishPledgeGamePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    int reply;
    ::Disassemble(packet, "dd", &userObjectId, &reply);

    User* const user = User::GetObjectW(userObjectId);
    CNPC* const operatorBase = CNpcMakerDB::Inst().GetPledgeGameOperatorBase();
    if (!user || !operatorBase || operatorBase->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreatePledgeGameResultEvent(operatorBase, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1ed9)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan-game result notification for user %u (reply %d). Event not dispatched.",
        //     static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the clan-game result notification for user %u (reply %d); event not dispatched.",
            static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }
    operatorBase->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::WorldChatPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    ::Disassemble(packet, "d", &userObjectId);

    User* const user = User::GetObjectW(userObjectId);
    CNPC* const operatorBase = CNpcMakerDB::Inst().GetWorldChatOperatorBase();
    if (!user || !operatorBase || operatorBase->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateWorldChatEvent(operatorBase, user);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1efe)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the world-chat notification for user %u. Event not dispatched.",
        //     static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the world-chat notification for user %u; event not dispatched.",
            static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    operatorBase->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::GetEventGreateContributor(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex = 0;
    int userObjectId = 0;
    ::Disassemble(packet, "dd", &npcSharedIndex, &userObjectId);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    User* const user = User::GetObjectW(userObjectId);
    if (!npc || !user) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateGetGreatEventContributor(npc, user->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1f1c)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the event contributor reply for NPC shared index %d, user %u. Event not
        //     dispatched.", npcSharedIndex, static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the event contributor reply for NPC shared index %d, user %u; event not dispatched.",
            npcSharedIndex, static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool FishingEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex = 0;
    int eventArgument = 0;
    ::Disassemble(packet, "dd", &userSharedIndex, &eventArgument);

    CCreature* const userObject = ResolveMappedCreature(userSharedIndex);
    CNPC* const fishingNpc = g_FishingNPC;
    if (!userObject || !userObject->m_data->m_bIsUser) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;

    CNPCEvent* const event = CNPCEventFactory::CreateFishingEventEvent(fishingNpc, user, eventArgument);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1f41)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the fishing notification for user shared index %d (argument %d). Event not
        //     dispatched.", userSharedIndex, eventArgument)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the fishing notification for user shared index %d (argument %d); event not "
            "dispatched.",
            userSharedIndex, eventArgument
        );
        return false;
    }
    if (fishingNpc) {
        fishingNpc->HandleEvent(event);
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::Handle_NS_CAST_BUFF_TO_RANDOM_USER(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int userObjectId = 0;
    ::Disassemble(packet, "dd", &npcObjectId, &userObjectId);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No npc in Handle_NS_CAST_BUFF_TO_RANDOM_USER: %d",
        //     npcObjectId)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot notify NPC %u to buff user %u: NPC not found.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId))
        L2_LOG_WARN(
            Log, "[AI event] Cannot notify NPC %u to buff user %u: NPC not found.", static_cast<unsigned int>(npcObjectId),
            static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    User* const user = User::GetObjectW(userObjectId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No user in Handle_NS_CAST_BUFF_TO_RANDOM_USER: %d",
        //     userObjectId)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot notify NPC %u to buff user %u: user not found.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId))
        L2_LOG_WARN(
            Log, "[AI event] Cannot notify NPC %u to buff user %u: user not found.", static_cast<unsigned int>(npcObjectId),
            static_cast<unsigned int>(userObjectId)
        );
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::Create_NPCEVENT_CAST_BUFF_TO_RANDOM_USER(npc, user->m_data);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x1ff3)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the random-user buff event for NPC %u, user %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the random-user buff event for NPC %u, user %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::NpcPosEventWithReward(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 2000 / sizeof(char16_t)> eventName;
    int enabled;
    int rewardItemEnabled;
    std::uint64_t rewardItemCountBits;
    int rewardNpcExistTimeDays;
    int resetInzoneBindingEnabled;
    int ignoredResetLowerBound;
    int resetUpperBound;
    ::Disassemble(
        packet, "SddQdddd", 2000, eventName.data(), &enabled, &rewardItemEnabled, &rewardItemCountBits,
        &rewardNpcExistTimeDays, &resetInzoneBindingEnabled, &ignoredResetLowerBound, &resetUpperBound
    );

    const std::int64_t rewardItemCount = SignedI64Bits(rewardItemCountBits);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NS_NPCPOS_EVENT_WITH_REWARD"
    //     L"(%s, %d, %d, %I64d, %d, %d, %d, %d)",
    //     Utf16ToWide(eventName.data()).c_str(),
    //     enabled,
    //     rewardItemEnabled,
    //     static_cast<long long>(rewardItemCount),
    //     rewardNpcExistTimeDays,
    //     resetInzoneBindingEnabled,
    //     ignoredResetLowerBound,
    //     resetUpperBound)
    // Log.Add(LOG_NORMAL,
    //     "[NPC event] Received request to %s position event '%s': item rewards %s, count %lld. Requested NPC lifetime %d
    //     days. Binding reset %s, end %d (supplied start %d is ignored).", enabled != 0 ? "enable" : "disable",
    //     Utf16ToUtf8(eventName.data()).c_str(), rewardItemEnabled != 0 ? "enabled" : "disabled", static_cast<long
    //     long>(rewardItemCount), rewardNpcExistTimeDays, resetInzoneBindingEnabled != 0 ? "enabled" : "disabled",
    //     resetUpperBound, ignoredResetLowerBound)
    L2_LOG_INFO(
        Log,
        "[NPC event] Received request to %s position event '%s': item rewards %s, count %lld; requested NPC lifetime %d "
        "days; binding reset %s, end %d (supplied start %d is ignored).",
        enabled != 0 ? "enable" : "disable", Utf16ToUtf8(eventName.data()).c_str(),
        rewardItemEnabled != 0 ? "enabled" : "disabled", static_cast<long long>(rewardItemCount), rewardNpcExistTimeDays,
        resetInzoneBindingEnabled != 0 ? "enabled" : "disabled", resetUpperBound, ignoredResetLowerBound
    );

    CNpcMakerDB::Inst().OnNpcPosEvent(eventName.data(), enabled != 0);

    const std::uint32_t rewardHoursBits = static_cast<std::uint32_t>(rewardNpcExistTimeDays) * 24u;
    std::int32_t rewardHours = 0;
    std::memcpy(&rewardHours, &rewardHoursBits, sizeof(rewardHours));
    gGlobalObj.m_adminRewardSystem.Set(
        enabled != 0, rewardItemEnabled != 0, rewardItemCount, rewardHours, resetInzoneBindingEnabled != 0,
        ignoredResetLowerBound, resetUpperBound
    );
    return false;
}

bool CServerSocket::Handle_NS_RESULT_PLEDGE_RAID_POINT(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int success = 0;
    int ignored = 0;
    int pledgeSharedIndex = 0;
    std::uint64_t reply = 0;
    ::Disassemble(packet, "ddddQ", &npcObjectId, &success, &ignored, &pledgeSharedIndex, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No npc in Handle_NS_RESULT_PLEDGE_RAID_POINT: %d",
        //     npcObjectId)
        // Log.Add(LOG_WARN,
        //     "[raid] Cannot forward the clan raid-points reply to NPC %u, pledge shared index %d (result %d, reply %lld):
        //     NPC not found.", static_cast<unsigned int>(npcObjectId), pledgeSharedIndex, success, static_cast<long
        //     long>(SignedI64Bits(reply)))
        L2_LOG_WARN(
            Log,
            "[raid] Cannot forward the clan raid-points reply to NPC %u, pledge shared index %d (result %d, reply %lld): "
            "NPC not found.",
            static_cast<unsigned int>(npcObjectId), pledgeSharedIndex, success, static_cast<long long>(SignedI64Bits(reply))
        );
        return false;
    }
    CSharedPledgeData* const pledge = CPledgeFactory::Inst().GetData(pledgeSharedIndex);
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Pledge Index invalid "
        //     L"Handle_NS_RESULT_PLEDGE_RAID_POINT: %d",
        //     pledgeSharedIndex)
        // Log.Add(LOG_WARN,
        //     "[raid] Cannot forward the clan raid-points reply to NPC %u, pledge shared index %d (result %d, reply %lld):
        //     clan shared data unavailable.", static_cast<unsigned int>(npcObjectId), pledgeSharedIndex, success,
        //     static_cast<long long>(SignedI64Bits(reply)))
        L2_LOG_WARN(
            Log,
            "[raid] Cannot forward the clan raid-points reply to NPC %u, pledge shared index %d (result %d, reply %lld): "
            "clan shared data unavailable.",
            static_cast<unsigned int>(npcObjectId), pledgeSharedIndex, success, static_cast<long long>(SignedI64Bits(reply))
        );
        return false;
    }

    CNPCEvent* const event =
        CNPCEventFactory::Create_NPCEVENT_RESULT_PLEDGE_RAID_POINT(npc, success, pledge, SignedI64Bits(reply));
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x2020)
        // Log.Add(LOG_ERROR,
        //     "[raid] Cannot create the clan raid-points reply for NPC %u, pledge shared index %d (result %d, reply %lld)
        //     event not dispatched.", static_cast<unsigned int>(npcObjectId), pledgeSharedIndex, success, static_cast<long
        //     long>(SignedI64Bits(reply)))
        L2_LOG_ERROR(
            Log,
            "[raid] Cannot create the clan raid-points reply for NPC %u, pledge shared index %d (result %d, reply %lld); "
            "event not dispatched.",
            static_cast<unsigned int>(npcObjectId), pledgeSharedIndex, success, static_cast<long long>(SignedI64Bits(reply))
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::Handle_NS_ON_HUNTER_GUILD_BLESS_EVENT(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int enabled = 0;
    ::Disassemble(packet, "d", &enabled);
    CNpcMakerDB::Inst().OnHunterGuildBless(enabled != 0);
    return false;
}

bool CServerSocket::Handle_NS_WORLD_RAID_START(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int integer0 = 0;
    int integer1 = 0;
    ::Disassemble(packet, "ddd", &npcObjectId, &integer0, &integer1);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No npc in Handle_NS_WORLD_RAID_START: %d",
        //     npcObjectId)
        // Log.Add(LOG_WARN,
        //     "[raid] Cannot dispatch world-raid start to NPC %u (arguments %d, %d): NPC not found.",
        //     static_cast<unsigned int>(npcObjectId), integer0, integer1)
        L2_LOG_WARN(
            Log, "[raid] Cannot dispatch world-raid start to NPC %u (arguments %d, %d): NPC not found.",
            static_cast<unsigned int>(npcObjectId), integer0, integer1
        );
        return false;
    }
    CNPCEvent* const event = CNPCEventFactory::Create_NPCEVENT_WORLD_RAID_START(npc, integer0, integer1);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x2053)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the world-raid start event for NPC %u (arguments %d, %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), integer0, integer1)
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create the world-raid start event for NPC %u (arguments %d, %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), integer0, integer1
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::Handle_NS_WORLD_RAID_STOP(CSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CServerSocket::Handle_NS_WORLD_RAID_BROADCAST_EVENT(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int integer0 = 0;
    int argument1 = 0;
    int argument2 = 0;
    int argument3 = 0;
    ::Disassemble(packet, "ddddd", &npcObjectId, &integer0, &argument1, &argument2, &argument3);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No npc in "
        //     L"Handle_NS_WORLD_RAID_BROADCAST_EVENT: %d",
        //     npcObjectId)
        // Log.Add(LOG_WARN,
        //     "[raid] Cannot dispatch world-raid broadcast to NPC %u (values %d, %d, %d, %d): NPC not found.",
        //     static_cast<unsigned int>(npcObjectId), integer0, argument1, argument2, argument3)
        L2_LOG_WARN(
            Log, "[raid] Cannot dispatch world-raid broadcast to NPC %u (values %d, %d, %d, %d): NPC not found.",
            static_cast<unsigned int>(npcObjectId), integer0, argument1, argument2, argument3
        );
        return false;
    }
    CNPCEvent* const event =
        CNPCEventFactory::Create_NPCEVENT_WORLD_RAID_EVENT(npc, integer0, argument1, argument2, argument3);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x2082)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the world-raid broadcast for NPC %u (values %d, %d, %d, %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), integer0, argument1, argument2, argument3)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the world-raid broadcast for NPC %u (values %d, %d, %d, %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), integer0, argument1, argument2, argument3
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::EventNpc(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex = 0;
    int eventType = 3;
    int eventId = 0;
    int integer0 = 0;
    ::Disassemble(packet, "dddd", &userSharedIndex, &eventType, &eventId, &integer0);

    CCreature* const userObject = ResolveMappedCreature(userSharedIndex);
    CNPC* const fishingNpc = g_FishingNPC;
    if (!userObject) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;
    if (!user->m_bIsUser) {
        return false;
    }

    CNPCEvent* event = nullptr;
    // The kept legacy diagnostic uses this source-line selector.
    [[maybe_unused]] int releaseLine = 0;
    // int failureLine = 0
    [[maybe_unused]] int failureLine = 0;
    switch (eventType) {
        case 0:
            event = CNPCEventFactory::CreateItemNpcEvent(fishingNpc, user, eventId, integer0);
            releaseLine = 0x1f9a;
            failureLine = 0x1f9e;
            break;
        case 1:
            event = CNPCEventFactory::CreatePcDieNpcEvent(fishingNpc, user, eventId);
            releaseLine = 0x1fac;
            failureLine = 0x1fb0;
            break;
        case 2:
            event = CNPCEventFactory::CreateMailNpcEvent(fishingNpc, user, eventId);
            releaseLine = 0x1fbe;
            failureLine = 0x1fc2;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Event Type not defined",
            //     L"serversocket.cpp",
            //     0x1fc9)
            // Log.Add(LOG_ERROR,
            //     "[AI event] Ignoring unsupported NPC event type %d for user shared index %d (event %d, argument %d)
            //     expected 0 (item), 1 (player death) or 2 (mail).", eventType, userSharedIndex, eventId, integer0)
            L2_LOG_ERROR(
                Log,
                "[AI event] Ignoring unsupported NPC event type %d for user shared index %d (event %d, argument %d); "
                "expected 0 (item), 1 (player death) or 2 (mail).",
                eventType, userSharedIndex, eventId, integer0
            );
            return false;
    }

    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     failureLine)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the %s notification for user shared index %d (event %d, argument %d). Event not
        //     dispatched.", eventType == 0 ? "item" : eventType == 1 ? "player death" : "mail", userSharedIndex, eventId,
        //     integer0)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the %s notification for user shared index %d (event %d, argument %d); event not "
            "dispatched.",
            eventType == 0       ? "item"
                : eventType == 1 ? "player death"
                                 : "mail",
            userSharedIndex, eventId, integer0
        );
        return false;
    }
    if (fishingNpc) {
        fishingNpc->HandleEvent(event);
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CServerSocket::RealDamagePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    CAutoStartTimer timer(SERVERSOCKET__ATTACK_PACKET);
    int attackerObjectId = 0;
    int npcObjectId = 0;
    std::uint64_t damageBits = 0;
    ::Disassemble(packet, "ddQ", &attackerObjectId, &npcObjectId, &damageBits);

    CCreature* const attackerObject = CCreature::GetObjectW(static_cast<std::uint32_t>(attackerObjectId));
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    const std::int64_t damage = SignedI64Bits(damageBits);
    if (!attackerObject || !npc || !attackerObject->m_data->m_bIsActive || !npc->m_data->m_bIsActive || damage < 1) {
        return false;
    }

    npc->dwLastAttackedTick = runtime::GetTickCount();
    npc->RealDamaged(attackerObject->m_data, damage);
    return false;
}

bool EnterWorldPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int objectId;
    int sharedIndex;
    std::array<char16_t, 0x800 / sizeof(char16_t)> name16;
    ::Disassemble(packet, "ddS", &objectId, &sharedIndex, 0x800, name16.data());

    CNPC* const npc = CNPC::GetObjectW(objectId);
    if (!npc) {
        const std::string name = Utf16ToUtf8(name16.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid NPC sid[%d], "
        //     L"index[%d], name[%s]",
        //     L"serversocket.cpp",
        //     0x133,
        //     objectId,
        //     sharedIndex,
        //     name.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC entry] Cannot enter the world for object %u, shared index %d ('%s'): NPC not found.",
        //     static_cast<unsigned int>(objectId), sharedIndex, WideToUtf8(name).c_str())
        L2_LOG_ERROR(
            Log, "[NPC entry] Cannot enter the world for object %u, shared index %d ('%s'): NPC not found.",
            static_cast<unsigned int>(objectId), sharedIndex, name.c_str()
        );
        return false;
    }
    if (npc->m_data->index != sharedIndex) {
        const std::string name = Utf16ToUtf8(name16.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid NPC index. "
        //     L"server[%d], npc[%d], name[%s]",
        //     L"serversocket.cpp",
        //     0x125,
        //     sharedIndex,
        //     npc->m_data->index,
        //     name.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC entry] Rejecting world entry for object %u ('%s'): shared index from L2Server is %d, but the NPC
        //     currently has index %d.", static_cast<unsigned int>(objectId), WideToUtf8(name).c_str(), sharedIndex,
        //     npc->m_data->index)
        L2_LOG_ERROR(
            Log,
            "[NPC entry] Rejecting world entry for object %u ('%s'): shared index from L2Server is %d, but the NPC "
            "currently has index %d.",
            static_cast<unsigned int>(objectId), name.c_str(), sharedIndex, npc->m_data->index
        );
        return false;
    }
    if (!npc->EnterWorld()) {
        return false;
    }

    npc->m_nSpawnTick = runtime::GetTickCount();
    npc->SpawnDefine()->OnNpcEnterWorld(npc);
    return false;
}

bool MovePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int objectId;
    int x;
    int y;
    int z;
    ::Disassemble(packet, "dddd", &objectId, &x, &y, &z);
    return false;
}

bool AttackPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    CAutoStartTimer timer(SERVERSOCKET__ATTACK_PACKET);
    int attackerObjectId;
    int targetObjectId;
    int damage;
    int attackedArgument;
    int attackedTail;
    ::Disassemble(packet, "ddddd", &attackerObjectId, &targetObjectId, &damage, &attackedArgument, &attackedTail);

    CCreature* const attackerObject = CCreature::GetObjectW(static_cast<std::uint32_t>(attackerObjectId));
    CCreature* const targetObject = CCreature::GetObjectW(static_cast<std::uint32_t>(targetObjectId));
    if (!attackerObject || !targetObject) {
        return false;
    }

    if (attackerObject->IsPC()) {
        attackerObject->CastUser()->HandleMasterAttackEvent(targetObject, damage, 0, 0, 0);
    }
    if (targetObject->IsPC()) {
        targetObject->CastUser()->HandleMasterAttackedEvent(attackerObject, damage, 0, 0, 0);
    }

    CNPC* const target = CNPC::GetObjectW(targetObjectId);
    CSharedCreatureData* const attacker = attackerObject->m_data;
    if (!target || !attacker->m_bIsActive || !target->m_data->m_bIsActive) {
        return false;
    }

    target->dwLastAttackedTick = runtime::GetTickCount();
    if (!target->m_data->m_bNotFunctionable) {
        target->Attacked(attacker, damage, attackedArgument, 0, true, attackedTail);
    }
    return false;
}

bool DiePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int objectId = 0;
    char16_t killerMasterName[25]{};
    int reason = 0;
    ::Disassemble(packet, "dSd", &objectId, static_cast<int>(sizeof(killerMasterName)), killerMasterName, &reason);

    CNPC* const npc = CNPC::GetObjectW(objectId);
    if (!npc) {
        const std::string killerName = Utf16ToUtf8(killerMasterName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Npc died, but cannot "
        //     L"find npc. sKillerMasterName [%s]",
        //     L"serversocket.cpp",
        //     0x1ed,
        //     killerName.c_str())
        // Log.Add(LOG_WARN,
        //     "[NPC death] Skipping death notification for NPC %u (reason %d, killer master '%s'): NPC not found.",
        //     static_cast<unsigned int>(objectId), reason, WideToUtf8(killerName).c_str())
        L2_LOG_WARN(
            Log, "[NPC death] Skipping death notification for NPC %u (reason %d, killer master '%s'): NPC not found.",
            static_cast<unsigned int>(objectId), reason, killerName.c_str()
        );
        return false;
    }

    if (!npc->m_data->m_bIsActive) {
        // Current EXE 0x0051941a..0x0051942a supplies only objectId even
        // though the shipping format contains two integer conversions.
        // Log.Add(
        //     LOG_ERROR,
        //     L"DiePacket npc is not active but die "
        //     L"sid[%d] classid[%d]",
        //     objectId)
        // Log.Add(LOG_WARN,
        //     "[NPC death] Received death notification for inactive NPC %u (reason %d). Continuing death processing.",
        //     static_cast<unsigned int>(objectId), reason)
        L2_LOG_WARN(
            Log, "[NPC death] Received death notification for inactive NPC %u (reason %d); continuing death processing.",
            static_cast<unsigned int>(objectId), reason
        );
    }

    // Noise: Historical TTP 67260 tracing duplicates normal NPC death processing. Actionable failures remain logged.
    // const auto isTtpNpc = [npc] {
    //     const int npcClassId =
    //         npc->m_data->m_nClassID.
    //     return npcClassId == 0x0fa4e4 ||
    //            npcClassId == 0x0fa4e7 ||
    //            npcClassId == 0x0fa4dd ||
    //            npcClassId == 0x0fa41b ||
    //            npcClassId == 0x0fa5f4 ||
    //            npcClassId == 0x0fa5c6.
    // }
    // Noise: Historical TTP 67260 tracing duplicates normal NPC death processing. Actionable failures remain logged.
    // if (isTtpNpc()) {
    //     const std::wstring npcName =
    //         Utf16ToWide(npc->m_data->Name())
    //     const std::wstring killerName =
    //         Utf16ToWide(killerMasterName)
    //     Log.Add(
    //         LOG_ERROR,
    //         L"[NO_ERROR][TTP 67260] NPC[%s] died, "
    //         L"sKillerMasterName [%s]",
    //         npcName.c_str(),
    //         killerName.c_str())
    // }

    CNPCEvent* const event = CNPCEventFactory::CreateMyDyingEvent(npc);
    if (!event) {
        const std::string npcName = Utf16ToUtf8(npc->m_data->Name());
        const std::string killerName = Utf16ToUtf8(killerMasterName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateMyDyingEvent failed. "
        //     L"NPC[%s], sKillerMasterName [%s]",
        //     L"serversocket.cpp",
        //     0x1e3,
        //     npcName.c_str(),
        //     killerName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC death] Cannot create the dying event for NPC %u ('%s', reason %d, killer master '%s'). Continuing with
        //     death persistence and decay.", static_cast<unsigned int>(objectId), WideToUtf8(npcName).c_str(), reason,
        //     WideToUtf8(killerName).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC death] Cannot create the dying event for NPC %u ('%s', reason %d, killer master '%s'); continuing with "
            "death persistence and decay.",
            static_cast<unsigned int>(objectId), npcName.c_str(), reason, killerName.c_str()
        );
    } else {
        // Noise: Historical TTP 67260 tracing duplicates normal NPC death processing. Actionable failures remain logged.
        // if (isTtpNpc()) {
        //     const std::wstring npcName =
        //         Utf16ToWide(npc->m_data->Name())
        //     const std::wstring killerName =
        //         Utf16ToWide(killerMasterName)
        //     Log.Add(
        //         LOG_ERROR,
        //         L"[NO_ERROR][TTP 67260] NPC[%s] "
        //         L"my_dying_event Called!, "
        //         L"sKillerMasterName [%s]",
        //         npcName.c_str(),
        //         killerName.c_str())
        // }
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    }

    npc->SaveDie(killerMasterName, reason);
    npc->AddDecayingDesire(6.6e30f);
    npc->m_bDied = true;
    return false;
}

bool TeleportRequestedEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int playerSharedIndex;
    int npcSharedIndex;
    ::Disassemble(packet, "dd", &playerSharedIndex, &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTeleportRequestedEvent(npc, player);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x244)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the teleport request for NPC shared index %d, user shared index %d. Event not
        //     dispatched.", npcSharedIndex, playerSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the teleport request for NPC shared index %d, user shared index %d; event not "
            "dispatched.",
            npcSharedIndex, playerSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool BuyRequestedEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int playerSharedIndex;
    int npcSharedIndex;
    ::Disassemble(packet, "dd", &playerSharedIndex, &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateBuyRequestedEvent(npc, player);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x266)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the purchase request for NPC shared index %d, user shared index %d. Event not
        //     dispatched.", npcSharedIndex, playerSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the purchase request for NPC shared index %d, user shared index %d; event not "
            "dispatched.",
            npcSharedIndex, playerSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SellRequestedEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int playerSharedIndex;
    int npcSharedIndex;
    ::Disassemble(packet, "dd", &playerSharedIndex, &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSellRequestedEvent(npc, player);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x287)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the sale request for NPC shared index %d, user shared index %d. Event not
        //     dispatched.", npcSharedIndex, playerSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the sale request for NPC shared index %d, user shared index %d; event not dispatched.",
            npcSharedIndex, playerSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ResurrectionPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int objectId;
    int objectSharedIndex;
    int npcSharedIndex;
    ::Disassemble(packet, "ddd", &objectId, &objectSharedIndex, &npcSharedIndex);

    if (CCreatureFactory::Inst().GetIdByIndex(objectSharedIndex) != objectId) {
        return false;
    }

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }
    if (npc->m_data->IsSummonOrPet()) {
        // Shipping .rdata at 0x0063f768 is 0x41a00000.
        npc->AddPetDefaultDesire_Follow(20.0f);
    }
    return false;
}

bool CServerSocket::SkillUsePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int casterObjectId;
    int casterSharedIndex;
    int targetObjectId;
    int skillId;
    int skillLevel;
    int skillSubLevel;
    int successNotification;
    ::Disassemble(
        packet, "ddddddd", &casterObjectId, &casterSharedIndex, &targetObjectId, &skillId, &skillLevel, &skillSubLevel,
        &successNotification
    );

    CCreature* const casterObject = CCreature::GetObjectW(static_cast<std::uint32_t>(casterObjectId));
    if (!casterObject) {
        return false;
    }
    CSharedCreatureData* const caster = casterObject->m_data;
    if (caster->index != casterSharedIndex) {
        return false;
    }

    const std::uint64_t skillNameId = PackSkillNameId(skillId, skillLevel, skillSubLevel);
    const std::int64_t signedSkillNameId = SignedI64Bits(skillNameId);
    if (casterObject->IsNPC()) {
        CNPC* const casterNpc = casterObject->CastNPC();
        casterNpc->SetSkillFired(signedSkillNameId, targetObjectId, successNotification);
    }

    CCreature* const targetObject = CCreature::GetObjectW(static_cast<std::uint32_t>(targetObjectId));
    if (!targetObject) {
        return false;
    }

    bool skillFound = true;
    const int effectPoint = g_PrecompiledHeaderDB.Skill_GetEffectPoint(signedSkillNameId, skillFound);
    int damage = 0;
    if (skillFound && effectPoint < 0) {
        damage = runtime::NegateInt32Wrapping(effectPoint);
    }
    if (effectPoint < 0) {
        if (casterObject->IsPC()) {
            User* const user = casterObject->CastUser();
            user->HandleMasterAttackEvent(targetObject, damage, skillId, skillLevel, skillSubLevel);
        }
        if (targetObject->IsPC()) {
            User* const user = targetObject->CastUser();
            user->HandleMasterAttackedEvent(casterObject, damage, skillId, skillLevel, skillSubLevel);
        }
    }

    targetObject->Spelled(caster, skillId, skillLevel, skillSubLevel);
    return false;
}

bool CheckMapIndex(CSocket*, const unsigned char* packet [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // The shipping body contains trace bookkeeping only.
    return false;
}

bool RequestNPCSpawn(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 200 / sizeof(char16_t)> nameBuffer{};
    int x;
    int y;
    int z;
    int inZoneId;
    std::array<char16_t, 0x200 / sizeof(char16_t)> aiNameBuffer{};
    int airShipServerId;
    std::uint8_t createFlag;
    int createArgument;
    int heading = 0;
    std::uint8_t group0Flag = 0;
    int group0Argument0 = 0;
    int group0Argument1 = 0;
    std::uint8_t decoType = 0;
    int decoArgument0 = 0;
    int decoArgument1 = 0;
    packet = ::Disassemble(
        packet, "SddddSdcdd", 200, nameBuffer.data(), &x, &y, &z, &inZoneId, 0x200, aiNameBuffer.data(), &airShipServerId,
        reinterpret_cast<char*>(&createFlag), &createArgument, &heading
    );
    packet = ::Disassemble(packet, "cdd", reinterpret_cast<char*>(&group0Flag), &group0Argument0, &group0Argument1);
    packet = ::Disassemble(packet, "cdd", reinterpret_cast<char*>(&decoType), &decoArgument0, &decoArgument1);

    if (inZoneId != 0) {
        NpcMakerEx* const maker = CNpcMakerDB::Inst().GetInstantZoneMakerEx(inZoneId, u"", false);
        if (!maker || (maker->GetInZoneID() != 0 && maker->GetInZoneDeleteCount() != 0)) {
            return false;
        }
    }

    int finalArgument = 0;
    ::Disassemble(packet, "d", &finalArgument);

    auto* const definition = new CNpcSpawnDefineWorld();
    if (aiNameBuffer[0] != u'\0') {
        definition->SetAI(aiNameBuffer.data());
    }

    if (heading > 0xffff) {
        heading = static_cast<int>(genrand64_real2() * 65536.0);
    }

    auto* const position = new SpawnPos(x, y, z, heading, 0x65);

    // RequestNPCSpawn @ 0x0052f320 writes the concrete definition fields
    // before publishing it: bAnywhere=false, total=1, respawn=15, one
    // SpawnPos, g_WorldNPCMaker, then the virtual ReserveNPC call.
    definition->SetAnywhere(false);
    definition->SetTotal(1);
    // SHIPPING BUG LEAK-INT-001: L2NPC.exe 0x0052f5be duplicates this name
    // into a calloc allocation, then SetName copies it into m_sName and the
    // temporary is never released. Keep the recovered leaking call disabled.
    /*
    definition->SetName(
        TargetWcsDup(nameBuffer.data())).
    */

    definition->SetName(nameBuffer.data());
    definition->SetRespawnTime(15);
    definition->AddSpawnPosition(position);
    definition->SetNpcMaker(&g_WorldNPCMaker);
    definition->ReserveNPC();
    definition->SetInZoneID(static_cast<std::uint32_t>(inZoneId));
    definition->SetDecoNpcInfo(static_cast<NpcTypeForDeco::Type>(decoType), decoArgument0);

    // Current L2NPC.exe RequestNPCSpawn 0x0052f320 calls the common
    // helper at 0x004e7a48. Besides the exact packet, that helper increments
    // NpcMaker_RequestCreate (not NpcMakerWorld_RequestCreate).
    SendCreateNpcPacket(
        nameBuffer.data(), definition, definition->GetPledgeDBID(), 0, 0, 0, 0, 0, 0, 0,
        static_cast<std::uint32_t>(inZoneId), static_cast<std::uint32_t>(airShipServerId), static_cast<char>(createFlag),
        createArgument, 0, 0, 0, static_cast<char>(group0Flag), static_cast<std::uint32_t>(group0Argument0),
        static_cast<std::uint32_t>(group0Argument1), static_cast<char>(decoType), decoArgument0, decoArgument1, finalArgument
    );

    const std::string name = Utf16ToUtf8(nameBuffer.data());
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[BuilderCmd] summon create. %s (%d, %d, %d). "
    //     L"ZoneId = %d, airshipServerID = %d",
    //     name.c_str(),
    //     x,
    //     y,
    //     z,
    //     inZoneId,
    //     airShipServerId)
    // Log.Add(LOG_NORMAL,
    //     "[NPC spawn] Requested '%s' at (%d, %d, %d), heading %d, instance zone %d, airship server %d, AI '%s'.",
    //     WideToUtf8(name).c_str(), x, y, z, heading, inZoneId, airShipServerId,
    //     aiNameBuffer[0] != u'\0' ? Utf16ToUtf8(aiNameBuffer.data()).c_str() : "<default>")
    L2_LOG_INFO(
        Log, "[NPC spawn] Requested '%s' at (%d, %d, %d), heading %d, instance zone %d, airship server %d, AI '%s'.",
        name.c_str(), x, y, z, heading, inZoneId, airShipServerId,
        aiNameBuffer[0] != u'\0' ? Utf16ToUtf8(aiNameBuffer.data()).c_str() : "<default>"
    );
    return false;
}

bool SeeCreature(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int observerId;
    packet = ::Disassemble(packet, "d", &observerId);

    CCreature* const observerObject = CCreature::GetObjectW(static_cast<std::uint32_t>(observerId));
    if (!observerObject) {
        return false;
    }
    CSharedCreatureData* const observer = observerObject->m_data;

    for (;;) {
        int seenId;
        packet = ::Disassemble(packet, "d", &seenId);
        if (seenId < -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Something wrong -_-...",
            //     L"serversocket.cpp",
            //     0x438)
            // Log.Add(LOG_ERROR,
            //     "[visibility] Stopping the creature list for observer %u: invalid entry %d. Expected a nonnegative object
            //     ID or the -1 terminator.", static_cast<unsigned int>(observerId), seenId)
            L2_LOG_ERROR(
                Log,
                "[visibility] Stopping the creature list for observer %u: invalid entry %d; expected a nonnegative object "
                "ID or the -1 terminator.",
                static_cast<unsigned int>(observerId), seenId
            );
            return false;
        }
        if (seenId == -1) {
            return false;
        }

        CCreature* const seenObject = CCreature::GetObjectW(static_cast<std::uint32_t>(seenId));
        if (!seenObject) {
            continue;
        }
        CSharedCreatureData* const seen = seenObject->m_data;

        const FVector observerPosition = observer->GetPos();
        const FVector seenPosition = seen->GetPos();
        const double deltaX = observerPosition.x - seenPosition.x;
        const double deltaY = observerPosition.y - seenPosition.y;
        if (deltaX * deltaX + deltaY * deltaY > 50331648.0) {
            // Noise: Skipping a recipient outside the visibility radius is a normal filter. One record per skipped creature
            // floods AI activity. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] too far creature see event",
            //     L"serversocket.cpp",
            //     0x444)
            continue;
        }

        // The callback owns only this virtual boundary. CNPC and User keep
        // their distinct reciprocal-visibility behavior.
        observerObject->SeeCreature(seenObject);
    }
}

bool OneSkillSelected(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int playerSharedIndex;
    int npcSharedIndex;
    std::uint64_t skillNameId = 0;
    int needQuest;
    int integer0;
    ::Disassemble(
        packet, "dddQdd", &ignoredObjectId, &playerSharedIndex, &npcSharedIndex, &skillNameId, &needQuest, &integer0
    );

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in OneSkillSelected",
        //     L"serversocket.cpp",
        //     0x46f)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the skill selection for NPC shared index %d, user shared index %d (skill ID
        //     0x%016llX, quest requirement %d, argument %d): %s.", npcSharedIndex, playerSharedIndex, static_cast<unsigned
        //     long long>(skillNameId), needQuest, integer0, !npc ? "NPC not found" : !player ? "user shared data
        //     unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the skill selection for NPC shared index %d, user shared index %d (skill ID "
            "0x%016llX, quest requirement %d, argument %d): %s.",
            npcSharedIndex, playerSharedIndex, static_cast<unsigned long long>(skillNameId), needQuest, integer0,
            !npc          ? "NPC not found"
                : !player ? "user shared data unavailable"
                          : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateOneSkillSelectedEvent(
        npc, player, static_cast<std::int64_t>(skillNameId), needQuest, integer0
    );
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x469)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the skill selection event for NPC shared index %d, user shared index %d (skill ID
        //     0x%016llX, quest requirement %d, argument %d). Event not dispatched.", npcSharedIndex, playerSharedIndex,
        //     static_cast<unsigned long long>(skillNameId), needQuest, integer0)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the skill selection event for NPC shared index %d, user shared index %d (skill ID "
            "0x%016llX, quest requirement %d, argument %d); event not dispatched.",
            npcSharedIndex, playerSharedIndex, static_cast<unsigned long long>(skillNameId), needQuest, integer0
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool LearnSkillRequest(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int playerSharedIndex;
    int npcSharedIndex;
    ::Disassemble(packet, "ddd", &ignoredObjectId, &playerSharedIndex, &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in LearnSkillRequest",
        //     L"serversocket.cpp",
        //     0x4de)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the skill-learning request for NPC shared index %d, user shared index %d: %s.",
        //     npcSharedIndex, playerSharedIndex,
        //     !npc ? "NPC not found" : !player ? "user shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log, "[AI event] Cannot process the skill-learning request for NPC shared index %d, user shared index %d: %s.",
            npcSharedIndex, playerSharedIndex,
            !npc          ? "NPC not found"
                : !player ? "user shared data unavailable"
                          : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateLearnSkillRequestedEvent(npc, player);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x4d8)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the skill-learning request event for NPC shared index %d, user shared index %d.
        //     event not dispatched.", npcSharedIndex, playerSharedIndex)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the skill-learning request event for NPC shared index %d, user shared index %d; event "
            "not dispatched.",
            npcSharedIndex, playerSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SkillQuestAccept(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int playerSharedIndex;
    int npcSharedIndex;
    int questId;
    ::Disassemble(packet, "dddd", &ignoredObjectId, &playerSharedIndex, &npcSharedIndex, &questId);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in SkillQuestAccept",
        //     L"serversocket.cpp",
        //     0x528)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the skill-quest acceptance for NPC shared index %d, user shared index %d (quest
        //     %d): %s.", npcSharedIndex, playerSharedIndex, questId, !npc ? "NPC not found" : !player ? "user shared data
        //     unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the skill-quest acceptance for NPC shared index %d, user shared index %d (quest %d): "
            "%s.",
            npcSharedIndex, playerSharedIndex, questId,
            !npc          ? "NPC not found"
                : !player ? "user shared data unavailable"
                          : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSkillQuestAcceptedEvent(npc, player, questId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x522)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the skill-quest acceptance event for NPC shared index %d, user shared index %d
        //     (quest %d), event not dispatched.", npcSharedIndex, playerSharedIndex, questId)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the skill-quest acceptance event for NPC shared index %d, user shared index %d (quest "
            "%d); event not dispatched.",
            npcSharedIndex, playerSharedIndex, questId
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool SeeItem(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    packet = ::Disassemble(packet, "d", &npcSharedIndex);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        return false;
    }

    int count;
    packet = ::Disassemble(packet, "d", &count);

    // Shipping SeeItem constructs this exact typed list on its stack. The
    // event borrows it only for the synchronous virtual HandleEvent call.
    ItemIndexList visibleItems;
    for (int index = 0; index < count; ++index) {
        int itemIndex;
        packet = ::Disassemble(packet, "d", &itemIndex);
        if (itemIndex < 0) {
            continue;
        }
        const CSharedItemData* const item = CItemFactory::Inst().GetData(itemIndex);
        if (item && item->m_bIsActive) {
            visibleItems.AddItem(itemIndex);
        }
    }
    if (visibleItems.GetSize() <= 0) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateSeeItemEvent(npc, &visibleItems);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x554)
        // Log.Add(LOG_ERROR,
        //     "[visibility] Cannot create the visible-item event for NPC shared index %d from %d reported entries. Event not
        //     dispatched.", npcSharedIndex, count)
        L2_LOG_ERROR(
            Log,
            "[visibility] Cannot create the visible-item event for NPC shared index %d from %d reported entries; event not "
            "dispatched.",
            npcSharedIndex, count
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ClassChangeRequest(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignoredObjectId;
    int playerSharedIndex;
    int npcSharedIndex;
    int occupationNameId;
    ::Disassemble(packet, "dddd", &ignoredObjectId, &playerSharedIndex, &npcSharedIndex, &occupationNameId);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in ClassChangeRequestPacket",
        //     L"serversocket.cpp",
        //     0x579)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the class-change request for NPC shared index %d, user shared index %d (class ID
        //     %d): %s.", npcSharedIndex, playerSharedIndex, occupationNameId, !npc ? "NPC not found" : !player ? "user
        //     shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the class-change request for NPC shared index %d, user shared index %d (class ID "
            "%d): %s.",
            npcSharedIndex, playerSharedIndex, occupationNameId,
            !npc          ? "NPC not found"
                : !player ? "user shared data unavailable"
                          : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateClassChangeRequestedEvent(npc, player, occupationNameId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x574)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the class-change request event for NPC shared index %d, user shared index %d (class
        //     ID %d), event not dispatched.", npcSharedIndex, playerSharedIndex, occupationNameId)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the class-change request event for NPC shared index %d, user shared index %d (class "
            "ID %d); event not dispatched.",
            npcSharedIndex, playerSharedIndex, occupationNameId
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool TimePacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcSharedIndex;
    int time;
    ::Disassemble(packet, "dd", &npcSharedIndex, &time);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in TimePacket",
        //     L"serversocket.cpp",
        //     0x59b)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot deliver time notification %d to NPC shared index %d: NPC not found.",
        //     time, npcSharedIndex)
        L2_LOG_WARN(
            Log, "[AI event] Cannot deliver time notification %d to NPC shared index %d: NPC not found.", time,
            npcSharedIndex
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTimeEvent(npc, time);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x596)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create time notification %d for NPC shared index %d. Event not dispatched.",
        //     time, npcSharedIndex)
        L2_LOG_ERROR(
            Log, "[AI event] Cannot create time notification %d for NPC shared index %d; event not dispatched.", time,
            npcSharedIndex
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DeleteNPCPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int objectId;
    int ignored;
    ::Disassemble(packet, "dd", &objectId, &ignored);

    CNPC* const npc = CNPC::GetObjectW(objectId);
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in LeaveWorldPacket npc "
        //     L"classid [%d]",
        //     L"serversocket.cpp",
        //     0x5b0,
        //     objectId)
        // Log.Add(LOG_WARN,
        //     "[NPC removal] Ignoring deletion of object %u: NPC not found. The population count remains unchanged.",
        //     static_cast<unsigned int>(objectId))
        L2_LOG_WARN(
            Log, "[NPC removal] Ignoring deletion of object %u: NPC not found; the population count remains unchanged.",
            static_cast<unsigned int>(objectId)
        );
        return false;
    }

    // Shipping order: virtual Delete first, then the independent global
    // population-count decrement.
    npc->Delete();
    runtime::raw_atomic::DecrementI32(&g_nNPCCount);
    return false;
}

bool FinishUseSkill(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int targetObjectId;
    std::uint64_t skillNameId;
    int well;
    ::Disassemble(packet, "ddQd", &npcObjectId, &targetObjectId, &skillNameId, &well);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->UseSkillFinished(static_cast<std::uint32_t>(targetObjectId), SignedI64Bits(skillNameId), well != 0);
    }
    return false;
}

bool ClanAttack(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int attackedObjectId;
    int attackedSharedIndex;
    int attackerTaggedId;
    int attackedParam;
    int damage;
    packet =
        ::Disassemble(packet, "ddddd", &attackedObjectId, &attackedSharedIndex, &attackerTaggedId, &attackedParam, &damage);

    CSharedCreatureData* const attacked = CCreatureFactory::Inst().GetData(attackedSharedIndex);
    if (CCreatureFactory::Inst().GetIdByIndex(attackedSharedIndex) != attackedObjectId) {
        return false;
    }

    const std::uint32_t tagged = static_cast<std::uint32_t>(attackerTaggedId);
    // const std::uint32_t type = tagged >> 27U
    const std::uint32_t type = tagged >> CLASS_SHIFT;

    // if (type == 9U) {
    if (type == SCLS_CREATURE) {
        CCreature* const creatureAttackerObject = CCreature::GetObjectW(static_cast<std::uint32_t>(attackerTaggedId));
        if (!creatureAttackerObject) {
            return false;
        }
        CSharedCreatureData* const creatureAttacker = creatureAttackerObject->m_data;

        int distantRecipients = 0;
        for (int remaining = 0x4fe8; remaining != 0; --remaining) {
            int recipientSharedIndex;
            packet = ::Disassemble(packet, "d", &recipientSharedIndex);
            if (recipientSharedIndex == -1) {
                break;
            }

            CNPC* const recipient = ResolveMappedNpc(recipientSharedIndex);
            if (!recipient) {
                continue;
            }

            const double dy = attacked->m_Pos.y - recipient->m_data->m_Pos.y;
            const double dx = attacked->m_Pos.x - recipient->m_data->m_Pos.x;
            if (dx * dx + dy * dy > 25000000.0) {
                // Noise: Per-recipient names were only needed by the original per-recipient log below.
                // const std::wstring recipientName =
                //     Utf16ToWide(
                //         recipient->m_data->Name())
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] too far clan help message npc[%s]",
                //     L"serversocket.cpp",
                //     0x666,
                //     recipientName.c_str())
                // Noise: Summarize distant recipients once per packet instead of emitting one record per NPC.
                ++distantRecipients;
            }

            recipient->ClanAttacked(attacked, creatureAttacker, attackedParam, 0, damage);
        }
        if (distantRecipients != 0) {
            // Log.Add(LOG_WARN,
            //     "[NPC assistance] Clan-attack notification for object %u from attacker %u included %d recipients more than
            //     5000 units away, distance does not block notification.", static_cast<unsigned int>(attackedObjectId),
            //     static_cast<unsigned int>(attackerTaggedId), distantRecipients)
            L2_LOG_WARN(
                Log,
                "[NPC assistance] Clan-attack notification for object %u from attacker %u included %d recipients more than "
                "5000 units away; distance does not block notification.",
                static_cast<unsigned int>(attackedObjectId), static_cast<unsigned int>(attackerTaggedId), distantRecipients
            );
        }
        return false;
    }

    // if (type == 11U) {
    if (type == SCLS_STATIC_OBJECT) {
        const int index = static_cast<int>(tagged & 0x000fffffU);
        if (index >= SharedDefine_StaticObject.max_data) {
            return false;
        }
        CSharedStaticObjectData* const staticAttacker = CStaticObjectFactory::Inst().GetData(index);
        if (!staticAttacker) {
            return false;
        }

        int distantRecipients = 0;
        for (int remaining = 0x4fe8; remaining != 0; --remaining) {
            int recipientSharedIndex;
            packet = ::Disassemble(packet, "d", &recipientSharedIndex);
            if (recipientSharedIndex == -1) {
                break;
            }

            CNPC* const recipient = ResolveMappedNpc(recipientSharedIndex);
            if (!recipient) {
                continue;
            }

            const double dy = attacked->m_Pos.y - recipient->m_data->m_Pos.y;
            const double dx = attacked->m_Pos.x - recipient->m_data->m_Pos.x;
            if (dx * dx + dy * dy > 25000000.0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] too far clan help message",
                //     L"serversocket.cpp",
                //     0x67b)
                // Noise: Summarize distant recipients once per packet instead of emitting one record per NPC.
                ++distantRecipients;
            }

            recipient->StaticObjectClanAttacked(attacked, staticAttacker, attackedParam, 0, damage);
        }
        if (distantRecipients != 0) {
            // Log.Add(LOG_WARN,
            //     "[NPC assistance] Clan-attack notification for object %u from static attacker %u included %d recipients
            //     more than 5000 units away, distance does not block notification.", static_cast<unsigned
            //     int>(attackedObjectId), static_cast<unsigned int>(attackerTaggedId), distantRecipients)
            L2_LOG_WARN(
                Log,
                "[NPC assistance] Clan-attack notification for object %u from static attacker %u included %d recipients "
                "more than 5000 units away; distance does not block notification.",
                static_cast<unsigned int>(attackedObjectId), static_cast<unsigned int>(attackerTaggedId), distantRecipients
            );
        }
    }
    return false;
}

bool ClanSpelled(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int casterObjectId;
    int casterSharedIndex;
    int targetTaggedId;
    int skillId;
    int skillLevel;
    int skillSubLevel;
    packet = ::Disassemble(
        packet, "dddddd", &casterObjectId, &casterSharedIndex, &targetTaggedId, &skillId, &skillLevel, &skillSubLevel
    );

    CSharedCreatureData* const caster = CCreatureFactory::Inst().GetData(casterSharedIndex);
    if (CCreatureFactory::Inst().GetIdByIndex(casterSharedIndex) != casterObjectId) {
        return false;
    }

    const std::uint32_t tagged = static_cast<std::uint32_t>(targetTaggedId);
    // const std::uint32_t type = tagged >> 27U
    // if (type == 9U) {
    const std::uint32_t type = tagged >> CLASS_SHIFT;
    if (type == SCLS_CREATURE) {
        CSharedCreatureData* const creatureTarget = CCreatureFactory::Inst().GetData(static_cast<int>(tagged & 0x000fffffU));

        for (int remaining = 0x4fe8; remaining != 0; --remaining) {
            int recipientSharedIndex;
            packet = ::Disassemble(packet, "d", &recipientSharedIndex);
            if (recipientSharedIndex == -1) {
                break;
            }

            CNPC* const recipient = ResolveMappedNpc(recipientSharedIndex);
            if (!recipient) {
                continue;
            }
            recipient->ClanSpelled(caster, creatureTarget, skillId, skillLevel, skillSubLevel);
        }
        return false;
    }

    // if (type == 11U) {
    if (type == SCLS_STATIC_OBJECT) {
        const int index = static_cast<int>(tagged & 0x000fffffU);
        CSharedStaticObjectData* staticTarget = nullptr;
        if (index < SharedDefine_StaticObject.max_data) {
            staticTarget = CStaticObjectFactory::Inst().GetData(index);
        }

        for (int remaining = 0x4fe8; remaining != 0; --remaining) {
            int recipientSharedIndex;
            packet = ::Disassemble(packet, "d", &recipientSharedIndex);
            if (recipientSharedIndex == -1) {
                break;
            }

            CNPC* const recipient = ResolveMappedNpc(recipientSharedIndex);
            if (!recipient) {
                continue;
            }
            recipient->StaticObjectClanSpelled(caster, staticTarget, skillId, skillLevel, skillSubLevel);
        }
    }
    return false;
}

bool ClanDiedEvent(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int victimObjectId;
    int victimSharedIndex;
    packet = ::Disassemble(packet, "dd", &victimObjectId, &victimSharedIndex);

    if (CCreatureFactory::Inst().GetIdByIndex(victimSharedIndex) != victimObjectId) {
        return false;
    }
    CSharedCreatureData* const victim = CCreatureFactory::Inst().GetData(victimSharedIndex);
    if (!victim) {
        return false;
    }

    const auto victimPosition = victim->GetPos();
    int distantRecipients = 0;
    for (int remaining = 0x13f8; remaining != 0; --remaining) {
        int recipientObjectId = 0;
        packet = ::Disassemble(packet, "d", &recipientObjectId);
        if (recipientObjectId == 0) {
            break;
        }

        CNPC* const recipient = CNPC::GetObjectW(recipientObjectId);
        if (!recipient) {
            continue;
        }

        const auto recipientPosition = recipient->m_data->GetPos();
        const double dx = victimPosition.x - recipientPosition.x;
        const double dy = victimPosition.y - recipientPosition.y;
        if (dx * dx + dy * dy > 25000000.0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] too far clan help message",
            //     L"serversocket.cpp",
            //     0x6b3)
            // Noise: Summarize distant recipients once per packet instead of emitting one record per NPC.
            ++distantRecipients;
        }
        recipient->ClanDied(victim);
    }
    if (distantRecipients != 0) {
        // Log.Add(LOG_WARN,
        //     "[NPC assistance] Clan-death notification for object %u included %d recipients more than 5000 units away.
        //     distance does not block notification.", static_cast<unsigned int>(victimObjectId), distantRecipients)
        L2_LOG_WARN(
            Log,
            "[NPC assistance] Clan-death notification for object %u included %d recipients more than 5000 units away; "
            "distance does not block notification.",
            static_cast<unsigned int>(victimObjectId), distantRecipients
        );
    }
    return false;
}

void CTestMessage::OnReceive(int) {
    std::array<std::uint8_t, 0x4ff8> arbitraryBytes;
    for (int remaining = 0x15e; remaining != 0; --remaining) {
        const int byteCount = (LegacyCrtRand() & 1) + 1;
        g_ns.Send("cdddb", 0, -1, -1, -1, byteCount, arbitraryBytes.data());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (LegacyCrtRand() % 100 == 0) {
        // Noise: Random socket-test chatter has no diagnostic content. SetAI reports test startup. Keep the random draw to
        // preserve the test sequence. Log.Add(
        //     LOG_NORMAL,
        //     "ahehe")
    }

    (new CTestMessage())->Post(0);
}

bool SetAI(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId;
    int ignoredSharedIndex;
    std::array<char16_t, 200 / sizeof(char16_t)> aiName;
    ::Disassemble(packet, "ddS", &npcObjectId, &ignoredSharedIndex, 200, aiName.data());

    if (npcObjectId == -1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Socket test start!!!")
        // Log.Add(LOG_NORMAL,
        //     "[diagnostic] Starting 3 repeating socket-test tasks at L2Server's request (350 frames and a 5 ms pause per
        //     task cycle).")
        L2_LOG_INFO(
            Log,
            "[diagnostic] Starting 3 repeating socket-test tasks at L2Server's request (350 frames and a 5 ms pause per "
            "task cycle)."
        );
        for (int remaining = 3; remaining != 0; --remaining) {
            (new CTestMessage())->Post(0);
        }
        return false;
    }

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (npc) {
        npc->SetAI(aiName.data());
    }
    return false;
}

bool TimeHour(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int hour;
    int minute;
    int isNight;
    ::Disassemble(packet, "ddd", &hour, &minute, &isNight);
    g_L2TimeInfo.mHour = hour;
    g_L2TimeInfo.mMin = minute;
    g_L2TimeInfo.mIsNight = isNight;
    return false;
}

bool DesireManipulation(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int npcSharedIndex;
    int actorObjectId;
    int actorSharedIndex;
    int argument;
    double value;
    ::Disassemble(packet, "dddddf", &ignored, &npcSharedIndex, &actorObjectId, &actorSharedIndex, &argument, &value);

    const int npcObjectId = CCreatureFactory::Inst().GetIdByIndex(npcSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    return npc ? npc->OnPacketDesireManipulation(actorObjectId, actorSharedIndex, argument, value) : false;
}

bool CreatePledge(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int userSharedIndex;
    int npcSharedIndex;
    int reply;
    ::Disassemble(packet, "dddd", &ignored, &userSharedIndex, &npcSharedIndex, &reply);

    const int npcObjectId = CCreatureFactory::Inst().GetIdByIndex(npcSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in CreatePledgePacket",
        //     L"serversocket.cpp",
        //     0x76b)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the clan creation reply for NPC shared index %d, user shared index %d (reply %d):
        //     %s.", npcSharedIndex, userSharedIndex, reply, !npc ? "NPC not found" : !user ? "user shared data unavailable"
        //     : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the clan creation reply for NPC shared index %d, user shared index %d (reply %d): "
            "%s.",
            npcSharedIndex, userSharedIndex, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCreatePledgeEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x766)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan creation reply event for NPC shared index %d, user shared index %d (reply
        //     %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan creation reply event for NPC shared index %d, user shared index %d (reply "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool DismissPledge(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int userSharedIndex;
    int npcSharedIndex;
    int reply;
    ::Disassemble(packet, "dddd", &ignored, &userSharedIndex, &npcSharedIndex, &reply);

    const int npcObjectId = CCreatureFactory::Inst().GetIdByIndex(npcSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in DismissPledgePacket",
        //     L"serversocket.cpp",
        //     0x81f)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the clan dissolution reply for NPC shared index %d, user shared index %d (reply
        //     %d): %s.", npcSharedIndex, userSharedIndex, reply, !npc ? "NPC not found" : !user ? "user shared data
        //     unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the clan dissolution reply for NPC shared index %d, user shared index %d (reply %d): "
            "%s.",
            npcSharedIndex, userSharedIndex, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateDismissPledgeEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x81a)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan dissolution reply event for NPC shared index %d, user shared index %d
        //     (reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan dissolution reply event for NPC shared index %d, user shared index %d (reply "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool RevivePledge(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int userSharedIndex;
    int npcSharedIndex;
    int reply;
    ::Disassemble(packet, "dddd", &ignored, &userSharedIndex, &npcSharedIndex, &reply);

    const int npcObjectId = CCreatureFactory::Inst().GetIdByIndex(npcSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in RevivePledgePacket",
        //     L"serversocket.cpp",
        //     0x842)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the clan revival reply for NPC shared index %d, user shared index %d (reply %d):
        //     %s.", npcSharedIndex, userSharedIndex, reply, !npc ? "NPC not found" : !user ? "user shared data unavailable"
        //     : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the clan revival reply for NPC shared index %d, user shared index %d (reply %d): %s.",
            npcSharedIndex, userSharedIndex, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateRevivePledgeEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x83d)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the clan revival reply event for NPC shared index %d, user shared index %d (reply
        //     %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the clan revival reply event for NPC shared index %d, user shared index %d (reply "
            "%d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool CreateAlliance(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int ignored;
    int userSharedIndex;
    int npcSharedIndex;
    int reply;
    ::Disassemble(packet, "dddd", &ignored, &userSharedIndex, &npcSharedIndex, &reply);

    const int npcObjectId = CCreatureFactory::Inst().GetIdByIndex(npcSharedIndex);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal in CreateAlliancePacket",
        //     L"serversocket.cpp",
        //     0xb8e)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the alliance creation reply for NPC shared index %d, user shared index %d (reply
        //     %d): %s.", npcSharedIndex, userSharedIndex, reply, !npc ? "NPC not found" : !user ? "user shared data
        //     unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the alliance creation reply for NPC shared index %d, user shared index %d (reply "
            "%d): %s.",
            npcSharedIndex, userSharedIndex, reply,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateCreateAllianceEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0xb89)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the alliance creation reply event for NPC shared index %d, user shared index %d
        //     (reply %d), event not dispatched.", npcSharedIndex, userSharedIndex, reply)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the alliance creation reply event for NPC shared index %d, user shared index %d "
            "(reply %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, reply
        );
        return false;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool LevelUpPledge(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    int userSharedIndex;
    int npcObjectId;
    int reply;
    ::Disassemble(packet, "dddd", &userObjectId, &userSharedIndex, &npcObjectId, &reply);

    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    User* const userObject = User::GetObjectW(userObjectId);
    if (!npc || !userObject) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;
    if (user->index != userSharedIndex) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid talker index. "
        //     L"server index[%d], npc index[%d]",
        //     L"serversocket.cpp",
        //     0x85b,
        //     userSharedIndex,
        //     user->index)
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot dispatch the level-up reply for NPC %u, user %u (reply %d): user shared index from L2Server is
        //     %d, locally observed index is %d.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned
        //     int>(userObjectId), reply, userSharedIndex, user->index)
        L2_LOG_ERROR(
            Log,
            "[clan] Cannot dispatch the level-up reply for NPC %u, user %u (reply %d): user shared index from L2Server is "
            "%d, locally observed index is %d.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply, userSharedIndex,
            user->index
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateLevelUpPledgeEvent(npc, user, reply);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x874)
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot create the level-up reply for NPC %u, user %u (reply %d). Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply)
        L2_LOG_ERROR(
            Log, "[clan] Cannot create the level-up reply for NPC %u, user %u (reply %d); event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId), reply
        );
        return false;
    }

    int expectedEventHandling = 0;
    if (userObject->m_nEventHandling.compare_exchange_strong(expectedEventHandling, 1)) {
        npc->HandleEvent(event);
        userObject->m_nEventHandling.exchange(0);
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool ManorMenuSelectEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userSharedIndex;
    int npcSharedIndex;
    int ask;
    int state;
    int time;
    ::Disassemble(packet, "ddddd", &userSharedIndex, &npcSharedIndex, &ask, &state, &time);

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const user = CCreatureFactory::Inst().GetData(userSharedIndex);
    const int userObjectId = CCreatureFactory::Inst().GetIdByIndex(userSharedIndex);
    User* const userObject = User::GetObjectW(userObjectId);
    if (!userObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user",
        //     L"serversocket.cpp",
        //     0x361)
        // Log.Add(LOG_WARN,
        //     "[manor] Cannot process menu selection for NPC shared index %d, user shared index %d (object %u, ask %d, state
        //     %d, time %d): user object not found.", npcSharedIndex, userSharedIndex, static_cast<unsigned
        //     int>(userObjectId), ask, state, time)
        L2_LOG_WARN(
            Log,
            "[manor] Cannot process menu selection for NPC shared index %d, user shared index %d (object %u, ask %d, state "
            "%d, time %d): user object not found.",
            npcSharedIndex, userSharedIndex, static_cast<unsigned int>(userObjectId), ask, state, time
        );
        return false;
    }
    if (!npc || !user || !user->m_bIsActive || !user->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal "
        //     L"in ManorMenuSelectEventPacket",
        //     L"serversocket.cpp",
        //     0x37c)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the manor menu selection for NPC shared index %d, user shared index %d (ask %d,
        //     state %d, time %d): %s.", npcSharedIndex, userSharedIndex, ask, state, time, !npc ? "NPC not found" : !user ?
        //     "user shared data unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the manor menu selection for NPC shared index %d, user shared index %d (ask %d, "
            "state %d, time %d): %s.",
            npcSharedIndex, userSharedIndex, ask, state, time,
            !npc        ? "NPC not found"
                : !user ? "user shared data unavailable"
                        : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateManorMenuSelectedEvent(npc, user, ask, state, time);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x377)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the manor menu selection event for NPC shared index %d, user shared index %d (ask
        //     %d, state %d, time %d), event not dispatched.", npcSharedIndex, userSharedIndex, ask, state, time)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the manor menu selection event for NPC shared index %d, user shared index %d (ask %d, "
            "state %d, time %d); event not dispatched.",
            npcSharedIndex, userSharedIndex, ask, state, time
        );
        return false;
    }

    int expectedEventHandling = 0;
    if (userObject->m_nEventHandling.compare_exchange_strong(expectedEventHandling, 1)) {
        npc->HandleEvent(event);
        userObject->m_nEventHandling.exchange(0);
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool TalkPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    CAutoStartTimer timer(SERVERSOCKET__TALK_PACKET);
    int userObjectId;
    int userSharedIndex;
    int npcObjectId;
    ::Disassemble(packet, "ddd", &userObjectId, &userSharedIndex, &npcObjectId);

    User* const userObject = User::GetObjectW(userObjectId);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!userObject || !npc) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;
    if (user->index != userSharedIndex || !user->m_bIsActive || !user->m_bIsAlive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateTalkedEvent(npc, user, 0, 0);
    const int npcClassId = npc->m_data->m_nClassID;
    if (!event) {
        if (npcClassId != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]Can't talk with [%d]",
            //     L"serversocket.cpp",
            //     0x224,
            //     npcClassId)
            // Log.Add(LOG_ERROR,
            //     "[dialog] Cannot create a talk event for NPC %u (class %d), user %u. Dialog event not dispatched.",
            //     static_cast<unsigned int>(npcObjectId), npcClassId, static_cast<unsigned int>(userObjectId))
            L2_LOG_ERROR(
                Log, "[dialog] Cannot create a talk event for NPC %u (class %d), user %u; dialog event not dispatched.",
                static_cast<unsigned int>(npcObjectId), npcClassId, static_cast<unsigned int>(userObjectId)
            );
        }
        return false;
    }

    int expectedEventHandling = 0;
    if (userObject->m_nEventHandling.compare_exchange_strong(expectedEventHandling, 1)) {
        npc->HandleEvent(event);
        userObject->m_nEventHandling.exchange(0);
    } else {
        if (npcClassId != 0) {
            // Noise: A repeated talk request while another user event is being handled is expected. The busy guard already
            // skips it. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]Can't talk with [%d]",
            //     L"serversocket.cpp",
            //     0x218,
            //     npcClassId)
        }
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool QuestAcceptedEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int playerSharedIndex;
    int npcSharedIndex;
    int questId;
    ::Disassemble(packet, "ddd", &playerSharedIndex, &npcSharedIndex, &questId);

    const int userObjectId = CCreatureFactory::Inst().GetIdByIndex(playerSharedIndex);
    User* const userObject = User::GetObjectW(userObjectId);
    if (!userObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user",
        //     L"serversocket.cpp",
        //     0x2a0)
        // Log.Add(LOG_WARN,
        //     "[quest] Cannot process acceptance of quest %d for NPC shared index %d, user shared index %d (object %u): user
        //     object not found.", questId, npcSharedIndex, playerSharedIndex, static_cast<unsigned int>(userObjectId))
        L2_LOG_WARN(
            Log,
            "[quest] Cannot process acceptance of quest %d for NPC shared index %d, user shared index %d (object %u): user "
            "object not found.",
            questId, npcSharedIndex, playerSharedIndex, static_cast<unsigned int>(userObjectId)
        );
        return false;
    }

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal "
        //     L"in QuestAcceptedEventPacket",
        //     L"serversocket.cpp",
        //     0x2bd)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the quest acceptance for NPC shared index %d, user shared index %d (quest %d):
        //     %s.", npcSharedIndex, playerSharedIndex, questId, !npc ? "NPC not found" : !player ? "user shared data
        //     unavailable" : "user was inactive or not a player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the quest acceptance for NPC shared index %d, user shared index %d (quest %d): %s.",
            npcSharedIndex, playerSharedIndex, questId,
            !npc          ? "NPC not found"
                : !player ? "user shared data unavailable"
                          : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateQuestAcceptedEvent(npc, player, questId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x2b7)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the quest acceptance event for NPC shared index %d, user shared index %d (quest %d)
        //     event not dispatched.", npcSharedIndex, playerSharedIndex, questId)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the quest acceptance event for NPC shared index %d, user shared index %d (quest %d); "
            "event not dispatched.",
            npcSharedIndex, playerSharedIndex, questId
        );
        return false;
    }

    int expectedEventHandling = 0;
    if (userObject->m_nEventHandling.compare_exchange_strong(expectedEventHandling, 1)) {
        npc->HandleEvent(event);
        userObject->m_nEventHandling.exchange(0);
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool MenuSelectEventPacket(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int playerSharedIndex;
    int lockOwnerObjectId;
    int npcSharedIndex;
    int ask;
    std::uint64_t reply;
    int state;
    ::Disassemble(packet, "ddddQd", &playerSharedIndex, &lockOwnerObjectId, &npcSharedIndex, &ask, &reply, &state);

    User* const lockOwnerObject = User::GetObjectW(lockOwnerObjectId);
    if (!lockOwnerObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user",
        //     L"serversocket.cpp",
        //     0x32a)
        // Log.Add(LOG_WARN,
        //     "[dialog] Cannot process menu selection for NPC shared index %d, user shared index %d (ask %d, reply %lld,
        //     state %d): event-lock owner user %u was not found.", npcSharedIndex, playerSharedIndex, ask, static_cast<long
        //     long>(SignedI64Bits(reply)), state, static_cast<unsigned int>(lockOwnerObjectId))
        L2_LOG_WARN(
            Log,
            "[dialog] Cannot process menu selection for NPC shared index %d, user shared index %d (ask %d, reply %lld, "
            "state %d): event-lock owner user %u was not found.",
            npcSharedIndex, playerSharedIndex, ask, static_cast<long long>(SignedI64Bits(reply)), state,
            static_cast<unsigned int>(lockOwnerObjectId)
        );
        return false;
    }

    CNPC* const npc = ResolveMappedNpc(npcSharedIndex);
    CSharedCreatureData* const player = CCreatureFactory::Inst().GetData(playerSharedIndex);
    if (!npc || !player || !player->m_bIsActive || !player->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something is abnormal "
        //     L"in MenuSelectEventPacket",
        //     L"serversocket.cpp",
        //     0x34c)
        // Log.Add(LOG_WARN,
        //     "[AI event] Cannot process the menu selection for NPC shared index %d, user shared index %d (ask %d, reply
        //     %lld, state %d): %s.", npcSharedIndex, playerSharedIndex, ask, static_cast<long long>(SignedI64Bits(reply)),
        //     state, !npc ? "NPC not found" : !player ? "user shared data unavailable" : "user was inactive or not a
        //     player")
        L2_LOG_WARN(
            Log,
            "[AI event] Cannot process the menu selection for NPC shared index %d, user shared index %d (ask %d, reply "
            "%lld, state %d): %s.",
            npcSharedIndex, playerSharedIndex, ask, static_cast<long long>(SignedI64Bits(reply)), state,
            !npc          ? "NPC not found"
                : !player ? "user shared data unavailable"
                          : "user was inactive or not a player"
        );
        return false;
    }
    if (npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* const event =
        CNPCEventFactory::CreateMenuSelectedEvent(npc, player, ask, static_cast<std::int64_t>(reply), state, 0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x346)
        // Log.Add(LOG_ERROR,
        //     "[AI event] Cannot create the menu selection event for NPC shared index %d, user shared index %d (ask %d,
        //     reply %lld, state %d), event not dispatched.", npcSharedIndex, playerSharedIndex, ask, static_cast<long
        //     long>(SignedI64Bits(reply)), state)
        L2_LOG_ERROR(
            Log,
            "[AI event] Cannot create the menu selection event for NPC shared index %d, user shared index %d (ask %d, reply "
            "%lld, state %d); event not dispatched.",
            npcSharedIndex, playerSharedIndex, ask, static_cast<long long>(SignedI64Bits(reply)), state
        );
        return false;
    }

    int expectedEventHandling = 0;
    if (lockOwnerObject->m_nEventHandling.compare_exchange_strong(expectedEventHandling, 1)) {
        npc->HandleEvent(event);
        lockOwnerObject->m_nEventHandling.exchange(0);
    }
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool QuestChoice(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    int userSharedIndex;
    int npcObjectId;
    int code;
    int choiceMode;
    ::Disassemble(packet, "ddddd", &userObjectId, &userSharedIndex, &npcObjectId, &code, &choiceMode);

    User* const userObject = User::GetObjectW(userObjectId);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!userObject || !npc) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;
    if (user->index != userSharedIndex || !user->m_bIsActive || !user->m_bIsAlive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* event = nullptr;
    // The kept legacy diagnostic uses this source-line selector.
    [[maybe_unused]] int releaseLine = 0;
    // int failureLine = 0
    [[maybe_unused]] int failureLine = 0;
    if (choiceMode == 0) {
        event = CNPCEventFactory::CreateTalkedEvent(npc, user, 1, code);
        releaseLine = 0x400;
        failureLine = 0x404;
    } else {
        event = CNPCEventFactory::CreateTalkSelectedEvent(npc, user, 1, code);
        releaseLine = 0x40d;
        failureLine = 0x411;
    }
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     failureLine)
        // Log.Add(LOG_ERROR,
        //     "[dialog] Cannot create the %s event for NPC %u, user %u (code %d, choice mode %d). Event not dispatched.",
        //     choiceMode == 0 ? "quest talk" : "quest selection", static_cast<unsigned int>(npcObjectId),
        //     static_cast<unsigned int>(userObjectId), code, choiceMode)
        L2_LOG_ERROR(
            Log, "[dialog] Cannot create the %s event for NPC %u, user %u (code %d, choice mode %d); event not dispatched.",
            choiceMode == 0 ? "quest talk" : "quest selection", static_cast<unsigned int>(npcObjectId),
            static_cast<unsigned int>(userObjectId), code, choiceMode
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

bool TalkSelect(CSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int userObjectId;
    int userSharedIndex;
    int npcObjectId;
    ::Disassemble(packet, "ddd", &userObjectId, &userSharedIndex, &npcObjectId);

    User* const userObject = User::GetObjectW(userObjectId);
    CNPC* const npc = CNPC::GetObjectW(npcObjectId);
    if (!userObject || !npc) {
        return false;
    }
    CSharedCreatureData* const user = userObject->m_data;
    if (user->index != userSharedIndex || !user->m_bIsActive || !user->m_bIsAlive || npc->m_data->m_bNotFunctionable) {
        return false;
    }

    CNPCEvent* event = CNPCEventFactory::CreateTalkSelectedEvent(npc, user, 0, 0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     L"serversocket.cpp",
        //     0x638)
        // Log.Add(LOG_ERROR,
        //     "[dialog] Cannot create the talk-selection event for NPC %u, user %u. Event not dispatched.",
        //     static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId))
        L2_LOG_ERROR(
            Log, "[dialog] Cannot create the talk-selection event for NPC %u, user %u; event not dispatched.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(userObjectId)
        );
        return false;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    return false;
}

} // namespace l2npc
