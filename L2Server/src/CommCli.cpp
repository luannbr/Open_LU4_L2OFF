#include "CommCli.h"
#include "CConfig.h"
#include "CommSocket.h"
#include "DB.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Setting.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <array>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

CCommCli commcli;

namespace {

std::uint32_t g_socketSendIndex = 0;

std::mutex g_commWorkerLock;
std::condition_variable g_commWorkerWake;
std::uint64_t g_commWorkerGeneration = 0;


std::array<char16_t, 20> CommAddressString(const ::in_addr address) {
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
    std::array<char, 20> narrow{};
    static_cast<void>(std::snprintf(
        narrow.data(), narrow.size(), "%d.%d.%d.%d", static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
        static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
    ));

    std::array<char16_t, 20> wide{};
    std::size_t index = 0;
    while (index + 1U < wide.size() && narrow[index] != '\0') {
        wide[index] = static_cast<char16_t>(static_cast<unsigned char>(narrow[index]));
        ++index;
    }
    return wide;
}

void CloseCommNativeSocket(const std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

void WakeCommWorker() {
    {
        std::lock_guard<std::mutex> guard(g_commWorkerLock);
        ++g_commWorkerGeneration;
    }
    g_commWorkerWake.notify_one();
}

std::uint32_t CommWorker(void*) {
    // Current L2Server 0x007f87b8 / 0x007f562c. SuspendThread is expressed
    // by an owner-local generation wait. No target layout is extended.
    CThreadManager::RegisterThisThread(ThreadID::L2COMM);
    InitializeCurrentIOBufferPool(0x80);

    std::uint64_t observed = 0;
    {
        std::lock_guard<std::mutex> guard(g_commWorkerLock);
        observed = g_commWorkerGeneration;
    }
    for (;;) {
        static_cast<void>(commcli.Init());
        std::unique_lock<std::mutex> guard(g_commWorkerLock);
        g_commWorkerWake.wait(guard, [&] { return g_commWorkerGeneration != observed; });
        observed = g_commWorkerGeneration;
    }
}

} // namespace

CCommCli::CCommCli() {
    // Current L2Server 0x005a9c14.
    // Clear initializes only status/sendable/tick/auto-connect. Current leaves
    // padding plus the thread-id/handle lanes untouched until their owners use
    // them, so do not value-initialize those scalar members here.
    L2_GUARD_FUNCTION();
    Clear();
}

CCommCli::~CCommCli() {
    // Current L2Server 0x005a9e08.
    m_hConnectThread = nullptr;
}

CCommCli* CCommCli::Inst() noexcept { return &commcli; }

void CCommCli::Clear() {
    // Current L2Server 0x005a95b4.
    L2_GUARD_FUNCTION();
    m_nConnectTick = 0;
    m_nStatus = 1;
    m_CommSendable = false;
    m_bAutoConnect = true;
}

void CCommCli::SetCliStatus(const int status) {
    // Current L2Server 0x005a9658.
    L2_GUARD_FUNCTION();
    m_nStatus = status;
    m_CommSendable = status == 3;
}

bool CCommCli::ConnectToCommD() {
    // Current L2Server 0x005a96fc.
    L2_GUARD_FUNCTION();
    if ((m_nStatus == 1 || m_nStatus == 4) && runtime::GetTickCount() - m_nConnectTick > 60000U) {
        WakeCommWorker();
        static_cast<void>(runtime::ResumeOwnedThread(m_hConnectThread));
        return true;
    }
    return false;
}

bool CCommCli::CreateThread() {
    // Current L2Server 0x005a97b8.
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(CommWorker, nullptr, true);
    m_nConnectThreadId = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::L2COMM, m_hConnectThread);
    return true;
}

void CCommCli::ToggleAutoConnect() {
    // Current L2Server 0x005a9888. The Win32 menu check is a host-UI side
    // effect. The target-owned state transition is the boolean toggle.
    L2_GUARD_FUNCTION();
    m_bAutoConnect = !m_bAutoConnect;
}

