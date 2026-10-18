#include "Raid.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"

#include "Alliance.h"
#include "Auth.h"
#include "DB.h"
#include "L2Packet.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "Relay.h"
#include "User.h"
#include "UserSocket.h"
#include "XmlLoader.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <limits>
#include <new>
#include <string>
#include <thread>

#if !defined(_WIN32)
extern long timezone;
#endif

namespace {

bool EqualsAsciiCaseInsensitive(const std::u16string& left, const char16_t* const right) {
    const std::u16string_view leftView = runtime::StringView(left);
    const std::u16string_view rightView = right ? std::u16string_view(right) : std::u16string_view();
    if (leftView.size() != rightView.size()) {
        return false;
    }
    for (std::size_t index = 0; index < leftView.size(); ++index) {
        char16_t leftCharacter = leftView[index];
        char16_t rightCharacter = rightView[index];
        if (leftCharacter >= u'A' && leftCharacter <= u'Z') {
            leftCharacter = static_cast<char16_t>(leftCharacter - u'A' + u'a');
        }
        if (rightCharacter >= u'A' && rightCharacter <= u'Z') {
            rightCharacter = static_cast<char16_t>(rightCharacter - u'A' + u'a');
        }
        if (leftCharacter != rightCharacter) {
            return false;
        }
    }
    return true;
}

} // namespace

class CLeaveWorldAllRaidUser final : public CObjectMapFunc {
public:
    bool Do(MemoryObject* object) override;
};


bool CLeaveWorldAllRaidUser::Do(MemoryObject* const object) {
    User* const user = static_cast<User*>(object);
    user->GetSocket()->ForceClose(SCR_RAID_QUIT);
    return true;
}

namespace {

void RaidClientThreadCommon() {
    for (;;) {
        RaidClientManager::inst().Init();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

std::uint32_t RaidClientThread(void*) {
    CThreadManager::RegisterThisThread(ThreadID::RAID);
    InitializeCurrentIOBufferPool(0x80);

    if (ExceptionMailing()) {
        // Log.Add(LOG_NORMAL, L"RaidClient in mailing mode")
        // L2_LOG_INFO(Log, L"RaidClient in mailing mode")
        L2_LOG_INFO(Log, "Raid client worker started with exception reporting enabled.");
        try {
            RaidClientThreadCommon();
        } catch (...) {
            runtime::process::HandleUnhandledSharedThreadException("Raid.cpp", 0x487, "Thu May 10 05:00:26 2018");
        }
    } else {
        RaidClientThreadCommon();
    }
    return 0;
}

} // namespace

RaidAllowAction* RaidAllowAction::m_AllowAction = nullptr;

RaidEnterVerifier::RaidEnterVerifier(CUserSocket* const socket) : m_pUserSocket(socket) {
    // Current L2Server 0x00a14d78.
    L2_ADD_REF(m_pUserSocket, ObjectRefType::VerifierRetain);
    AddTimer(30000, 0);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void RaidEnterVerifier::TimerExpired(int) {
    // Current L2Server 0x00a14e00.
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pUserSocket, ObjectRefType::VerifierRetain);
}

RaidSocket::RaidSocket(const std::uint64_t socket) :
    CSocket(socket, 0x400), m_cMaxProtocolNo(0), m_alignment109To10c{}, m_nWorldId(0) {}

RaidSocket::~RaidSocket() = default;

bool RaidSocket::CheckPendingWrite() {
    // Current L2Server 0x00a14ebc.
    L2_GUARD_FUNCTION();
    if (m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid RaidSocket.",
        //     L"Raid.cpp",
        //     0x57)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Write to invalid RaidSocket.",
        //     L"Raid.cpp",
        //     0x57)
        L2_LOG_WARN(Log, "Raid write rejected: connection for world %d is closed.", m_nWorldId);
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"RaidSocket pending write > %d KB, Close!",
    //     0x2000)
    // L2_LOG_ERROR(Log,
    //     L"RaidSocket pending write > %d KB, Close!",
    //     0x2000)
    L2_LOG_ERROR(Log, "Raid write rejected for world %d: pending output exceeds 8192 KiB.", m_nWorldId);
    ReportCallStack(LOG_ERROR, false);
    return false;
}

void RaidSocket::OnClose() {
    // Current L2Server 0x00a14fc4.
    L2_GUARD_FUNCTION();
    m_Mode = SM_CLOSE;
}

void RaidSocket::OnCreate() {
    // Current L2Server 0x00a177c8.
    L2_GUARD_FUNCTION();
    Read(0);
}

void RaidSocket::OnRead() {
    // Current L2Server 0x00a1786c.
    L2_GUARD_FUNCTION();
    if (m_Mode == SM_CLOSE) {
        Close();
        return;
    }

    const int readSize = static_cast<int>(m_pReadBuf->m_dwSize);
    int packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_Mode == SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }
            const unsigned char* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_nPacketLen = static_cast<int>(header[1]) * 0x100 - 2 + static_cast<int>(header[0]);
            if (static_cast<std::uint32_t>(m_nPacketLen - 1) > 0x4ffdU) {
                // Log.Add(
                //     LOG_ERROR,
                //     "RaidSocket[%x]: bad packet size %d, worldID[%d]",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen,
                //     m_nWorldId)
                // L2_LOG_ERROR(Log,
                //     "RaidSocket[%x]: bad packet size %d, worldID[%d]",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen,
                //     m_nWorldId)
                L2_LOG_ERROR(
                    Log,
                    "Closing raid connection for world %d: packet body size %d is outside 1..20478 bytes (socket %llu).",
                    m_nWorldId, m_nPacketLen, static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
                );
                m_Mode = SM_CLOSE;
                Close();
                return;
            }
            m_Mode = SM_READ_BODY;
            packetOffset += 2;
        } else if (m_Mode != SM_READ_BODY) {
            Close();
            return;
        }

        if (readSize < packetOffset + m_nPacketLen) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        ++m_nPacketCount;
        const unsigned char opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (opcode >= m_cMaxProtocolNo) {
            // Log.Add(
            //     LOG_ERROR,
            //     "RaidSocket: unknown protocol %d, worldID[%d]",
            //     static_cast<int>(opcode),
            //     m_nWorldId)
            // L2_LOG_ERROR(Log,
            //     "RaidSocket: unknown protocol %d, worldID[%d]",
            //     static_cast<int>(opcode),
            //     m_nWorldId)
            L2_LOG_ERROR(
                Log, "Closing raid connection for world %d: opcode %u is outside the supported range (exclusive limit %u).",
                m_nWorldId, static_cast<unsigned int>(opcode), static_cast<unsigned int>(m_cMaxProtocolNo)
            );
            m_Mode = SM_CLOSE;
            Close();
            return;
        }

        auto* const raidPacket = new CPacketEx<DefaultIOBuffer, CSocket, PacketFunc>;
        raidPacket->m_pSocket = this;
        raidPacket->m_pBuf = m_pReadBuf;
        raidPacket->m_pFunc = m_pPacketTable[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        raidPacket->PostObject(packetOffset);

        packetOffset += m_nPacketLen;
        m_Mode = SM_READ_LEN;
    }
    Read(0);
}

RaidServerSocket::RaidServerSocket(const std::uint64_t socket) : RaidSocket(socket) {
    m_cMaxProtocolNo = 0x0f;
    m_pPacketTable = reinterpret_cast<PacketFunc*>(g_RaidServerPacketTable.GetData());
}

RaidServerSocket::~RaidServerSocket() = default;

void RaidServerSocket::OnClose() {
    // Current L2Server 0x00a1de08.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR] RaidServerSocket: OnClose(), WorldID[%d]",
    //     m_nWorldId)
    // L2_LOG_ERROR(Log,
    //     "[NO_ERROR] RaidServerSocket: OnClose(), WorldID[%d]",
    //     m_nWorldId)
    L2_LOG_WARN(Log, "Raid server connection closed for world %d; clearing raid users.", m_nWorldId);
    RaidSocket::OnClose();
    RaidClientManager::inst().Connected(m_nWorldId, false);
    RaidClientManager::inst().DeleteAllRaidUser(m_nWorldId);
}

RaidClientSocket::RaidClientSocket(const std::uint64_t socket) : RaidSocket(socket), m_Quota(0), m_CurrentQuota(0) {
    m_cMaxProtocolNo = 0x27;
    m_pPacketTable = reinterpret_cast<PacketFunc*>(g_RaidClientPacketTable.GetData());
}

RaidClientSocket::~RaidClientSocket() = default;

RaidClientSocket* RaidClientSocket::Allocate(const std::uint64_t socket) { return new RaidClientSocket(socket); }

void RaidClientSocket::OnClose() {
    // Current L2Server 0x00a1e6e4.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR] RaidClientSocket: OnClose(), WorldID[%d]",
    //     m_nWorldId)
    // L2_LOG_ERROR(Log,
    //     "[NO_ERROR] RaidClientSocket: OnClose(), WorldID[%d]",
    //     m_nWorldId)
    L2_LOG_WARN(Log, "Raid client connection closed for world %d; removing the connection.", m_nWorldId);
    RaidSocket::OnClose();
    RaidServerManager::inst().RemoveSocket(m_nWorldId);
}

bool RaidClientSocket::CheckQuota() {
    // Current L2Server 0x00a1fa34 increments first and rejects equality.
    L2_GUARD_FUNCTION();
    const int current = runtime::raw_atomic::AddI32(&m_CurrentQuota, 1);
    if (m_Quota <= current) {
        runtime::raw_atomic::AddI32(&m_CurrentQuota, -1);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][RAID][04-1] [failure] CurrentQuota: %d Quota: %d",
        //     static_cast<int>(m_CurrentQuota),
        //     m_Quota)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][RAID][04-1] [failure] CurrentQuota: %d Quota: %d",
        //     static_cast<int>(m_CurrentQuota),
        //     m_Quota)
        L2_LOG_WARN(
            Log, "Raid reservation rejected for world %d: attempted quota usage %d reaches or exceeds limit %d.", m_nWorldId,
            current, m_Quota
        );
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][RAID][04-1] [success] CurrentQuota: %d Quota: %d",
    //     static_cast<int>(m_CurrentQuota),
    //     m_Quota)
    // Noise: successful quota checks are internal steps. The caller logs the reservation result.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][RAID][04-1] [success] CurrentQuota: %d Quota: %d",
    //     static_cast<int>(m_CurrentQuota),
    //     m_Quota)
    return true;
}

void RaidClientSocket::OnUserSocketClosed() {
    // Current L2Server 0x00a1fb3c.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::AddI32(&m_CurrentQuota, -1);
}

WorldRaidHost::WorldRaidHost() : m_idFirstStartedWorld(0), m_alignment14(0) {}

WorldRaidHost::~WorldRaidHost() = default;

std::uint32_t WorldRaidHost::GetNPCID(const int worldId) {
    // Current L2Server 0x00a17b08.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto found = m_WorldRaidNpcs.find(worldId);
    return found != m_WorldRaidNpcs.end() ? found->second : 0;
}

void WorldRaidHost::StopWorldRaid(const int worldId) {
    // Current L2Server 0x00a1a270.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_WorldRaidNpcs.erase(worldId);
    if (m_idFirstStartedWorld == worldId) {
        m_idFirstStartedWorld = 0;
    }
}

bool WorldRaidHost::StartWorldRaid(const int worldId, const std::uint32_t npcId) {
    // Current L2Server 0x00a1b5fc.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_WorldRaidNpcs[worldId] = npcId;
    if (m_idFirstStartedWorld != 0) {
        return false;
    }
    m_idFirstStartedWorld = worldId;
    return true;
}

RaidReserverInfo::RaidReserverInfo(
    const int worldId,
    const int accountId,
    SharedUtilsWideChar* const accountName,
    const int characterId,
    SharedUtilsWideChar* const characterName,
    const unsigned char languageType,
    const int characterServerId,
    const FVector originPosition,
    const FVector destinationPosition,
    const stAccountAuthInfo accountAuthInfo,
    const int premiumLevel,
    const int dormantType,
    const int dormantMarkingTime
) :
    m_nWorldId(worldId),
    m_nAccountId(accountId),
    m_nCharId(characterId),
    m_cLanguageType(languageType),
    m_nCharServerId(characterServerId),
    m_OriginPos(originPosition),
    m_DestPos(destinationPosition),
    m_nPremiumLevel(premiumLevel),
    m_nDormantType(dormantType),
    m_nDormantMarkingTime(dormantMarkingTime),
    m_AccountAuthInfo(accountAuthInfo) {
    // Current L2Server 0x00a168f4. The packet decoder caps both source
    // strings to these exact destination extents before this constructor.
    const std::size_t accountLength = std::char_traits<SharedUtilsWideChar>::length(accountName);
    std::char_traits<SharedUtilsWideChar>::copy(m_sAccountName, accountName, accountLength + 1);
    const std::size_t characterLength = std::char_traits<SharedUtilsWideChar>::length(characterName);
    std::char_traits<SharedUtilsWideChar>::copy(m_sCharName, characterName, characterLength + 1);
}

RaidReserverInfo::~RaidReserverInfo() = default;

RaidServerManager::RaidServerManager() :
    m_ReviveType(RSOT_NORMAL),
    m_DiePenaltyType(RSOT_NORMAL),
    m_PKKarmaPenaltyType(RSOT_NORMAL),
    m_alignment184(0),
    m_pWorldRaidHost(new WorldRaidHost) {}

RaidServerManager::~RaidServerManager() {
    delete m_pWorldRaidHost;
    m_pWorldRaidHost = nullptr;
}

L2_DEFINE_OBJECT_CLASS_ID(RaidServerManager)

L2_DEFINE_OBJECT_CLASS_NAME(RaidServerManager, m_pClassName_RaidServerManager)

L2_DEFINE_OBJECT_ALLOCATOR(RaidServerManager, __GetClassName())

L2_DEFINE_OBJECT_NEW(RaidServerManager)

L2_DEFINE_OBJECT_RELEASE(RaidServerManager)

void RaidServerManager::LoadForbiddenItems(XMLElement& element) {
    // Current L2Server 0x00a1d610.
    L2_GUARD_FUNCTION();
    for (XMLElement* const item : element.m_vChild) {
        const std::u16string& itemName = item->m_smapAttr[std::u16string(u"ItemName")];
        const int classId = g_ObjectDB.GetClassIdFromName(itemName.c_str(), false);
        m_ForbiddenItemSet.insert(classId);
    }
}

void RaidServerManager::Load() {
    // Current L2Server 0x00a1da7c.
    L2_GUARD_FUNCTION();
    constexpr const wchar_t* kFileName = L"..\\Script\\RaidServerData.xml";
    XMLElement* const root = CXmlLoader::inst().Load(kFileName);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load %s",
        //     L"Raid.cpp",
        //     0x14b,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load %s",
        //     L"Raid.cpp",
        //     0x14b,
        //     kFileName)
        L2_LOG_ERROR(Log, "Raid configuration could not be loaded: %s.", WideToUtf8(kFileName).c_str());
        return;
    }

    for (XMLElement* const section : root->m_vChild) {
        if (section == nullptr) {
            continue;
        }
        if (EqualsAsciiCaseInsensitive(section->m_sName, u"AllowFrom")) {
            for (XMLElement* const client : section->m_vChild) {
                if (client == nullptr) {
                    continue;
                }
                const int worldId = XMLUtil::GetAttributeValueAsInt(client, std::u16string(u"WorldID"), 0);
                const int quota = XMLUtil::GetAttributeValueAsInt(client, std::u16string(u"Quota"), 0);
                m_ClientQuotaMap[worldId] = quota;
            }
        }

        if (EqualsAsciiCaseInsensitive(section->m_sName, u"Setting")) {
            for (XMLElement* const setting : section->m_vChild) {
                if (setting == nullptr) {
                    continue;
                }
                if (EqualsAsciiCaseInsensitive(setting->m_sName, u"PenaltyType")) {
                    m_ReviveType = static_cast<RaidServerOptionType>(
                        XMLUtil::GetAttributeValueAsInt(setting, std::u16string(u"ReviePosType"), 0)
                    );
                    m_DiePenaltyType = static_cast<RaidServerOptionType>(
                        XMLUtil::GetAttributeValueAsInt(setting, std::u16string(u"DiePenaltyType"), 0)
                    );
                    m_PKKarmaPenaltyType = static_cast<RaidServerOptionType>(
                        XMLUtil::GetAttributeValueAsInt(setting, std::u16string(u"PKKarmaPenaltyType"), 0)
                    );
                }
                if (g_L2ServerType == L2ServerType::WORLDSERVER &&
                    EqualsAsciiCaseInsensitive(setting->m_sName, u"ForbiddenItems_WorldServer")) {
                    LoadForbiddenItems(*setting);
                }
            }
        }
    }

    root->ReleaseChild();
    delete root;
}

RaidServerOptionType RaidServerManager::GetReviveType() {
    L2_GUARD_FUNCTION();
    return m_ReviveType;
}

RaidServerOptionType RaidServerManager::GetPKKarmaPenaltyType() {
    L2_GUARD_FUNCTION();
    return m_PKKarmaPenaltyType;
}

RaidClientSocket* RaidServerManager::GetSocket(const int worldId) {
    // Current L2Server 0x00a17c2c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto found = m_RaidSockets.find(worldId);
    return found != m_RaidSockets.end() ? found->second : nullptr;
}

bool RaidServerManager::SetSocket(const int worldId, RaidClientSocket* const socket) {
    // Current L2Server 0x00a1a348.
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return false;
    }

    L2_GUARD(m_Lock);
    int quota = 0;
    const auto quotaEntry = m_ClientQuotaMap.find(worldId);
    if (quotaEntry == m_ClientQuotaMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unregistered RaidClient[%d] is connected!!!",
        //     L"Raid.cpp",
        //     0x1a6,
        //     worldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unregistered RaidClient[%d] is connected!!!",
        //     L"Raid.cpp",
        //     0x1a6,
        //     worldId)
        L2_LOG_WARN(Log, "Raid client world %d has no configured quota; registering its connection with quota 0.", worldId);
    } else {
        quota = quotaEntry->second;
    }

    socket->SetWorldId(worldId);
    socket->SetQuota(quota);
    L2_ADD_REF(socket, ObjectRefType::SocketMap);

    const auto existing = m_RaidSockets.find(worldId);
    if (existing == m_RaidSockets.end()) {
        m_RaidSockets.emplace(worldId, socket);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Raid Client[%d] is already connected. Dangling socket? But allowed.",
        //     L"Raid.cpp",
        //     0x1b0,
        //     worldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Raid Client[%d] is already connected. Dangling socket? But allowed.",
        //     L"Raid.cpp",
        //     0x1b0,
        //     worldId)
        L2_LOG_WARN(Log, "Replacing the existing raid client connection for world %d.", worldId);
        existing->second = socket;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] SetSocket(). RaidClient[%d]",
    //     worldId)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] SetSocket(). RaidClient[%d]",
    //     worldId)
    L2_LOG_INFO(Log, "Raid client connection registered for world %d with quota %d.", worldId, quota);
    return true;
}

bool RaidServerManager::RemoveSocket(const int worldId) {
    // Current L2Server 0x00a17d68.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto found = m_RaidSockets.find(worldId);
    if (found == m_RaidSockets.end()) {
        return false;
    }
    L2_RELEASE(found->second, ObjectRefType::SocketMap);
    m_RaidSockets.erase(found);
    return true;
}

void RaidServerManager::RaidClientVersionPacket(const int worldId, RaidClientSocket* const socket) {
    // Current L2Server 0x00a1a9bc.
    L2_GUARD_FUNCTION();
    if (SetSocket(worldId, socket)) {
        db.RequestCheckRaidWorld(worldId);
    }
}