bool CCommCli::CheckCommSvr(User* const user, const bool notify) {
    // Current L2Server 0x005a9940.
    L2_GUARD_FUNCTION();
    if (commUseSvr == 1) {
        if (!m_CommSendable && notify) {
            user->SendSystemMessage(SystemMessageId::COMMUNITY_SERVER_UNDER_MAINTENANCE);
            user->GetSocket()->Send("cd", 0x7b, 0);
            ConnectToCommD();
        }
        return true;
    }
    user->SendSystemMessage(SystemMessageId::COMMUNITY_SERVER_UNDER_MAINTENANCE);
    user->GetSocket()->Send("cd", 0x7b, 0);
    return false;
}

bool CCommCli::CheckCommSvr(const bool connect) {
    // Current L2Server 0x005a9a84.
    L2_GUARD_FUNCTION();
    if (commUseSvr != 1) {
        return false;
    }
    if (!m_CommSendable && connect) {
        ConnectToCommD();
    }
    return true;
}

bool CCommCli::Connect(CSPointer<CCommSocket>& destination) {
    // Current L2Server 0x005a9e94.
    L2_GUARD_FUNCTION();
    if (m_CommSendable) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CCommCli::Connect is sendable")
        // L2_LOG_ERROR(Log,
        //     L"CCommCli::Connect is sendable")
        L2_LOG_WARN(
            Log, "Community connection setup was requested while the client is already sendable; setup still proceeds."
        );
    }

    const int created = ::socket(AF_INET, SOCK_STREAM, 0);
    if (created == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't bind socket to comm server")
        // L2_LOG_ERROR(Log,
        //     L"Can't bind socket to comm server")
        const int socketError = portable::GetNativeSocketLastError();
        L2_LOG_ERROR(Log, "Cannot create the community connection socket (socket error %d).", socketError);
        return false;
    }
    const std::uint64_t nativeSocket = static_cast<std::uint64_t>(created);

    ::sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    if (::bind(created, reinterpret_cast<const ::sockaddr*>(&localAddress), sizeof(localAddress)) == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't bind socket to comm server")
        // L2_LOG_ERROR(Log,
        //     L"Can't bind socket to comm server")
        const int socketError = portable::GetNativeSocketLastError();
        L2_LOG_ERROR(
            Log, "Cannot bind the community connection socket to a local address (socket error %d); closing it.", socketError
        );
        CloseCommNativeSocket(nativeSocket);
        return false;
    }

    ::sockaddr_in remoteAddress{};
    remoteAddress.sin_family = AF_INET;
    remoteAddress.sin_addr = commServerAddr;
    remoteAddress.sin_port = htons(commServerPort);
    if (::connect(created, reinterpret_cast<const ::sockaddr*>(&remoteAddress), sizeof(remoteAddress)) == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't connect to comm server")
        // L2_LOG_ERROR(Log,
        //     L"Can't connect to comm server")
        const int socketError = portable::GetNativeSocketLastError();
        const auto address = CommAddressString(remoteAddress.sin_addr);
        L2_LOG_ERROR(
            Log, "Cannot connect to community server %s:%u (socket error %d); closing the socket.",
            Utf16ToUtf8(std::u16string_view(address.data())).c_str(), static_cast<unsigned>(commServerPort), socketError
        );
        CloseCommNativeSocket(nativeSocket);
        return false;
    }

    CCommSocket* const socket = new CCommSocket(nativeSocket, g_CommPacketTable);
    if (destination) {
        destination.SetObject(socket, ObjectRefType::FunctionChain);
    } else {
        CSPointer<CCommSocket> replacement(socket, ObjectRefType::FunctionChain);
        destination = replacement;
    }

    const bool bound = portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, const std::size_t length) {
            return portable::FeedSocketRead(static_cast<CCommSocket*>(owner), data, length);
        },
        [](void* owner, const int error) { portable::FeedSocketTerminal(static_cast<CCommSocket*>(owner), error); }
    );
    if (!bound) {
        CloseCommNativeSocket(nativeSocket);
    }
    destination->Initialize();
    return true;
}

CSPointer<CCommSocket> CCommCli::GetCommSocket(const int index) const {
    // Current L2Server 0x005aa118.
    L2_GUARD_FUNCTION();
    return index == 0 ? m_spCommSocket0 : m_spCommSocket1;
}

CSPointer<CCommSocket> CCommCli::GetCommSocket() const {
    // Current L2Server 0x005aa218. The signed counter wraps
    // with the target's 32-bit lane. Only its low bit selects the socket.
    L2_GUARD_FUNCTION();
    ++g_socketSendIndex;
    return GetCommSocket(g_socketSendIndex & 1U);
}

void CCommCli::Send(const char* const format, ...) const {
    // Current L2Server 0x005aa2dc.
    L2_GUARD_FUNCTION();
    CSPointer<CCommSocket> socket = GetCommSocket();
    std::va_list arguments;
    va_start(arguments, format);
    if (m_CommSendable && socket) {
        socket->SendV(format, arguments);
    }
    va_end(arguments);
}

void CCommCli::RequestBBSInit(
    User* const user, const int value1, int boardId, int value3, const int value4, const int value5
) {
    // Current L2Server 0x005aa3cc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (boardId == 0 && value3 == 0) {
        boardId = CSetting::inst().GetRestartPointSys(user->m_data->m_Race, user->m_data->m_Pos, 1);
        value3 = static_cast<int>(user->m_data->m_nPledgeID);
    }
    if (CheckCommSvr(user, true)) {
        Send(
            "cddddddddd", 2, user->GetServerId(), user->GetDBID(), value1, boardId, value3, value4, value5,
            user->m_data->m_nWorldId, user->GetAccountID()
        );
    }
}

void CCommCli::RequestBBSList(User* const user, const int boardId, const int page, const int flags) {
    // Current L2Server 0x005aa59c.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int effectiveFlags = flags | (user->m_data->m_bPledgeMaster ? 8 : 0);
        Send(
            "cdddddddd", 7, user->GetServerId(), user->GetDBID(), user->m_data->m_nPledgeID, boardId, effectiveFlags, page,
            user->m_data->m_nWorldId, user->GetAccountID()
        );
    }
}

void CCommCli::RequestBBSRead(
    User* const user,
    const int boardId,
    const int articleId,
    const int boardType,
    const int listMode,
    const int listPage,
    const int searchType,
    SharedUtilsWideChar* const search,
    const int searchStart,
    const int searchEnd
) {
    // Current L2Server 0x005aa708.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int effectiveSearchType = searchType | (user->m_data->m_bPledgeMaster ? 8 : 0);
        Send(
            "cddddddddddSddd", 6, user->GetServerId(), user->GetDBID(), user->m_data->m_nPledgeID, user->GetAccountID(),
            boardId, articleId, boardType, listMode, listPage, effectiveSearchType, search, searchStart, searchEnd,
            user->m_data->m_nWorldId
        );
    }
}

void CCommCli::RequestBBSReadThread(
    User* const user,
    const int boardId,
    const int articleId,
    const int boardType,
    const int listMode,
    const int listPage,
    const int searchType,
    SharedUtilsWideChar* const search,
    const int searchStart,
    const int searchEnd
) {
    // Current L2Server 0x005aa8c4.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int effectiveSearchType = searchType | (user->m_data->m_bPledgeMaster ? 8 : 0);
        Send(
            "cdddddddddSddd", 8, user->GetServerId(), user->GetDBID(), user->m_data->m_nPledgeID, boardId, articleId,
            boardType, listMode, listPage, effectiveSearchType, search, searchStart, searchEnd, user->m_data->m_nWorldId
        );
    }
}