void RaidServerManager::RaidReserveUserPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1aa8c.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    SharedUtilsWideChar accountName[15]{};
    int characterDatabaseId = 0;
    SharedUtilsWideChar characterName[25]{};
    unsigned char languageType = 0;
    int characterServerId = 0;
    int allianceDatabaseId = 0;
    int originX = 0;
    int originY = 0;
    int originZ = 0;
    int destinationX = 0;
    int destinationY = 0;
    int destinationZ = 0;
    stAccountAuthInfo authInfo;
    int premiumLevel = 0;
    int dormantType = 0;
    int dormantMarkingTime = 0;

    ::Disassemble(
        packet, "dSdScdddddddddddddcdddsdddd", &accountId, 0x1e, accountName, &characterDatabaseId, 0x32, characterName,
        &languageType, &characterServerId, &allianceDatabaseId, &originX, &originY, &originZ, &destinationX, &destinationY,
        &destinationZ, &authInfo.nTimeLeft, &authInfo.nStat, &authInfo.nWarnFlag, &authInfo.nLoginFlag, &authInfo.nAge,
        &authInfo.cChargeMethod, &authInfo.nReservations, &authInfo.nRestTime_china, &authInfo.nPlayTime_china, 9,
        authInfo.sPCCafeCode, &authInfo.client_addr.s_addr, &premiumLevel, &dormantType, &dormantMarkingTime
    );

    const int worldId = socket->GetWorldId();
    if (allianceDatabaseId > 0 && !g_AllianceDbIdMap.ExistKey(RaidKey(worldId, allianceDatabaseId))) {
        db.RequestLoadAlliance(worldId, allianceDatabaseId);
    }

    if (!socket->CheckQuota()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][RAID][05-3] RSC_RESERVE_RESULT[failure] protocol is sent")
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][RAID][05-3] RSC_RESERVE_RESULT[failure] protocol is sent")
        L2_LOG_WARN(
            Log,
            "Raid reservation rejected for character database ID %d from world %d: quota check failed; sending failure "
            "response.",
            characterDatabaseId, worldId
        );
        socket->Send("cdd", 1, characterDatabaseId, 0);
        return;
    }

    L2_GUARD(m_Lock);
    const RaidKey key(worldId, characterDatabaseId);
    if (m_mapRaidCandidates.find(key) == m_mapRaidCandidates.end()) {
        RaidReserverInfo* const info = new RaidReserverInfo(
            worldId, accountId, accountName, characterDatabaseId, characterName, languageType, characterServerId,
            FVector(originX, originY, originZ), FVector(destinationX, destinationY, destinationZ), authInfo, premiumLevel,
            dormantType, dormantMarkingTime
        );
        m_mapRaidCandidates.emplace(key, info);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][RAID][05-1] RSC_RESERVE_RESULT[success] protocol is sent. [%d][%d]",
        //     worldId,
        //     characterDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][RAID][05-1] RSC_RESERVE_RESULT[success] protocol is sent. [%d][%d]",
        //     worldId,
        //     characterDatabaseId)
        L2_LOG_INFO(
            Log, "Raid reservation created for character database ID %d from world %d; sending success response.",
            characterDatabaseId, worldId
        );
        socket->Send("cdd", 1, characterDatabaseId, 1);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][RAID][05-2] RSC_RESERVE_RESULT[failure] protocol is sent. useu already exist in mapRaidCandidates")
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][RAID][05-2] RSC_RESERVE_RESULT[failure] protocol is sent. useu already exist in mapRaidCandidates")
    L2_LOG_WARN(
        Log,
        "Raid reservation rejected for character database ID %d from world %d: candidate already exists; removing the "
        "candidate and sending failure response.",
        characterDatabaseId, worldId
    );
    DeleteRaidCandidate(worldId, characterDatabaseId);
    socket->Send("cdd", 1, characterDatabaseId, 0);
}

void RaidServerManager::DeleteRaidCandidate(const int worldId, const int characterDatabaseId) {
    // Current L2Server 0x00a1a5b8 erases only the pointer
    // carrier. It does not delete the pointed reservation record here.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_mapRaidCandidates.erase(RaidKey(worldId, characterDatabaseId));
}

RaidReserverInfo* RaidServerManager::GetRaidCandidateInfo(const RaidKey key) {
    // Current L2Server 0x00a17ef8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return _GetRaidCandidateInfo(key);
}

RaidReserverInfo* RaidServerManager::_GetRaidCandidateInfo(const RaidKey key) {
    // Current L2Server 0x00a17ffc. Caller owns m_Lock.
    L2_GUARD_FUNCTION();
    const auto found = m_mapRaidCandidates.find(key);
    return found != m_mapRaidCandidates.end() ? found->second : nullptr;
}

void RaidServerManager::ReplyVersionPacket(const int worldId, const int result) {
    // Current L2Server 0x00a180a8.
    L2_GUARD_FUNCTION();

    RaidClientSocket* const socket = GetSocket(worldId);
    if (socket == nullptr) {
        return;
    }

    long timezoneSeconds = 0;
#if defined(_WIN32)
    const int timezoneResult = ::_get_timezone(&timezoneSeconds);
#else
    ::tzset();
    timezoneSeconds = ::timezone;
    const int timezoneResult = 0;
#endif

    int timezoneHours = 0;
    // int timezoneLogLine = 0
    // Noise: obsolete format/line bookkeeping belongs to the disabled log below.
    // [[maybe_unused]] int timezoneLogLine = 0.
    // const wchar_t* timezoneLog = nullptr
    // [[maybe_unused]] const wchar_t* timezoneLog = nullptr.
    if (timezoneResult == 0) {
        timezoneHours = static_cast<int>(timezoneSeconds / 0xe10L);
        // timezoneLogLine = 0x26c
        // timezoneLog = L"[NO_ERROR][%s][%d] Raid Server Timezone [%d]"
    } else {
        timezoneHours = -0xd;
        // timezoneLogLine = 0x270
        // timezoneLog = L"[%s][%d] Fail to get Timezone [%d]"
    }
    // Log.Add(
    //     LOG_ERROR,
    //     timezoneLog,
    //     L"Raid.cpp",
    //     timezoneLogLine,
    //     timezoneHours)
    // L2_LOG_ERROR(Log,
    //     timezoneLog,
    //     L"Raid.cpp",
    //     timezoneLogLine,
    //     timezoneHours)
    if (timezoneResult == 0) {
        L2_LOG_INFO(Log, "Raid handshake uses standard-time offset %d hours west of UTC.", timezoneHours);
    } else {
        L2_LOG_ERROR(
            Log, "Cannot read the raid server time-zone offset (error %d); using fallback %d hours.", timezoneResult,
            timezoneHours
        );
    }

    const auto quota = m_ClientQuotaMap.find(worldId);
    if (quota == m_ClientQuotaMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unregistered RaidClient[%d] is connected!!!",
        //     L"Raid.cpp",
        //     0x27b,
        //     worldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unregistered RaidClient[%d] is connected!!!",
        //     L"Raid.cpp",
        //     0x27b,
        //     worldId)
        L2_LOG_WARN(Log, "Raid handshake from world %d has no configured quota; responding with quota 0.", worldId);
        socket->Send("cdddd", 0, g_nWorldId, result, 0, timezoneHours);
        return;
    }

    const int clientQuota = quota->second;
    if (result != 1) {
        socket->Send("cdddd", 0, g_nWorldId, result, clientQuota, timezoneHours);
        return;
    }

    L2_GUARD(m_Lock);
    std::array<char, 0x5000> packet;
    int packetCount = 0;
    int packetLength = 0;
    const auto raidPledges = m_mapRaidPledge.find(worldId);
    if (raidPledges != m_mapRaidPledge.end()) {
        for (const auto& raidPledge : raidPledges->second) {
            const int written = ::Assemble(
                packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "d",
                static_cast<int>(raidPledge.second)
            );
            if (written > 0) {
                packetLength += written;
                ++packetCount;
            }
            if (packetCount > 2000) {
                socket->Send(
                    "cdddddb", 0, g_nWorldId, 1, clientQuota, timezoneHours, packetCount, packetLength, packet.data()
                );
                packetCount = 0;
                packetLength = 0;
            }
        }
    }
    socket->Send("cdddddb", 0, g_nWorldId, 1, clientQuota, timezoneHours, packetCount, packetLength, packet.data());
}

User* RaidServerManager::GetAttacker(const RaidKey key) {
    // Current L2Server 0x00a186c4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto found = m_mapRaidUsersByKey.find(key);
    return found != m_mapRaidUsersByKey.end() ? found->second : nullptr;
}

void RaidServerManager::BroadcastWorldRaidEvent(const int firstValue, const int secondValue, const int thirdValue) {
    // Current L2Server 0x00a18910.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const auto& entry : m_RaidSockets) {
        RaidClientSocket* const socket = entry.second;
        if (socket == nullptr) {
            continue;
        }
        const std::uint32_t npcId = m_pWorldRaidHost->GetNPCID(socket->GetWorldId());
        if (npcId != 0) {
            socket->Send("cddddd", 0x0e, npcId, socket->GetWorldId(), firstValue, secondValue, thirdValue);
        }
    }
}

void RaidServerManager::DelegatePacketHandle(
    User& user, const int packetProtocol, const int packetLength, const char* const packet
) {
    // Current L2Server 0x00a196e8.
    L2_GUARD_FUNCTION();
    const int worldId = user.m_data->m_nWorldId;
    if (worldId != g_nWorldId) {
        const RaidKey key(worldId, static_cast<int>(user.GetDBID()));
        if (GetAttacker(key) != nullptr) {
            if (RaidClientSocket* const socket = GetSocket(user.m_data->m_nWorldId)) {
                socket->Send("cddb", 5, static_cast<int>(user.GetDBID()), packetProtocol, packetLength, packet);
            }
        }
    }
}

void RaidServerManager::ConfirmRaidUser(User& user) {
    // Current L2Server 0x00a1a7dc.
    L2_GUARD_FUNCTION();
    RaidClientSocket* const socket = GetSocket(user.m_data->m_nWorldId);
    if (socket != nullptr) {
        socket->Send("cd", 2, static_cast<int>(user.GetDBID()));
        user.GetSocket()->IncUserCount(false);
    }

    L2_GUARD(m_Lock);
    static_cast<void>(_GetRaidCandidateInfo(RaidKey(user.m_data->m_nWorldId, static_cast<int>(user.GetDBID()))));
    m_mapRaidUsersByKey.emplace(RaidKey(user.m_data->m_nWorldId, static_cast<int>(user.GetDBID())), &user);
    m_mapRaidCandidates.erase(RaidKey(user.m_data->m_nWorldId, static_cast<int>(user.GetDBID())));
}

void RaidServerManager::ClearRaidUserMap(User& user) {
    // Current L2Server 0x00a1b06c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_mapRaidUsersByKey.erase(RaidKey(user.m_data->m_nWorldId, static_cast<int>(user.GetDBID())));
}

void RaidServerManager::DisconnectRaidUser(const int worldId, const int characterDatabaseId) {
    // Current L2Server 0x00a1b168.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(RaidKey(worldId, characterDatabaseId));
    if (!user) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    socket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    {
        L2_GUARD(socket->mLock);
        socket->UnBindUser();
    }
    ClearRaidUserMap(*user.get());
    socket->Send("chd", 0xfe, 0xb9, 0);
}

void RaidServerManager::NotifyRaidUserDisconnected(User& user) {
    // Current L2Server 0x00a19830.
    L2_GUARD_FUNCTION();
    const int worldId = user.m_data->m_nWorldId;
    if (worldId == g_nWorldId) {
        return;
    }

    const RaidKey key(worldId, static_cast<int>(user.GetDBID()));
    if (GetAttacker(key) == nullptr) {
        return;
    }

    RaidClientSocket* const socket = GetSocket(worldId);
    if (socket != nullptr) {
        socket->Send("cd", 6, static_cast<int>(user.GetDBID()));
    }
}

bool RaidServerManager::RestartOnRaidServer(User& user) {
    // Current L2Server 0x00a1b700.
    L2_GUARD_FUNCTION();
    const int worldId = user.m_data->m_nWorldId;
    if (worldId == g_nWorldId) {
        return false;
    }

    const RaidKey key(worldId, static_cast<int>(user.GetDBID()));
    if (GetAttacker(key) == nullptr) {
        return false;
    }

    CUserSocket* const userSocket = user.GetSocket();
    if (userSocket == nullptr) {
        return false;
    }

    ClearRaidUserMap(user);
    RaidClientSocket* const raidSocket = GetSocket(worldId);
    if (raidSocket == nullptr) {
        userSocket->Send("chd", 0xfe, 0xb9, 1);
        userSocket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
        userSocket->UnBindUser();
        return false;
    }

    raidSocket->Send("cdd", 7, static_cast<int>(user.GetDBID()), static_cast<int>(user.GetServerId()));
    return false;
}

bool RaidServerManager::ReturnToOrigin(User* const user) {
    // Current L2Server 0x00a19488.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReturnToOrigin failed User is Not valid",
        //     L"Raid.cpp",
        //     0x2cb)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReturnToOrigin failed User is Not valid",
        //     L"Raid.cpp",
        //     0x2cb)
        L2_LOG_WARN(Log, "Return from raid rejected: player pointer is null.");
        return false;
    }

    const int worldId = user->m_data->m_nWorldId;
    if (worldId != g_nWorldId && GetAttacker(RaidKey(worldId, static_cast<int>(user->GetDBID()))) != nullptr) {
        CUserSocket* const userSocket = user->GetSocket();
        if (userSocket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ERROR][RAID] ReturnToOrigin failed. "
            //     L"pUserSocket is not valid. [%s][%d]",
            //     L"Raid.cpp",
            //     0x2ef)
            // L2_LOG_ERROR(Log,
            //     L"[ERROR][RAID] ReturnToOrigin failed. "
            //     L"pUserSocket is not valid. [%s][%d]",
            //     L"Raid.cpp",
            //     0x2ef)
            L2_LOG_WARN(
                Log, "Return from raid rejected for character database ID %u: player socket is unavailable.",
                static_cast<unsigned int>(user->GetDBID())
            );
            return false;
        }

        RaidClientSocket* const raidSocket = GetSocket(user->m_data->m_nWorldId);
        if (raidSocket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ERROR][RAID] ReturnToOrigin failed. Raid socket is "
            //     L"not valid. [%s][%d][%d][%d]",
            //     L"Raid.cpp",
            //     0x2e5,
            //     user->m_data->m_nWorldId,
            //     static_cast<int>(user->GetDBID()))
            // L2_LOG_ERROR(Log,
            //     L"[ERROR][RAID] ReturnToOrigin failed. Raid socket is "
            //     L"not valid. [%s][%d][%d][%d]",
            //     L"Raid.cpp",
            //     0x2e5,
            //     user->m_data->m_nWorldId,
            //     static_cast<int>(user->GetDBID()))
            L2_LOG_WARN(
                Log, "Return from raid rejected for character database ID %u: connection to origin world %d is unavailable.",
                static_cast<unsigned int>(user->GetDBID()), worldId
            );
            ShowSocketList();
            userSocket->SendSystemMessage(u"SYS", u"Your original server is currently unavailable.");
            return false;
        }

        raidSocket->Send("cdd", 3, static_cast<int>(user->GetDBID()), static_cast<int>(user->GetServerId()));
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] ReturnToOrigin failed",
    //     L"Raid.cpp",
    //     0x2d1)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] ReturnToOrigin failed",
    //     L"Raid.cpp",
    //     0x2d1)
    L2_LOG_WARN(
        Log,
        "Return from raid rejected for character database ID %u: player belongs to the local world or has no attacker "
        "registration (player world %d, local world %d).",
        static_cast<unsigned int>(user->GetDBID()), worldId, g_nWorldId
    );
    return false;
}

void RaidServerManager::ShowSocketList() {
    // Current L2Server 0x00a1718c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_RaidSockets.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] ShowSocketList, size() == 0. [%s][%d].",
        //     L"Raid.cpp",
        //     0x1df)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] ShowSocketList, size() == 0. [%s][%d].",
        //     L"Raid.cpp",
        //     0x1df)
        L2_LOG_INFO(Log, "Raid connection list is empty.");
        return;
    }

    for (const auto& entry : m_RaidSockets) {
        if (entry.second != nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] ShowSocketList - RaidSocket[worldId:%d]. "
            //     L"[%s][%d].",
            //     entry.first,
            //     L"Raid.cpp",
            //     0x1e9)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] ShowSocketList - RaidSocket[worldId:%d]. "
            //     L"[%s][%d].",
            //     entry.first,
            //     L"Raid.cpp",
            //     0x1e9)
            L2_LOG_INFO(Log, "Raid connection list: world %d has a registered socket.", entry.first);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] ShowSocketList - RaidSocket[worldId:%d] is "
            //     L"nullptr. [%s][%d].",
            //     entry.first,
            //     L"Raid.cpp",
            //     0x1ed)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] ShowSocketList - RaidSocket[worldId:%d] is "
            //     L"nullptr. [%s][%d].",
            //     entry.first,
            //     L"Raid.cpp",
            //     0x1ed)
            L2_LOG_WARN(Log, "Raid connection list: world %d has a null socket entry.", entry.first);
        }
    }
}

int RaidServerManager::GetCurrentRaiduserCount() {
    // Current L2Server.exe 0x00a17340 traverses the live map
    // without taking m_Lock.
    L2_GUARD_FUNCTION();
    std::int32_t currentUserCount = 0;
    for (const auto& entry : m_RaidSockets) {
        if (entry.second != nullptr) {
            currentUserCount =
                runtime::AddInt32Wrapping(currentUserCount, static_cast<std::int32_t>(entry.second->GetCurrentQuota()));
        }
    }
    return currentUserCount;
}

void RaidServerManager::ShowRaidServerInfo(User& user) {
    // Current L2Server 0x00a187c8.
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_ClientQuotaMap) {
        std::array<char16_t, 512> message;
        static_cast<void>(FormatUtf16To(message.data(), 0x1ff, u"AllowFrom: %d / Quata: %d", entry.first, entry.second));
        user.GetSocket()->SendSystemMessage(u"SYS", message.data());
    }
}

bool RaidServerManager::RequestPledgeChattingRelay(
    CPledge& pledge,
    const int serverId,
    const int characterDatabaseId,
    const char16_t* const senderName,
    const char16_t* const message,
    const SayPacketType type
) {
    // Current L2Server 0x00a184cc.
    L2_GUARD_FUNCTION();
    RaidClientSocket* const socket = GetSocket(pledge.GetWorldId());
    if (socket != nullptr) {

        socket->Send("cdddSSd", 8, pledge.GetDbId(), serverId, characterDatabaseId, senderName, message, type);
    }
    return socket != nullptr;
}

bool RaidServerManager::DoNotRelayForPledge(CPledge& pledge) {
    // Current L2Server 0x00a185e8.
    L2_GUARD_FUNCTION();
    RaidClientSocket* const socket = GetSocket(pledge.GetWorldId());
    if (socket != nullptr) {
        socket->Send("cd", 9, pledge.GetDbId());
    }
    return socket != nullptr;
}

void RaidServerManager::RemoveRaidPledge(CPledge& pledge) {
    // Current L2Server 0x00a1a6a4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto world = m_mapRaidPledge.find(pledge.GetWorldId());
    if (world != m_mapRaidPledge.end()) {
        world->second.erase(static_cast<int>(pledge.GetDbId()));
    }
}

RaidAllowAction::RaidAllowAction() {}

RaidAllowAction& RaidAllowAction::Inst() {
    L2_GUARD_FUNCTION();
    if (m_AllowAction == nullptr) {
        m_AllowAction = new RaidAllowAction;
        m_AllowAction->SetAction();
    }
    return *m_AllowAction;
}

void RaidAllowAction::SetAction() {
    L2_GUARD_FUNCTION();
    static constexpr std::array<int, 20> kInvalidActions{
        3, 5, 10, 11, 28, 57, 37, 51, 56, 58, 59, 60, 63, 65, 76, 61, 71, 72, 86, 73};

    m_actionCount = 0;
    m_bufLen = 0;
    for (const int actionId : kInvalidActions) {
        m_vInvalidActionList.push_back(actionId);
    }
}

bool RaidAllowAction::IsValidAction(const int actionId) const {
    L2_GUARD_FUNCTION();
    return std::find(m_vInvalidActionList.begin(), m_vInvalidActionList.end(), actionId) == m_vInvalidActionList.end();
}

void RaidAllowAction::AddAction(const int actionId) {
    L2_GUARD_FUNCTION();
    if (!IsValidAction(actionId)) {
        return;
    }

    ++m_actionCount;
    const int assembled =
        ::Assemble(reinterpret_cast<char*>(m_buf) + m_bufLen, static_cast<int>(sizeof(m_buf) - m_bufLen), "d", actionId);
    if (assembled > 0) {
        m_bufLen += static_cast<std::uint32_t>(assembled);
    }
}

void RaidAllowAction::SendActionList(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    user->GetSocket()->Send("chdb", 0xfe, 0x60, m_actionCount, static_cast<int>(m_bufLen), m_buf);
}

RaidServerInfo::RaidServerInfo(const int worldId, const std::uint32_t serverAddress, const int port) :
    m_bConnected(false),
    m_bSendable(false),
    m_alignment002To008{},
    m_pRaidSocket(nullptr),
    m_nLogTick(0),
    m_nWorldId(worldId),
    m_ServerAddr(serverAddress),
    m_nPort(port),
    m_Quota(0),
    m_CurrentQuota(0) {}

RaidServerInfo::~RaidServerInfo() = default;

bool RaidServerInfo::Connect() {
    // Current L2Server 0x00a16784.
    L2_GUARD_FUNCTION();
    if (m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Already connected to RaidServer")
        // L2_LOG_ERROR(Log,
        //     L"Already connected to RaidServer")
        L2_LOG_WARN(Log, "Raid connection attempt skipped: world %d is already marked connected.", m_nWorldId);
        return false;
    }

    const runtime::NativeSocket nativeSocket = runtime::OpenTcpSocket();
    in_addr address{};
    address.s_addr = m_ServerAddr;
    if (!runtime::ConnectTcpSocket(nativeSocket, address, static_cast<std::uint16_t>(m_nPort))) {
        static_cast<void>(runtime::GetTickCount());
        runtime::CloseTcpSocket(nativeSocket);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][Connected to Raid Server] m_nWorldId[%d]",
    //     m_nWorldId)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][Connected to Raid Server] m_nWorldId[%d]",
    //     m_nWorldId)
    L2_LOG_INFO(Log, "TCP connection established to raid world %d; initializing the socket.", m_nWorldId);
    m_pRaidSocket = new RaidServerSocket(nativeSocket);
    m_pRaidSocket->Initialize();
    m_pRaidSocket->SetWorldId(m_nWorldId);
    return true;
}