void CCommCli::RequestBBSReadComment(
    User* const user,
    const int boardId,
    const int articleId,
    const int boardType,
    const int listMode,
    const int listPage,
    const int searchType,
    SharedUtilsWideChar* const search,
    const int searchStart,
    const int searchEnd
) {
    // Current L2Server 0x005aaa64.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int effectiveSearchType = searchType | (user->m_data->m_bPledgeMaster ? 8 : 0);
        Send(
            "cdddddddddSddd", 9, user->GetServerId(), user->GetDBID(), user->m_data->m_nPledgeID, boardId, articleId,
            boardType, listMode, listPage, effectiveSearchType, search, searchStart, searchEnd, user->m_data->m_nWorldId
        );
    }
}

void CCommCli::RequestBBSReply(User* const user, const int value1, const int value2, const int value3) {
    // Current L2Server 0x005aac04.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        CSharedCreatureData* const data = user->m_data;
        const int worldId = data->m_nWorldId;
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cdddddddd", 0x0a, serverId, databaseId, accountId, data->m_nPledgeID, value1, value2 | (pledgeMaster ? 8 : 0),
            value3, worldId
        );
    }
}

void CCommCli::RequestBBSWrite(
    User* const user,
    const int value1,
    const int value2,
    const int value3,
    SharedUtilsWideChar* const string1,
    SharedUtilsWideChar* const string2,
    SharedUtilsWideChar* const string3
) {
    // Current L2Server 0x005aad6c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::array<char16_t, 20> address = CommAddressString(user->GetSocket()->GetAddress());
    if (CheckCommSvr(user, true)) {
        const int effectiveValue2 = value2 | (user->m_data->m_bPledgeMaster ? 8 : 0);
        const int worldId = user->m_data->m_nWorldId;
        const char16_t* const accountName = user->m_sAccountName.data();
        const std::uint32_t accountId = user->GetAccountID();
        const char16_t* const name = user->GetName();
        CSharedCreatureData* const data = user->m_data;
        const unsigned int dbId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cdddSdSdddSSSSd", 5, serverId, dbId, data->m_nPledgeID, name, accountId, accountName, value1, effectiveValue2,
            value3, string1, string2, address.data(), string3, worldId
        );
    }
}

void CCommCli::RequestBBSWriteComment(
    User* const user, const int value1, const int value2, const int value3, SharedUtilsWideChar* const string
) {
    // Current L2Server 0x005aafbc.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        CSharedCreatureData* const data = user->m_data;
        const int worldId = data->m_nWorldId;
        const std::uint32_t accountId = user->GetAccountID();
        const char16_t* const name = user->GetName();
        const unsigned int dbId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cddSdSddddSd", 0x0b, serverId, dbId, name, accountId, user->m_sAccountName.data(), data->m_nPledgeID, value1,
            value2 | (pledgeMaster ? 8 : 0), value3, string, worldId
        );
    }
}

void CCommCli::RequestBBSWriteForm(User* const user, const int value1, const int value2) {
    // Current L2Server 0x005ab160.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send("cdddddd", 0x0c, serverId, databaseId, value1, value2 | (pledgeMaster ? 8 : 0), worldId, accountId);
    }
}

void CCommCli::RequestBBSSearch(
    User* const user,
    const int value1,
    const int value2,
    const int value3,
    const int value4,
    SharedUtilsWideChar* const search
) {
    // Current L2Server 0x005ab2ac.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        CSharedCreatureData* const data = user->m_data;
        const int worldId = data->m_nWorldId;
        const unsigned int dbId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cdddddddSd", 0x0d, serverId, dbId, data->m_nPledgeID, value1, value2 | (pledgeMaster ? 8 : 0), value3, value4,
            search, worldId
        );
    }
}

void CCommCli::RequestBBSSysinfo(User* const user) {
    // Current L2Server 0x005ac884.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        user->m_dwL2CommTick = runtime::GetTickCount();
        const std::uint32_t serverId = user->GetServerId();
        const unsigned int databaseId = user->GetDBID();
        Send("cdd", 0x17, databaseId, serverId);
    }
}

void CCommCli::RequestBBSModifyForm(User* const user, const int value1, const int value2, const int value3) {
    // Current L2Server 0x005ab41c.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        CSharedCreatureData* const data = user->m_data;
        const int worldId = data->m_nWorldId;
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cdddddddd", 0x0e, serverId, databaseId, accountId, data->m_nPledgeID, value1, value2 | (pledgeMaster ? 8 : 0),
            value3, worldId
        );
    }
}