void RaidServerInfo::Init() {
    // Current L2Server 0x00a169b4.
    L2_GUARD_FUNCTION();
    if (!m_bConnected || m_pRaidSocket == nullptr) {
        static_cast<void>(runtime::GetTickCount());
        if (!Connect()) {
            m_bConnected = false;
            m_bSendable = false;
            m_pRaidSocket = nullptr;
        } else {
            m_bConnected = true;
            m_bSendable = true;
            m_pRaidSocket->Send("cd", 0, g_nWorldId);
        }
    }
}

void RaidServerInfo::SetSendable(bool value) {
    // Current L2Server 0x00a15178.
    L2_GUARD_FUNCTION();
    m_bSendable = value;
    if (!value) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MyWorld[%d] can attack to RaidWorld[%d], but RaidWorld do not have sql pool!!!",
        //     L"Raid.cpp",
        //     0x4f2,
        //     g_nWorldId,
        //     m_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MyWorld[%d] can attack to RaidWorld[%d], but RaidWorld do not have sql pool!!!",
        //     L"Raid.cpp",
        //     0x4f2,
        //     g_nWorldId,
        //     m_nWorldId)
        L2_LOG_WARN(Log, "Raid sending disabled from local world %d to raid world %d.", g_nWorldId, m_nWorldId);
    }
}

bool RaidServerInfo::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || m_pRaidSocket == nullptr) {
        return false;
    }

    va_list arguments;
    va_start(arguments, format);
    m_pRaidSocket->SendV(format, arguments);
    va_end(arguments);
    return true;
}

bool RaidServerInfo::CheckCondition(User* const user, const bool byBuilder) {
    // Current L2Server 0x00a1cb98. `byBuilder` survives in the public
    // contract but this current revision does not branch on it.
    L2_GUARD_FUNCTION();
    (void)byBuilder;

    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestRaidReserve failed. user is not valid",
        //     L"Raid.cpp",
        //     0x523)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RequestRaidReserve failed. user is not valid",
        //     L"Raid.cpp",
        //     0x523)
        L2_LOG_WARN(Log, "Raid reservation rejected: player pointer is null.");
        return false;
    }
    if (!m_bSendable || m_pRaidSocket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestRaidReserve failed. Cannot send m_bSendable[%d]m_pRaidSocket[%d] IsConnet[%d]",
        //     L"Raid.cpp",
        //     0x529,
        //     m_bSendable,
        //     m_pRaidSocket != nullptr,
        //     m_bConnected)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RequestRaidReserve failed. Cannot send m_bSendable[%d]m_pRaidSocket[%d] IsConnet[%d]",
        //     L"Raid.cpp",
        //     0x529,
        //     m_bSendable,
        //     m_pRaidSocket != nullptr,
        //     m_bConnected)
        L2_LOG_WARN(
            Log, "Raid reservation rejected: connection is not ready (sendable %s, socket present %s, connected %s).",
            m_bSendable ? "yes" : "no", m_pRaidSocket != nullptr ? "yes" : "no", m_bConnected ? "yes" : "no"
        );
        return false;
    }
    if (user->m_nCursedWeaponClassId >= 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestRaidReserve failed because the user is cursed",
        //     L"Raid.cpp",
        //     0x52e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RequestRaidReserve failed because the user is cursed",
        //     L"Raid.cpp",
        //     0x52e)
        L2_LOG_WARN(
            Log, "Raid reservation rejected for character database ID %u: cursed weapon class %d is active.",
            static_cast<unsigned int>(user->GetDBID()), user->m_nCursedWeaponClassId
        );
        return false;
    }
    if (user->m_data->GetWorldId() != g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestRaidReserve failed because the user is already raiduser",
        //     L"Raid.cpp",
        //     0x534)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RequestRaidReserve failed because the user is already raiduser",
        //     L"Raid.cpp",
        //     0x534)
        L2_LOG_WARN(
            Log, "Raid reservation rejected for character database ID %u: player world %d differs from local world %d.",
            static_cast<unsigned int>(user->GetDBID()), static_cast<int>(user->m_data->GetWorldId()), g_nWorldId
        );
        return false;
    }
    if (user->HaveSummon() || user->HavePet()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestRaidReserve failed because the user has summons or pet.",
        //     L"Raid.cpp",
        //     0x539)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RequestRaidReserve failed because the user has summons or pet.",
        //     L"Raid.cpp",
        //     0x539)
        L2_LOG_WARN(
            Log, "Raid reservation rejected for character database ID %u: a summon or pet is active.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }
    if (user->IsTransformed(TransformType::Max) != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RequestRaidReserve failed because the user is Transformed.",
        //     L"Raid.cpp",
        //     0x53e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RequestRaidReserve failed because the user is Transformed.",
        //     L"Raid.cpp",
        //     0x53e)
        L2_LOG_WARN(
            Log, "Raid reservation rejected for character database ID %u: a transformation is active.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    runtime::raw_atomic::AddI32(&m_CurrentQuota, 1);
    return true;
}

bool RaidServerInfo::RequestReserveUser(User* const user, FVector& position) {
    // Current L2Server 0x00a1b890.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    stAccountAuthInfo authInfo;
    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(user->GetAccountID());
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] RequestReserveUser failed.",
            //     L"Raid.cpp",
            //     0x577)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] RequestReserveUser failed.",
            //     L"Raid.cpp",
            //     0x577)
            L2_LOG_WARN(
                Log,
                "Raid reservation request cannot continue for character database ID %u: authentication data for account ID "
                "%u is missing.",
                static_cast<unsigned int>(user->GetDBID()), static_cast<unsigned int>(user->GetAccountID())
            );
            return false;
        }
        authInfo = account->second;
    }

    int allianceId = 0;
    if (CPledge* const pledge = user->GetPledge()) {
        allianceId = pledge->GetAllianceDbId();
    }

    if (!m_bSendable || m_pRaidSocket == nullptr ||
        !m_mapRaidUsers.Insert(static_cast<int>(user->GetDBID()), user, const_cast<char*>("Raid.cpp"), 0x584)) {
        return false;
    }

    user->ProcessMyParty(true);
    CUserSocket* const socket = user->GetSocket();
    const FVector current = user->GetPos();
    const char16_t* const userName = user->GetName();
    const std::u16string targetUserName = std::u16string(userName);
    const std::uint32_t userDatabaseId = user->GetDBID();
    m_pRaidSocket->Send(
        "cdSdScdddddddddddddcdddsdddd", 1, user->GetAccountID(), user->m_sAccountName.data(), userDatabaseId,
        targetUserName.c_str(), user->m_data->m_CountryCode, userDatabaseId, allianceId, static_cast<int>(current.x),
        static_cast<int>(current.y), static_cast<int>(current.z), static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), authInfo.nTimeLeft, authInfo.nStat, authInfo.nWarnFlag, authInfo.nLoginFlag,
        authInfo.nAge, authInfo.cChargeMethod, authInfo.nReservations, authInfo.nRestTime_china, authInfo.nPlayTime_china,
        authInfo.sPCCafeCode, authInfo.client_addr.s_addr, socket->m_iPremiumLevel, socket->m_nDormantType,
        socket->m_nDormantMarkingTime
    );

    socket->AddRaidStatus(URS_REQUEST_RAID);
    user->m_RaidInfo.m_nRaidWorldId = m_nWorldId;
    user->m_bIsSendUserInfoCompleted = false;
    user->m_bIsSendFactionInfoCompleted = false;
    return true;
}

void RaidServerInfo::DisconnectUser(int worldId, int characterDatabaseId) {
    // Current L2Server 0x00a15310.
    L2_GUARD_FUNCTION();
    if (m_pRaidSocket != nullptr) {
        m_pRaidSocket->Send("cdd", 0x1c, worldId, characterDatabaseId);
    }
}

RaidPledgeMemberService_PacketRelay::RaidPledgeMemberService_PacketRelay(int length, const char* buffer) noexcept :
    m_nLen(length), m_alignment0cTo10(0), m_Buffer(buffer) {}

bool RaidPledgeMemberService_PacketRelay::Do(CPledge& pledge, RaidServerInfo& server) {
    L2_GUARD_FUNCTION();
    return server.Send("cddb", 5, pledge.GetDbId(), m_nLen, m_nLen, m_Buffer);
}

bool RaidPledgeMemberService_CancelRenamed::Do(CPledge& pledge, RaidServerInfo& server) {
    L2_GUARD_FUNCTION();
    return server.Send("cd", 0x0f, pledge.GetDbId());
}

bool RaidPledgeMemberService_AffectedByWar::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15c70.
    L2_GUARD_FUNCTION();
    return server.Send("cd", 0x10, pledge.GetDbId());
}

bool RaidPledgeMemberService_UnequipCastleItem::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15a3c.
    L2_GUARD_FUNCTION();
    return server.Send("cd", 0x0d, pledge.GetDbId());
}

RaidPledgeMemberService_SetCastleSiege::RaidPledgeMemberService_SetCastleSiege(
    const std::int32_t (&castleIds)[9], const std::int32_t (&castleStatuses)[9]
) noexcept :
    m_castleIds{}, m_castleStatuses{} {
    std::copy_n(castleIds, 9, m_castleIds);
    std::copy_n(castleStatuses, 9, m_castleStatuses);
}

bool RaidPledgeMemberService_SetCastleSiege::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a16a98.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (std::size_t index = 0; index < 9; ++index) {
        packet.Assemble("dd", m_castleIds[index], m_castleStatuses[index]);
    }
    return server.Send("cddb", 0x11, pledge.GetDbId(), packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
}

RaidPledgeMemberService_SetFortressSiege::RaidPledgeMemberService_SetFortressSiege(
    const int defenceFortressId, const int offenceFortressId
) noexcept :
    m_defenceFortressId(defenceFortressId), m_offenceFortressId(offenceFortressId) {}

bool RaidPledgeMemberService_SetFortressSiege::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15d2c.
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x12, pledge.GetDbId(), m_defenceFortressId, m_offenceFortressId);
}

RaidPledgeMemberService_SetAlliance::RaidPledgeMemberService_SetAlliance(
    const std::uint32_t allianceDatabaseId, const bool masterPledge
) noexcept :
    m_allianceDatabaseId(allianceDatabaseId), m_masterPledge(masterPledge ? 1 : 0) {}

bool RaidPledgeMemberService_SetAlliance::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15edc.
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x14, pledge.GetDbId(), m_allianceDatabaseId, m_masterPledge != 0);
}

bool RaidPledgeMemberService_SetRelationChanged::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15af8.
    L2_GUARD_FUNCTION();
    return server.Send("cd", 0x0e, pledge.GetDbId());
}

bool RaidPledgeMemberService_OpenAllWindowPacket::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a157ec.
    L2_GUARD_FUNCTION();
    return server.Send("cd", 10, pledge.GetDbId());
}

bool RaidPledgeMemberService_DismissPledge::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a158a8.
    L2_GUARD_FUNCTION();
    return server.Send("cd", 0x0b, pledge.GetDbId());
}

RaidPledgeMemberService_UpdatePledgePower::RaidPledgeMemberService_UpdatePledgePower(
    const int gradeId, const std::uint32_t pledgePowerBits
) noexcept :
    m_nGradeId(gradeId), m_lPledgePowerBits(pledgePowerBits) {}

bool RaidPledgeMemberService_UpdatePledgePower::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15964.
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x0c, pledge.GetDbId(), m_nGradeId, m_lPledgePowerBits);
}

bool RaidPledgeMemberService_SetCrest::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a1549c.
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 6, pledge.GetDbId(), m_crestId);
}

bool RaidPledgeMemberService_SetEmblem::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15570.
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 7, pledge.GetDbId(), m_emblemId);
}

bool RaidPledgeMemberService_SetAllianceCrest::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15644.
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 8, pledge.GetDbId(), m_allianceCrestId);
}

RaidPledgeMemberService_SetDominionSiege::RaidPledgeMemberService_SetDominionSiege(
    const int dominionSiegeId, const int dominionSiegeStatus
) noexcept :
    m_nDominionSiegeId(dominionSiegeId), m_nDominionSiegeStatus(dominionSiegeStatus) {}

RaidPledgeMemberService_SyncPledgeSkill::RaidPledgeMemberService_SyncPledgeSkill(
    const int skillId, const int skillLevel
) noexcept :
    m_skillId(skillId), m_skillLevel(skillLevel) {}

RaidPledgeMemberService_SyncPledgeType::RaidPledgeMemberService_SyncPledgeType(
    const std::uint32_t characterDatabaseId, const PledgeType pledgeType
) noexcept :
    m_characterDatabaseId(characterDatabaseId), m_pledgeType(pledgeType) {}

RaidPledgeMemberService_SyncPledgeLevel::RaidPledgeMemberService_SyncPledgeLevel(const int level) noexcept :
    m_level(level), m_alignment0cTo10(0) {}

RaidPledgeMemberService_SyncPledgeMastery::RaidPledgeMemberService_SyncPledgeMastery(
    const PledgeMasteryID::Type masteryId, const int point
) noexcept :
    m_masteryId(masteryId), m_point(point) {}

bool RaidPledgeMemberService_SyncPledgeMastery::Do(CPledge& pledge, RaidServerInfo& server) {
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x24, pledge.GetDbId(), static_cast<int>(m_masteryId), m_point);
}

bool RaidPledgeMemberService_SyncPledgeType::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a16260.
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x25, pledge.GetDbId(), m_characterDatabaseId, static_cast<int>(m_pledgeType));
}

bool RaidPledgeMemberService_SyncPledgeSkill::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a16338.
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x26, pledge.GetDbId(), m_skillId, m_skillLevel);
}

bool RaidPledgeMemberService_SyncPledgeLevel::Do(CPledge& pledge, RaidServerInfo& server) {
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 0x23, pledge.GetDbId(), m_level);
}

bool RaidPledgeMemberService_SetParamChanged::Do(CPledge& pledge, RaidServerInfo& server) {
    // Current L2Server 0x00a15718. The raid wire contract carries the
    // command byte, pledge DBID and the low target dword of the mask.
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 9, pledge.GetDbId(), static_cast<std::uint32_t>(m_flag));
}

bool RaidPledgeMemberService_SetDominionSiege::Do(CPledge& pledge, RaidServerInfo& server) {
    L2_GUARD_FUNCTION();
    return server.Send("cddd", 0x13, pledge.GetDbId(), m_nDominionSiegeId, m_nDominionSiegeStatus);
}

bool RaidAllianceService_Dismiss::Do(CAlliance& alliance, RaidServerInfo& server) {
    // Current L2Server 0x00a165b0. The complete concrete vtable at
    // 0x00f77a70 has this single slot.
    L2_GUARD_FUNCTION();
    return server.Send("cd", 0x17, alliance.m_data->m_nDbId);
}

bool RaidAllianceService_AddPledgeMember::Do(CAlliance& alliance, RaidServerInfo& server) {
    // Current L2Server 0x00a16410. The complete concrete vtable at
    // 0x00e4cbe0 has this single slot.
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 0x15, alliance.m_data->m_nDbId, m_pledgeDatabaseId);
}

bool RaidAllianceService_DelPledgeMember::Do(CAlliance& alliance, RaidServerInfo& server) {
    // Current L2Server 0x00a164e0. The complete concrete vtable at
    // 0x00e4c140 has this single slot.
    L2_GUARD_FUNCTION();
    return server.Send("cdd", 0x16, alliance.m_data->m_nDbId, m_pledgeDatabaseId);
}

void RaidClientManager::Load() {
    // Current L2Server 0x00a1d74c.
    L2_GUARD_FUNCTION();
    constexpr const wchar_t* kFileName = L"..\\Script\\RaidServerData.xml";
    XMLElement* const root = CXmlLoader::inst().Load(kFileName);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load %s",
        //     L"Raid.cpp",
        //     0x5f0,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load %s",
        //     L"Raid.cpp",
        //     0x5f0,
        //     kFileName)
        L2_LOG_ERROR(Log, "Raid configuration could not be loaded: %s.", WideToUtf8(kFileName).c_str());
        return;
    }

    for (XMLElement* const section : root->m_vChild) {
        if (section == nullptr || !EqualsAsciiCaseInsensitive(section->m_sName, u"MoveTo")) {
            continue;
        }
        for (XMLElement* const server : section->m_vChild) {
            if (server == nullptr) {
                continue;
            }
            const int worldId = XMLUtil::GetAttributeValueAsInt(server, std::u16string(u"WorldID"), 0);
            std::u16string internalAddress =
                XMLUtil::GetAttributeValue(server, std::u16string(u"InternalIP"), std::u16string(u""));
            std::array<char, 0x10> addressBuffer{};
            UnicodeToAnsi(reinterpret_cast<SharedUtilsWideChar*>(internalAddress.data()), 0x10, addressBuffer.data(), 0x10);
            const std::uint32_t internalIp = ::inet_addr(addressBuffer.data());
            std::u16string externalAddress =
                XMLUtil::GetAttributeValue(server, std::u16string(u"ExternalIP"), std::u16string(u""));
            UnicodeToAnsi(reinterpret_cast<SharedUtilsWideChar*>(externalAddress.data()), 0x10, addressBuffer.data(), 0x10);
            const std::uint32_t externalIp = ::inet_addr(addressBuffer.data());
            const int port = XMLUtil::GetAttributeValueAsInt(server, std::u16string(u"Port"), 0);

            m_ServerListMap[worldId] = new RaidServerInfo(worldId, internalIp, port);
            m_mapExternalIP[worldId] = externalIp;
            if (m_ServerListMap.size() > 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Raid system does NOT support multiple move to server!!!!")
                // L2_LOG_ERROR(Log,
                //     L"Raid system does NOT support multiple move to server!!!!")
                L2_LOG_ERROR(
                    Log,
                    "Multiple raid destinations are configured; this configuration is unsupported and the newly loaded "
                    "entry remains registered."
                );
            }
        }
    }

    root->ReleaseChild();
    delete root;
}

void RaidClientManager::Init() {
    // Current L2Server 0x00a17444.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_ServerListMap) {
        if (entry.second != nullptr) {
            entry.second->Init();
        }
    }
}

RaidUserCountLong RaidClientManager::GetCurrentRaidUserCount() {
    // Current L2Server.exe 0x00a17538 traverses the live map
    // without taking m_lock. Win64 long is the summed signed 32-bit lane.
    L2_GUARD_FUNCTION();
    std::int32_t currentUserCount = 0;
    for (const auto& entry : m_ServerListMap) {
        if (entry.second != nullptr) {
            currentUserCount = runtime::AddInt32Wrapping(currentUserCount, entry.second->GetCurrentQuota());
        }
    }
    return static_cast<RaidUserCountLong>(currentUserCount);
}

void RaidClientManager::CreateThread() {
    // Current L2Server 0x00a1e040 creates logical RAID thread 19 suspended.
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(RaidClientThread, nullptr, true);
    m_nConnectThread = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::RAID, m_hConnectThread);
}

void RaidClientManager::ConnectToRaidServer() {
    // Current L2Server 0x00a15408.
    L2_GUARD_FUNCTION();
    static_cast<void>(runtime::ResumeOwnedThread(m_hConnectThread));
}

void RaidClientManager::Connected(const int worldId, const bool connected) {
    // Current L2Server 0x00a19950.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetServerInfo(worldId);
    if (server == nullptr) {
        return;
    }
    server->Connected(connected);
    if (!connected) {
        // The target clears the complete pointer lane before clearing the
        // adjacent sendable byte.
        server->m_pRaidSocket = nullptr;
        server->m_bSendable = false;
    } else {
        server->m_bSendable = true;
    }
}

void RaidClientManager::DeleteAllRaidUser(const int worldId) {
    // Current L2Server 0x00a19a14.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetServerInfo(worldId);
    if (server != nullptr) {
        server->DeleteAllReserver();
    }
}