void CCommCli::RequestBBSModify(
    User* const user,
    const int value1,
    const int value2,
    const int value3,
    SharedUtilsWideChar* const string1,
    SharedUtilsWideChar* const string2,
    SharedUtilsWideChar* const string3
) {
    // Current L2Server 0x005ab584.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int dbId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cdddddSSSdd", 0x0f, serverId, dbId, value1, value2 | (pledgeMaster ? 8 : 0), value3, string1, string2, string3,
            worldId, accountId
        );
    }
}

void CCommCli::RequestBBSDelete(
    User* const user, const int value1, const int value2, const int value3, const int value4, const int value5
) {
    // Current L2Server 0x005ab70c.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const bool pledgeMaster = user->m_data->m_bPledgeMaster;
        const int worldId = user->m_data->m_nWorldId;
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cdddddddd", 0x10, serverId, databaseId, value1, value2 | (pledgeMaster ? 8 : 0), value3, value4, value5, worldId
        );
    }
}

void CCommCli::RequestBBSGetFavorite(User* const user) {
    // Current L2Server 0x005ab85c.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        Send("cddd", 0x11, user->GetDBID(), user->GetServerId(), user->m_data->m_nWorldId);
    }
}

void CCommCli::RequestBBSAddFavorite(
    User* const user,
    SharedUtilsWideChar* const string1,
    SharedUtilsWideChar* const string2,
    const int value1,
    const int value2,
    const int value3,
    const int value4,
    const int value5,
    SharedUtilsWideChar* const string3
) {
    // Current L2Server 0x005ab958.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send(
            "cddSSdddddSd", 0x12, serverId, databaseId, string1, string2, value1, value2, value3, value4, value5, string3,
            worldId
        );
    }
}

void CCommCli::RequestBBSDelFavorite(User* const user, const int value) {
    // Current L2Server 0x005abac0.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send("cdddd", 0x13, serverId, databaseId, value, worldId);
    }
}

void CCommCli::RequestBBSGetAnnounce(User* const user, const int value) {
    // Current L2Server 0x005abbd0.
    L2_GUARD_FUNCTION();
    if (user != nullptr && CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const unsigned int databaseId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        Send("cdddd", 0x14, serverId, databaseId, value, worldId);
    }
}

void CCommCli::RequestGetMemoList(User* const user, const int page, SharedUtilsWideChar* const search) {
    // Current L2Server 0x005abce0.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        Send(
            "cddddddS", 0x15, user->GetDBID(), user->GetAccountID(), user->GetServerId(), 1, user->m_data->m_nWorldId, page,
            search
        );
    }
}

void CCommCli::RequestGetMemo(User* const user, const int memoId, const int page, SharedUtilsWideChar* const search) {
    // Current L2Server 0x005abe1c.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int databaseId = user->GetDBID();
        Send("cdddddddS", 0x15, databaseId, accountId, serverId, 2, worldId, memoId, page, search);
    }
}

void CCommCli::RequestCreateMemo(User* const user, SharedUtilsWideChar* const title, SharedUtilsWideChar* const contents) {
    // Current L2Server 0x005abf64.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t firstAccountId = user->GetAccountID();
        const char16_t* const name = user->GetName();
        const unsigned int firstDbId = user->GetDBID();
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t secondAccountId = user->GetAccountID();
        const unsigned int secondDbId = user->GetDBID();
        Send(
            "cddddddSdSSS", 0x15, secondDbId, secondAccountId, serverId, 3, worldId, firstDbId, name, firstAccountId,
            user->m_sAccountName.data(), title, contents
        );
    }
}

void CCommCli::RequestDeleteMemo(User* const user, const int memoId) {
    // Current L2Server 0x005ac100.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int databaseId = user->GetDBID();
        Send("cdddddd", 0x15, databaseId, accountId, serverId, 4, worldId, memoId);
    }
}