void RaidClientManager::RaidReserveRequestPacket(const int worldId, const int characterDatabaseId, const bool success) {
    // Current L2Server 0x00a1bfb4.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
    RaidServerInfo* const server = GetServerInfo(worldId);

    if (!user) {
        if (server != nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RaidSocket has Invalid User[%d] Exist Info",
            //     "Raid.cpp",
            //     0x8ae,
            //     characterDatabaseId)
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] RaidSocket has Invalid User[%d] Exist Info",
            //     "Raid.cpp",
            //     0x8ae,
            //     characterDatabaseId)
            L2_LOG_WARN(
                Log,
                "Raid reservation response from world %d has no player for character database ID %d; removing the "
                "reservation.",
                worldId, characterDatabaseId
            );
            server->DeleteReserver(characterDatabaseId, true);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RaidSocket has Invalid User[%d] Not Exist Info",
            //     "Raid.cpp",
            //     0x8b3,
            //     characterDatabaseId)
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] RaidSocket has Invalid User[%d] Not Exist Info",
            //     "Raid.cpp",
            //     0x8b3,
            //     characterDatabaseId)
            L2_LOG_WARN(
                Log,
                "Raid reservation response ignored: character database ID %d and server information for world %d are "
                "unavailable.",
                characterDatabaseId, worldId
            );
        }
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (server == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RaidSocket has invalid world id[%d]",
        //     "Raid.cpp",
        //     0x8a8,
        //     worldId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] RaidSocket has invalid world id[%d]",
        //     "Raid.cpp",
        //     0x8a8,
        //     worldId)
        L2_LOG_WARN(
            Log,
            "Raid reservation response ignored for character database ID %d: server information for world %d is "
            "unavailable.",
            characterDatabaseId, worldId
        );
        return;
    }

    if (!success) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][RAID][07] S_EX_RAID_RESERVE_RESULT[failure] protocol is sent")
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][RAID][07] S_EX_RAID_RESERVE_RESULT[failure] protocol is sent")
        L2_LOG_WARN(
            Log,
            "Raid reservation declined by world %d for character database ID %d; sending failure response to the player.",
            worldId, characterDatabaseId
        );
        socket->Send("chd", 0xfe, 0xb7, 0);
        user->GetSocket()->SendSystemMessage(u"SYS", u"WorldServe is full. can not move to raid server");
        socket->SetRaidStatus(URS_NONE);
        user->m_RaidInfo.m_nRaidWorldId = 0;
        server->DeleteReserver(characterDatabaseId, false);
        return;
    }

    user->LeaveWorldForRaid();
    user->m_RaidInfo.m_OriginPos = user->GetPos();
    socket->AddRaidStatus(URS_ACCEPTED_REQUEST);
    new RaidEnterVerifier(user->GetSocket());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][RAID][07] S_EX_RAID_RESERVE_RESULT[success] protocol is sent")
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][RAID][07] S_EX_RAID_RESERVE_RESULT[success] protocol is sent")
    L2_LOG_INFO(
        Log, "Raid reservation accepted by world %d for character database ID %d; sending transfer details to the player.",
        worldId, characterDatabaseId
    );
    socket->Send("chddddd", 0xfe, 0xb7, 1, GetExternalIP(worldId), g_nWorldId, characterDatabaseId, user->GetServerId());
    db.RequestReloadUserFlagOn(*user.get());
    RelayClient::Inst()->SendEnterRaidServer(user.get());
}

void RaidClientManager::RaidReturnToOriginPacket(
    const int characterDatabaseId, const int characterServerId, const int raidServerWorldId
) {
    // Current L2Server 0x00a18f38.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
    if (!user) {
        if (RaidServerInfo* const server = GetServerInfo(raidServerWorldId)) {
            server->Send("cdd", 0x1d, characterServerId, 0);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [Raid] ClientSocket does not exist. user[%d][%d] SID[%d]",
        //     L"Raid.cpp",
        //     0x8e5,
        //     characterDatabaseId,
        //     g_nWorldId,
        //     characterServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [Raid] ClientSocket does not exist. user[%d][%d] SID[%d]",
        //     L"Raid.cpp",
        //     0x8e5,
        //     characterDatabaseId,
        //     g_nWorldId,
        //     characterServerId)
        L2_LOG_WARN(
            Log,
            "Raid return request rejected: character database ID %d is unavailable in local world %d (character server ID "
            "%d).",
            characterDatabaseId, g_nWorldId, characterServerId
        );
        return;
    }

    RaidServerInfo* const server = GetServerInfo(user->m_RaidInfo.m_nRaidWorldId);
    if (server == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RaidServerInfo does not exist",
        //     L"Raid.cpp",
        //     0x8c5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RaidServerInfo does not exist",
        //     L"Raid.cpp",
        //     0x8c5)
        L2_LOG_WARN(
            Log,
            "Raid return request rejected for character database ID %d: server information for raid world %d is "
            "unavailable.",
            characterDatabaseId, user->m_RaidInfo.m_nRaidWorldId
        );
        return;
    }

    if (user->m_data->m_nWorldId == g_nWorldId && user->m_RaidInfo.m_nRaidWorldId > 0) {
        CUserSocket* const socket = user->GetSocket();
        socket->AddRaidStatus(URS_RETURN_FROM_RAID);
        socket->SetRaidCharServerId(characterServerId);
        server->Send("cdd", 0x1d, characterServerId, 1);
    } else {
        server->Send("cdd", 0x1d, characterServerId, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RCS_REQUEST_CONFIRMED_RETURN failed",
        //     L"Raid.cpp",
        //     0x8d6)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] RCS_REQUEST_CONFIRMED_RETURN failed",
        //     L"Raid.cpp",
        //     0x8d6)
        L2_LOG_WARN(
            Log,
            "Raid return confirmation rejected for character database ID %d: player world %d, local world %d, raid world "
            "%d.",
            characterDatabaseId, user->m_data->m_nWorldId, g_nWorldId, user->m_RaidInfo.m_nRaidWorldId
        );
    }
}

bool RaidClientManager::RaidRestartOnRaidServerPacket(const int characterDatabaseId, const int characterServerId) {
    // Current L2Server 0x00a191fc.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][RAID] RestartOnRaidServerPacket [%d][%d] Server restart",
        //     characterDatabaseId,
        //     g_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][RAID] RestartOnRaidServerPacket [%d][%d] Server restart",
        //     characterDatabaseId,
        //     g_nWorldId)
        L2_LOG_WARN(
            Log, "Raid restart request rejected: character database ID %d is unavailable in local world %d.",
            characterDatabaseId, g_nWorldId
        );
        return false;
    }

    if (user->m_data->m_nWorldId != g_nWorldId || user->m_RaidInfo.m_nRaidWorldId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][RAID] RestartOnRaidServerPacket [%d][%d] Invalid user",
        //     user->GetDBID(),
        //     user->m_RaidInfo.m_nRaidWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][RAID] RestartOnRaidServerPacket [%d][%d] Invalid user",
        //     user->GetDBID(),
        //     user->m_RaidInfo.m_nRaidWorldId)
        L2_LOG_WARN(
            Log,
            "Raid restart request rejected for character database ID %u: player world %d, local world %d, raid world %d.",
            static_cast<unsigned int>(user->GetDBID()), user->m_data->m_nWorldId, g_nWorldId, user->m_RaidInfo.m_nRaidWorldId
        );
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    socket->AddRaidStatus(URS_RESTART_ON_RAID);
    RaidServerInfo* const server = GetServerInfo(user->m_RaidInfo.m_nRaidWorldId);
    if (server == nullptr) {
        return false;
    }
    server->Send("cdd", 0x1e, characterServerId, 1);
    return true;
}

void RaidClientManager::AddRaidPledge(const int worldId, const int pledgeDatabaseId) {
    // Current L2Server 0x00a1cdc4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    auto found = m_mapRaidPledge.find(pledgeDatabaseId);
    if (found == m_mapRaidPledge.end()) {
        m_mapRaidPledge[pledgeDatabaseId].push_back(worldId);
    } else if (std::find(found->second.begin(), found->second.end(), worldId) == found->second.end()) {
        found->second.push_back(worldId);
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
    if (pledge) {
        pledge->SetUnderRaid(true);
    }
}

void RaidClientManager::AddRaidPledge(User& user) {
    // Current L2Server 0x00a1d3c0.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user.GetPledge();
    if (pledge == nullptr) {
        return;
    }
    AddRaidPledge(user.m_RaidInfo.m_nRaidWorldId, static_cast<int>(pledge->GetDbId()));
    pledge->SetUnderRaid(true);
}

void RaidClientManager::RaidServerVersionPacket(
    const int worldId, const int result, const int quota, const unsigned char* packet
) {
    // Current L2Server 0x00a1d1d0.
    L2_GUARD_FUNCTION();
    int raidServerTimezone = 0;
    packet = ::Disassemble(packet, "d", &raidServerTimezone);
    if (result == 1) {
        int pledgeCount = 0;
        packet = ::Disassemble(packet, "d", &pledgeCount);
        for (int index = 0; index < pledgeCount; ++index) {
            int pledgeDatabaseId = 0;
            packet = ::Disassemble(packet, "d", &pledgeDatabaseId);
            AddRaidPledge(worldId, pledgeDatabaseId);
        }
    }

    RaidServerInfo* const server = GetServerInfo(worldId);
    if (server == nullptr) {
        return;
    }
    server->SetSendable(result == 1);
    server->m_Quota = quota;
    if (static_cast<unsigned int>(raidServerTimezone + 12) >= 26U) {
        return;
    }

    long timezoneSeconds = 0;
#if defined(_WIN32)
    const int timezoneResult = ::_get_timezone(&timezoneSeconds);
#else
    ::tzset();
    timezoneSeconds = ::timezone;
    const int timezoneResult = 0;
#endif
    if (timezoneResult == 0) {
        const int localTimezone = static_cast<int>(timezoneSeconds / 0xe10L);
        SharedRaidServerData* const sharedData = SharedRaidServerData::GetInfo();
        sharedData->m_nRaidServerTimeDiff = localTimezone - raidServerTimezone;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] My Sever Timezone [%d], Diff [%d]",
        //     L"Raid.cpp",
        //     0x863,
        //     localTimezone,
        //     sharedData->m_nRaidServerTimeDiff)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] My Sever Timezone [%d], Diff [%d]",
        //     L"Raid.cpp",
        //     0x863,
        //     localTimezone,
        //     sharedData->m_nRaidServerTimeDiff)
        L2_LOG_INFO(
            Log,
            "Raid time offset updated: local standard-time offset %d hours west of UTC; local-minus-raid difference %d "
            "hours.",
            localTimezone, sharedData->m_nRaidServerTimeDiff
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to get Timezone [%d]",
        //     L"Raid.cpp",
        //     0x867,
        //     0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to get Timezone [%d]",
        //     L"Raid.cpp",
        //     0x867,
        //     0)
        L2_LOG_ERROR(
            Log, "Cannot read the local time-zone offset (error %d); raid time difference remains unchanged.", timezoneResult
        );
    }
}

void RaidClientManager::RaidUserConfirmedPacket(const int characterDatabaseId) {
    // Current L2Server 0x00a1d490.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    socket->DecUserCount(false);
    socket->AddRaidStatus(URS_ON_RAID);
    AddRaidPledge(*user.get());
}

std::uint32_t RaidClientManager::GetExternalIP(int worldId) {
    L2_GUARD_FUNCTION();

    if (m_mapExternalIP.empty()) {
        return 0;
    }

    const auto found = m_mapExternalIP.find(worldId);
    return found != m_mapExternalIP.end() ? found->second : 0;
}

RaidServerInfo* RaidClientManager::GetServerInfo(int worldId) {
    L2_GUARD_FUNCTION();

    const auto found = m_ServerListMap.find(worldId);
    return found != m_ServerListMap.end() ? found->second : nullptr;
}

void RaidClientManager::ShowRaidInfo(User& user) {
    // Current L2Server 0x00a18d28.
    L2_GUARD_FUNCTION();

    auto externalIterator = m_mapExternalIP.begin();
    for (auto serverIterator = m_ServerListMap.begin(); serverIterator != m_ServerListMap.end();
        ++serverIterator, ++externalIterator) {
        RaidServerInfo* const server = serverIterator->second;

        in_addr internalAddress{};
        internalAddress.s_addr = server->m_ServerAddr;
        const std::wstring internalText = Utf8ToWide(::inet_ntoa(internalAddress));

        in_addr externalAddress{};
        externalAddress.s_addr = externalIterator->second;
        const std::wstring externalText = Utf8ToWide(::inet_ntoa(externalAddress));

        std::array<char16_t, 512> message;
        static_cast<void>(FormatUtf16To(
            message.data(), 0x1ff, u"MoveTo: %d InIP: %s EXIP: %s", server->m_nWorldId, internalText.c_str(),
            externalText.c_str()
        ));
        user.GetSocket()->SendSystemMessage(u"SYS", message.data());
    }
}

RaidServerInfo* RaidClientManager::GetHostServerInfoForWorldRaid() const {
    // Current L2Server 0x00a16ca0.
    L2_GUARD_FUNCTION();
    const auto first = m_ServerListMap.begin();
    return first != m_ServerListMap.end() ? first->second : nullptr;
}

bool RaidClientManager::RequestWorldRaidStart(const std::uint32_t npcSid) const {
    // Current L2Server.exe 0x00a16d3c.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetHostServerInfoForWorldRaid();
    if (server != nullptr) {
        server->Send("cd", 0x20, npcSid);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_START sent: %d",
        //     L"Raid.cpp",
        //     0x931,
        //     npcSid)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_START sent: %d",
        //     L"Raid.cpp",
        //     0x931,
        //     npcSid)
        L2_LOG_INFO(Log, "World raid start send attempted for NPC server ID %u.", static_cast<unsigned int>(npcSid));
    }
    return server != nullptr;
}

void RaidClientManager::RequestWorldRaidStop(const std::uint32_t npcSid) const {
    // Current L2Server.exe 0x00a16e2c.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetHostServerInfoForWorldRaid();
    if (server != nullptr) {
        server->Send("cd", 0x21, npcSid);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_STOP sent: %d",
        //     L"Raid.cpp",
        //     0x943,
        //     npcSid)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_STOP sent: %d",
        //     L"Raid.cpp",
        //     0x943,
        //     npcSid)
        L2_LOG_INFO(Log, "World raid stop send attempted for NPC server ID %u.", static_cast<unsigned int>(npcSid));
    }
}

void RaidClientManager::RequestWorldRaidBroadcastEvent(int first, int second, int third) const {
    // Current L2Server 0x00a16f18.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetHostServerInfoForWorldRaid();
    if (server != nullptr) {
        server->Send("cddd", 0x22, first, second, third);
    }
}

bool RaidClientManager::DisconnectRaidUser(int worldId, int characterDatabaseId) {
    // Current L2Server 0x00a19ac8.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetServerInfo(worldId);
    if (server != nullptr) {
        server->DisconnectUser(g_nWorldId, characterDatabaseId);
    }
    return server != nullptr;
}

bool RaidClientManager::DeleteRaidReserved(const int worldId, const int characterDatabaseId, const bool notifyRaidServer) {
    // Current L2Server 0x00a1bed8.
    L2_GUARD_FUNCTION();
    RaidServerInfo* const server = GetServerInfo(worldId);
    return server != nullptr && server->DeleteReserver(characterDatabaseId, notifyRaidServer);
}

void RaidClientManager::ReloadUserIfReturnedCompletely(User& user) {
    // Current L2Server 0x00a1c538.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = user.GetSocket();
    if (socket == nullptr) {
        return;
    }

    {
        L2_GUARD(*user.m_lock);
        if (!socket->IsCompletelyReturned()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ERROR][RAID][05] Invalid Raid User Info[%d][%d] Socket Closed",
            //     static_cast<int>(user.GetDBID()),
            //     user.m_RaidInfo.m_nRaidWorldId)
            // L2_LOG_ERROR(Log,
            //     L"[ERROR][RAID][05] Invalid Raid User Info[%d][%d] Socket Closed",
            //     static_cast<int>(user.GetDBID()),
            //     user.m_RaidInfo.m_nRaidWorldId)
            L2_LOG_ERROR(
                Log, "Closing player connection: raid return is incomplete for character database ID %u (raid world %d).",
                static_cast<unsigned int>(user.GetDBID()), user.m_RaidInfo.m_nRaidWorldId
            );
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return;
        }
        if (!socket->SetReloadState()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ERROR][RAID][05] Invalid Raid User State[%d][%d] Socket Closed",
            //     static_cast<int>(user.GetDBID()),
            //     user.m_RaidInfo.m_nRaidWorldId)
            // L2_LOG_ERROR(Log,
            //     L"[ERROR][RAID][05] Invalid Raid User State[%d][%d] Socket Closed",
            //     static_cast<int>(user.GetDBID()),
            //     user.m_RaidInfo.m_nRaidWorldId)
            L2_LOG_ERROR(
                Log,
                "Closing player connection: reload-state transition failed after raid return for character database ID %u "
                "(raid world %d).",
                static_cast<unsigned int>(user.GetDBID()), user.m_RaidInfo.m_nRaidWorldId
            );
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return;
        }

        socket->SetRaidStatus(URS_NONE);
    }
    DeleteRaidReserved(user.m_RaidInfo.m_nRaidWorldId, static_cast<int>(user.GetDBID()), false);
    user.GetSocket()->IncUserCount(false);

    const FVector originForZ = user.m_RaidInfo.m_OriginPos;
    const FVector originForY = user.m_RaidInfo.m_OriginPos;
    const FVector originForX = user.m_RaidInfo.m_OriginPos;
    db.ReloadCharacter(
        user, runtime::Win64CvttDoubleToInt32(originForX.x), runtime::Win64CvttDoubleToInt32(originForY.y),
        runtime::Win64CvttDoubleToInt32(originForZ.z)
    );
}