void CCommCli::RequestUpdateMemoForm(User* const user, const int memoId) {
    // Current L2Server 0x005ac228.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int databaseId = user->GetDBID();
        Send("cdddddd", 0x15, databaseId, accountId, serverId, 5, worldId, memoId);
    }
}

void CCommCli::RequestUpdateMemo(
    User* const user, const int memoId, SharedUtilsWideChar* const title, SharedUtilsWideChar* const contents
) {
    // Current L2Server 0x005ac350.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t accountId = user->GetAccountID();
        const unsigned int dbId = user->GetDBID();
        Send("cddddddSS", 0x15, dbId, accountId, serverId, 6, worldId, memoId, title, contents);
    }
}

void CCommCli::RequestClanBBSView(User* const user, const int pledgeId) {
    // Current L2Server 0x005a9b44.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        db.RequestClanBBSView(user, pledgeId);
    }
}

void CCommCli::RequestClanBBSList(User* const user, const int page, const int value, SharedUtilsWideChar* const search) {
    // Current L2Server 0x005ac498.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        Send("cddddddS", 0x16, user->GetServerId(), user->m_data->m_nWorldId, 1, user->GetAccountID(), page, value, search);
    }
}

void CCommCli::RequestClanBBSView2(
    User* const user,
    const int value1,
    const int value2,
    SharedUtilsWideChar* const string1,
    const int value3,
    SharedUtilsWideChar* const string2,
    const int value4,
    SharedUtilsWideChar* const string3,
    const int value5,
    const int value6
) {
    // Current L2Server 0x005ac5cc.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, true)) {
        Send(
            "cddddddSdSdSdd", 0x16, user->GetServerId(), user->m_data->m_nWorldId, 3, value1, user->m_data->m_nPledgeID,
            value2, string1, value3, string2, value4, string3, value5, value6
        );
    }
}

void CCommCli::RequestClanBBSMake(
    User* const user, const int pledgeId, const char16_t* const pledgeName, const char16_t* const masterName
) {
    // Current L2Server 0x005ac750.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(user, false)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t serverId = user->GetServerId();
        const unsigned int databaseId = user->GetDBID();
        Send("cdddddSS", 0x16, databaseId, serverId, 5, worldId, pledgeId, pledgeName, masterName);
    }
}

void CCommCli::RequestClanBBSDelete(const int pledgeId, const int worldId) {
    // Current L2Server 0x005ac978.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(true)) {
        Send("cdd", 0x18, pledgeId, worldId);
    }
}

void CCommCli::RequestPledgeMasterTransfer(const unsigned char* packet) {
    // Current L2Server 0x005aca4c.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(true)) {
        std::int32_t count = 0;
        std::int32_t payloadLength = 0;
        const unsigned char* const payload = ::Disassemble(packet, "dd", &count, &payloadLength);
        Send("cddb", 0x19, g_nWorldId, count, payloadLength, payload);
    }
}

void CCommCli::RequestSetPledgeInfo(
    const int worldId, const int pledgeId, const wchar_t* const pledgeName, const wchar_t* const masterName
) {
    // Current L2Server 0x005acb5c.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(true)) {
        Send(
            "cddSS", 0x1a, worldId, pledgeId, target_utf16::FromRecoveredWindowsWide(pledgeName).c_str(),
            target_utf16::FromRecoveredWindowsWide(masterName).c_str()
        );
    }
}

void CCommCli::RequestChangePledgeMasterName(const int worldId, const int characterId, wchar_t* const name) {
    // Current L2Server 0x005acc50.
    L2_GUARD_FUNCTION();
    if (CheckCommSvr(true)) {
        Send("cddS", 0x1b, worldId, characterId, target_utf16::FromRecoveredWindowsWide(name).c_str());
    }
}

void CCommCli::RequestClanBBSInit(User* const user) {
    // Current L2Server 0x005aced4.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr && pledge->GetFunction() != nullptr &&
            pledge->GetFunction()->IsEnable(PledgeFunctionType::COMMUNITY) && user->m_data->m_nPledgeID != 0) {
            RequestClanBBSView(user, user->m_data->m_nPledgeID);
            return;
        }
    }
    SharedUtilsWideChar empty[] = {0};
    RequestClanBBSList(user, 1, 0, empty);
}