void RaidClientManager::RestartUserIfCompletely(User& user) {
    // Current L2Server 0x00a1c7c0.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = user.GetSocket();
    if (socket == nullptr) {
        return;
    }

    {
        L2_GUARD(*user.m_lock);
        if (!socket->IsReadyToRestart()) {
            return;
        }
        socket->SetRaidStatus(URS_NONE);
    }

    socket->Send("ch", 0xfe, S_EX_RESTART_RESPONSE);
    if (static_cast<int>(socket->GetAccountID()) > 0) {
        g_UserSocketMapEnterChar.DeleteObject(
            static_cast<int>(socket->GetAccountID()), const_cast<char*>("Raid.cpp"), 0x69e
        );
    }
    DeleteRaidReserved(user.m_RaidInfo.m_nRaidWorldId, static_cast<int>(user.GetDBID()), false);
    user.SendLogout();
    socket->m_nCloseReason = SCR_RAID_QUIT;
    {
        L2_GUARD(socket->mLock);
        socket->UnBindUser();
    }
    socket->IncUserCount(false);
    db.RequestLoadChar(user.GetAccountID(), socket);
}

bool RaidServerInfo::DeleteReserver(const int characterDatabaseId, const bool notifyRaidServer) {
    // Current L2Server immediate owner 0x00a1bde0.
    L2_GUARD_FUNCTION();
    if (m_bSendable && m_pRaidSocket != nullptr && notifyRaidServer) {
        m_pRaidSocket->Send("cd", 2, characterDatabaseId);
    }
    runtime::raw_atomic::AddI32(&m_CurrentQuota, -1);
    return m_mapRaidUsers.DeleteObject(characterDatabaseId, const_cast<char*>("Raid.cpp"), 0x5be);
}

void RaidServerInfo::DeleteAllReserver() {
    // Current L2Server 0x00a18ab4 / exact CObjectMap clear callback.
    L2_GUARD_FUNCTION();
    CLeaveWorldAllRaidUser callback;
    m_mapRaidUsers.FOR_ALL_DO_AND_CLEAR(callback, const_cast<char*>("Raid.cpp"), 0x5c7);
}

bool RaidClientManager::RequestRaidPledgeMemberService(CPledge& pledge, RaidPledgeMemberService& service) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return false;
    }

    L2_GUARD(m_lock);
    const auto group = m_mapRaidPledge.find(static_cast<int>(pledge.GetDbId()));
    if (group == m_mapRaidPledge.end()) {
        pledge.SetUnderRaid(false);
        return false;
    }

    for (const int worldId : group->second) {
        if (RaidServerInfo* const server = GetServerInfo(worldId)) {
            service.Do(pledge, *server);
        }
    }
    return true;
}

bool RaidClientManager::RequestPledgeChattingRelay(
    CPledge& pledge,
    const int serverId,
    const int characterDatabaseId,
    const char16_t* const senderName,
    const char16_t* const message,
    const SayPacketType type,
    const int excludedWorldId
) {
    // Current L2Server 0x00a19b9c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto group = m_mapRaidPledge.find(static_cast<int>(pledge.GetDbId()));
    if (group == m_mapRaidPledge.end()) {
        pledge.SetUnderRaid(false);
        return false;
    }


    for (const int worldId : group->second) {
        if (worldId != excludedWorldId) {
            if (RaidServerInfo* const server = GetServerInfo(worldId)) {
                server->Send(
                    "cdddSSd", 4, pledge.GetDbId(), serverId, characterDatabaseId, senderName, message, type
                );
            }
        }
    }
    return true;
}

bool RaidClientManager::RequestAllianceService(CAlliance& alliance, RaidAllianceService& service) {
    // Current L2Server 0x00a19fbc.
    L2_GUARD_FUNCTION();

    std::vector<CPledge*> pledges;
    alliance.GetAllMemberPledge(pledges);

    bool result = false;
    for (CPledge* const pledge : pledges) {
        L2_GUARD(m_lock);
        const auto group = m_mapRaidPledge.find(static_cast<int>(pledge->GetDbId()));
        if (group == m_mapRaidPledge.end()) {
            continue;
        }
        for (const int worldId : group->second) {
            if (RaidServerInfo* const server = GetServerInfo(worldId)) {
                service.Do(alliance, *server);
            }
        }
        result = true;
    }
    return result;
}

bool RaidClientManager::RequestRaidReserve(const int worldId, User* const user) {
    // Current L2Server 0x00a1cfe4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    RaidServerInfo* const server = GetServerInfo(worldId);
    if (server == nullptr || !server->CheckCondition(user, true)) {
        return false;
    }
    FVector position = user->GetPos();
    return server->RequestReserveUser(user, position);
}

bool RaidClientManager::RequestRaidReserve(const int worldId, User* const user, FVector position) {
    // Current L2Server 0x00a1d0e4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    RaidServerInfo* const server = GetServerInfo(worldId);
    return server != nullptr && server->CheckCondition(user, true) && server->RequestReserveUser(user, position);
}