int CCommCli::CheckClanOwner(User* const user) {
    // Current L2Server 0x005ad0cc.
    L2_GUARD_FUNCTION();
    if (user->GetPledge() != nullptr) {
        if (!user->GetPledge()->GetFunction()->IsEnable(PledgeFunctionType::COMMUNITY)) {
            user->SendSystemMessage(SystemMessageId::CLAN_COMMUNITY_REQUIRES_HIGHER_CLAN_LEVEL);
            RequestClanBBSInit(user);
            return 0;
        }
        const std::uint32_t masterDbId = user->GetPledge()->GetMasterDbId();
        if (user->GetDBID() == masterDbId) {
            const int pledgeId = static_cast<int>(user->GetPledge()->GetDbId());
            if (pledgeId != 0) {
                return pledgeId;
            }
        }
    }
    user->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
    RequestClanBBSInit(user);
    return 0;
}

void CCommCli::RequestClanBBSAdmin(User* const user, int) {
    // Current L2Server 0x005ad228 ignores the supplied pledge id and
    // resolves the current owner again through CheckClanOwner.
    L2_GUARD_FUNCTION();
    const int pledgeId = CheckClanOwner(user);
    if (pledgeId != 0 && CheckCommSvr(user, true)) {
        CPledge* const pledge = user->GetPledge();
        Send(
            "cdddddSS", 0x16, user->GetServerId(), user->m_data->m_nWorldId, 4, user->GetAccountID(), pledgeId,
            pledge->GetPledgeName(), pledge->GetPledgeMasterName()
        );
    }
}

void CCommCli::RequestClanSetIntro(User* const user, SharedUtilsWideChar* const introduction) {
    // Current L2Server 0x005ad398.
    L2_GUARD_FUNCTION();
    const int pledgeId = CheckClanOwner(user);
    if (pledgeId != 0 && CheckCommSvr(user, true)) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t serverId = user->GetServerId();
        const unsigned int dbId = user->GetDBID();
        Send("cdddddS", 0x16, dbId, serverId, 6, worldId, pledgeId, introduction);
    }
}

void CCommCli::RequestClanBBSSetPer(
    User* const user, int, const int setting1, const int setting2, const int setting3, const int setting4
) {
    // Current L2Server 0x005ad4c0 ignores the supplied pledge id and repeats
    // the owner check before sending the four permission lanes.
    L2_GUARD_FUNCTION();
    const int pledgeId = CheckClanOwner(user);
    if (pledgeId != 0 && CheckCommSvr(user, true)) {
        Send(
            "cddddddddd", 0x16, user->GetServerId(), user->GetAccountID(), 7, user->m_data->m_nWorldId, pledgeId, setting1,
            setting2, setting3, setting4
        );
    }
}

bool CCommCli::Connect() {
    // Current L2Server 0x005acd38.
    L2_GUARD_FUNCTION();
    if (!Connect(m_spCommSocket0)) {
        return false;
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to comm server")
    // L2_LOG_INFO(Log,
    //     L"Connected to comm server")
    L2_LOG_INFO(
        Log,
        "First community connection setup returned successfully; setting up the second connection next. Version negotiation "
        "is still pending."
    );
    if (!Connect(m_spCommSocket1)) {
        return false;
    }
    CSPointer<CCommSocket> socket = GetCommSocket();
    socket->Send("cd", 0, 5);
    return true;
}

bool CCommCli::Init() {
    // Current L2Server 0x005acfe4.
    L2_GUARD_FUNCTION();
    bool result = true;
    if (m_nStatus == 1 || m_nStatus == 4) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nConnectTick > 60000U) {
            m_nConnectTick = runtime::GetTickCount();
            result = Connect();
            SetCliStatus(result ? 2 : 1);
        }
    }
    return result;
}
