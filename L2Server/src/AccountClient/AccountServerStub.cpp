#include "AccountServerStub.h"
#include "Shared/src/TemplateHelper.h"

#include "L2Server/src/CConfig.h"
#include "L2Server/src/User.h"
#include "L2Server/src/UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <string>

namespace {

constexpr const char16_t* kSystemMessageContext = u"SYS";

std::uint32_t AccountMapKey(const std::int64_t accountID) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(accountID));
}

CUserSocket* FindAccountSocket(const std::int64_t accountID) {
    CUserSocket* socket;
    {
        L2_GUARD(accountMapSect);
        const auto found = accountMap.find(AccountMapKey(accountID));
        socket = found != accountMap.end() ? found->second : nullptr;
    }
    return socket;
}

template <typename Packet>
void DecodePacket(Packet& decoded, const std::uint8_t* packet) {
    network::Stream stream(const_cast<unsigned char*>(packet), 0x4000);
    static_cast<void>(decoded.Decode(&stream));
}

template <typename... Arguments>
void SendAccountSystemMessage(CUserSocket* socket, const std::u16string_view targetFormat, const Arguments&... arguments) {
    std::array<char16_t, 1024> message{};
    FormatUtf16To(message.data(), message.size(), targetFormat, arguments...);
    socket->SendSystemMessage(kSystemMessageContext, message.data());
}

runtime::NativeSocket CreateAccountSocket(
    const in_addr serverAddress, const std::uint16_t serverPort, const char* serverName
) {
    // Current L2Server 0x0041bda4 binds an all-zero ephemeral local endpoint
    // before attempting the remote connect.
    const runtime::NativeSocket socket = runtime::OpenTcpSocket();
    in_addr localAddress{};
    if (!runtime::BindTcpSocket(socket, localAddress, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Can't bind socket to %s",
        //     serverName)
        // L2_LOG_ERROR(Log,
        //     "Can't bind socket to %s",
        //     serverName)
        const int socketError = portable::GetNativeSocketLastError();
        L2_LOG_ERROR(
            Log, "Cannot %s socket for %s (socket error %d); account connection setup stops.",
            socket == runtime::kInvalidNativeSocket ? "create" : "bind", serverName, socketError
        );
        runtime::CloseTcpSocket(socket);
        return 0;
    }
    if (!runtime::ConnectTcpSocket(socket, serverAddress, serverPort)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Can't connect to %s",
        //     serverName)
        // L2_LOG_ERROR(Log,
        //     "Can't connect to %s",
        //     serverName)
        const int socketError = portable::GetNativeSocketLastError();
        const std::uint32_t address = ntohl(serverAddress.s_addr);
        L2_LOG_ERROR(
            Log, "Cannot connect to %s at %u.%u.%u.%u:%u (socket error %d); closing the socket.", serverName,
            static_cast<unsigned>((address >> 24) & 255U), static_cast<unsigned>((address >> 16) & 255U),
            static_cast<unsigned>((address >> 8) & 255U), static_cast<unsigned>(address & 255U),
            static_cast<unsigned>(serverPort), socketError
        );
        runtime::CloseTcpSocket(socket);
        return 0;
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     "Connected to %s",
    //     serverName)
    // L2_LOG_INFO(Log,
    //     "Connected to %s",
    //     serverName)
    L2_LOG_INFO(Log, "TCP connection to %s established; account protocol negotiation is still pending.", serverName);
    return socket;
}

} // namespace

AccountServerStub::AccountServerStub() :
    m_serverAddress{},
    m_serverPort(0),
    m_accountClientSocket(nullptr, ObjectRefType::FunctionChain),
    m_sendable(0),
    m_hConnectThread(reinterpret_cast<void*>(~std::uintptr_t{0})),
    m_connectThreadId(0),
    m_connectSignal("AccountConnectSignal") {}

AccountServerStub& AccountServerStub::Inst() {
    static AccountServerStub instance;
    return instance;
}

void AccountServerStub::Init(const in_addr serverAddress, const std::uint16_t serverPort) {
    L2_GUARD_FUNCTION();
    m_serverPort = serverPort;
    m_serverAddress = serverAddress;
}

void AccountServerStub::OnAccountSocketClosed(void* context, AccountClientSocket*) {
    auto* const stub = static_cast<AccountServerStub*>(context);
    runtime::raw_atomic::StoreI32(&stub->m_sendable, 0);
    stub->m_accountClientSocket.Release();
}

bool AccountServerStub::Connect(const int timeoutSeconds) {
    // Current L2Server 0x0041f01c.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32(&m_sendable) != 0) {
        return true;
    }

    const runtime::NativeSocket nativeSocket = CreateAccountSocket(m_serverAddress, m_serverPort, "AccountServer");
    if (nativeSocket == 0) {
        return false;
    }

    auto* const socket = new AccountClientSocket(
        nativeSocket, g_AccountServerPacketTable.GetData(), 0x10,
        AccountClientCloseFunction(&AccountServerStub::OnAccountSocketClosed, this)
    );
    m_accountClientSocket.SetObject(socket, ObjectRefType::FunctionChain);
    if (!m_accountClientSocket) {
        return false;
    }

    const bool bound = portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, const std::size_t length) {
            return portable::FeedSocketRead(static_cast<AccountClientSocket*>(owner), data, length);
        },
        [](void* owner, const int error) { portable::FeedSocketTerminal(static_cast<AccountClientSocket*>(owner), error); }
    );
    if (!bound) {
        runtime::CloseTcpSocket(nativeSocket);
    }
    m_accountClientSocket->Initialize();
    Send_ACS_CONNECT(g_nWorldId);
    m_connectSignal.WaitFinish(static_cast<unsigned int>(timeoutSeconds));
    return runtime::raw_atomic::LoadI32(&m_sendable) != 0;
}

void AccountServerStub::ConnectCallback(const bool connected) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::StoreI32(&m_sendable, connected ? 1 : 0);
    m_connectSignal.NotifyFinish();
}

void AccountServerStub::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    if (runtime::raw_atomic::LoadI32(&m_sendable) == 0 || !m_accountClientSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[Send1] AccountSocket Invalid")
        // L2_LOG_ERROR(Log,
        //     "[Send1] AccountSocket Invalid")
        L2_LOG_WARN(Log, "Account request was not sent: the client is not sendable or its socket is unavailable.");
    } else {
        m_accountClientSocket->SendV(format, arguments);
    }
    va_end(arguments);
}

void AccountServerStub::Send(const AccountProtocol::ACCOUNT_CS_PROTOCOL protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (!m_accountClientSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[Send2] AccountSocket Invalid")
        // L2_LOG_ERROR(Log,
        //     "[Send2] AccountSocket Invalid")
        L2_LOG_WARN(
            Log, "Account protocol request 0x%02x was not sent: its socket is unavailable.", static_cast<unsigned>(protocol)
        );
        return;
    }
    m_accountClientSocket->SendPacket(protocol, packet);
}

void AccountServerStub::ACS_TEST_DB_SP() {
    L2_GUARD_FUNCTION();
    Send("c", 1);
}

void AccountServerStub::ASC_TEST_MORPHEUS() {
    L2_GUARD_FUNCTION();
    protocol::ACS_TEST_MORPHEUS packet;
    packet.packetId = 0;
    constexpr char16_t greeting[] = u"Hello, accountD";
    std::copy(std::begin(greeting), std::end(greeting), std::begin(packet.greetings));
    protocol::Foo value;
    value.bar = 0x79;
    packet.garbageList.push_back(value);
    Send(AccountProtocol::ACS_TEST_MORPHEUS, packet);
}

void AccountServerStub::Send_ACS_CONNECT(const int connectedServerNumber) {
    L2_GUARD_FUNCTION();
    protocol::ACS_CONNECT packet;
    packet.connectedServerNumber = connectedServerNumber;
    Send(AccountProtocol::ACS_CONNECT, packet);
}

void AccountServerStub::Send_ACS_FRIEND_DELETE(const std::int64_t requesterAccountID, const std::int64_t friendAccountID) {
    L2_GUARD_FUNCTION();
    protocol::ACS_FRIEND_DELETE packet;
    packet.requesterAccountID = requesterAccountID;
    packet.friendAccountID = friendAccountID;
    Send(AccountProtocol::ACS_FRIEND_DELETE, packet);
}

void AccountServerStub::Send_ACS_FRIEND(const std::int64_t accountID) {
    L2_GUARD_FUNCTION();
    protocol::ACS_FRIEND packet;
    packet.accountID = accountID;
    Send(AccountProtocol::ACS_FRIEND, packet);
}

void AccountServerStub::Send_ACS_POINT_UPDATE(
    const std::int64_t accountID, const char pointType, const std::int64_t pointAmount
) {
    L2_GUARD_FUNCTION();
    protocol::ACS_POINT_UPDATE packet;
    packet.accountID = accountID;
    packet.pointType = pointType;
    packet.pointAmount = pointAmount;
    Send(AccountProtocol::ACS_POINT_UPDATE, packet);
}

void AccountServerStub::Send_ACS_POINT(const std::int64_t accountID) {
    L2_GUARD_FUNCTION();
    protocol::ACS_POINT packet;
    packet.accountID = accountID;
    Send(AccountProtocol::ACS_POINT, packet);
}

void AccountServerStub::Send_ACS_FRIEND_CREATE_ANSWER(
    const std::int64_t requesterAccountID, const std::int64_t answerAccountID, const std::int16_t resultCode
) {
    L2_GUARD_FUNCTION();
    protocol::ACS_FRIEND_CREATE_ANSWER packet;
    packet.requesterAccountID = requesterAccountID;
    packet.answerAccountID = answerAccountID;
    packet.resultCode = resultCode;
    Send(AccountProtocol::ACS_FRIEND_CREATE_ANSWER, packet);
}

void AccountServerStub::Send_ACS_FRIEND_ONLINE(const std::int64_t accountID, const std::int64_t friendAccountID) {
    L2_GUARD_FUNCTION();
    protocol::ACS_FRIEND_ONLINE packet;
    packet.accountID = accountID;
    packet.friendaccountID = friendAccountID;
    Send(AccountProtocol::ACS_FRIEND_ONLINE, packet);
}

void AccountServerStub::Send_ACS_FRIEND_OFFLINE(const std::int64_t accountID) {
    L2_GUARD_FUNCTION();
    protocol::ACS_FRIEND_OFFLINE packet;
    packet.accountID = accountID;
    Send(AccountProtocol::ACS_FRIEND_OFFLINE, packet);
}

void AccountServerStub::Send_ACS_FRIEND_CREATE_REQUEST(const std::int64_t requesterAccountID, std::u16string friendName) {
    L2_GUARD_FUNCTION();
    protocol::ACS_FRIEND_CREATE_REQUEST packet;
    packet.requesterAccountID = requesterAccountID;
    packet.wfriendName = friendName;
    Send(AccountProtocol::ACS_FRIEND_CREATE_REQUEST, packet);
}

void AccountServerStub::Send_ACS_CHAT(
    const std::int64_t senderAccountID, const std::int64_t receiverAccountID, std::u16string message
) {
    L2_GUARD_FUNCTION();
    protocol::ACS_CHAT packet;
    packet.senderAccountID = senderAccountID;
    packet.receiverAccountID = receiverAccountID;
    packet.param = message;
    Send(AccountProtocol::ACS_CHAT, packet);
}

void AccountServerStub::Send_ACS_REGISTER_CHAT_NAME(const std::int64_t accountID, std::u16string chatName) {
    L2_GUARD_FUNCTION();
    protocol::ACS_REGISTER_CHAT_NAME packet;
    packet.accountID = accountID;
    packet.chatName = chatName;
    Send(AccountProtocol::ACS_REGISTER_CHAT_NAME, packet);
}

bool AccountServerStub::DummyPacket(CSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in RelayClient")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in RelayClient")
    L2_LOG_WARN(Log, "Account-server packet reached the fallback handler; it returns without processing the packet.");
    return false;
}

bool AccountServerStub::ASC_TEST_DB_SP_RETURNED(CSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    char result = 0;
    static_cast<void>(Disassemble(packet, "c", &result));
    // Log.Add(
    //     LOG_ERROR,
    //     "Receive RELAYSC_TEST packet[%d]",
    //     static_cast<int>(result))
    // L2_LOG_ERROR(Log,
    //     "Receive RELAYSC_TEST packet[%d]",
    //     static_cast<int>(result))
    L2_LOG_INFO(Log, "Account database test reply received with result %d.", static_cast<int>(result));
    return false;
}

bool AccountServerStub::ASC_TEST_MORPHEUS_RETURN(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_TEST_MORPHEUS_RETURN packet;
    DecodePacket(packet, packetBytes);
    const std::string greeting = Utf16ToUtf8(packet.greetings);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s], returned: %d, %s",
    //     L"bool __cdecl AccountServerStub::ASC_TEST_MORPHEUS_RETURN(class CSocket *,const unsigned char *)",
    //     static_cast<int>(packet.packetId),
    //     greeting.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[%s], returned: %d, %s",
    //     L"bool __cdecl AccountServerStub::ASC_TEST_MORPHEUS_RETURN(class CSocket *,const unsigned char *)",
    //     static_cast<int>(packet.packetId),
    //     greeting.c_str())
    L2_LOG_INFO(
        Log, "Account Morpheus test reply received: packet ID %d, greeting '%s'.", static_cast<int>(packet.packetId),
        greeting.c_str()
    );
    return false;
}

bool AccountServerStub::ASC_CONNECT_RETURN(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_CONNECT_RETURN packet;
    DecodePacket(packet, packetBytes);
    Inst().ConnectCallback(packet.connected == 1);
    return false;
}

bool AccountServerStub::ASC_ACCOUNT_CONNECTION_STATUS_UPDATE(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_ACCOUNT_CONNECTION_STATUS_UPDATE packet;
    DecodePacket(packet, packetBytes);
    if (packet.resultCode == 0) {
        Inst().Send_ACS_FRIEND_OFFLINE(packet.accountID);
    } else if (packet.resultCode == 1) {
        Inst().Send_ACS_FRIEND(packet.accountID);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"connection_status_update: %I64d, %d",
        //     static_cast<long long>(packet.accountID),
        //     static_cast<int>(packet.resultCode))
        // L2_LOG_ERROR(Log,
        //     L"connection_status_update: %I64d, %d",
        //     static_cast<long long>(packet.accountID),
        //     static_cast<int>(packet.resultCode))
        L2_LOG_ERROR(
            Log,
            "Account %lld connection-status update has unsupported result %d; expected 0 or 1. No friend-status request is "
            "sent.",
            static_cast<long long>(packet.accountID), static_cast<int>(packet.resultCode)
        );
    }
    return false;
}

bool AccountServerStub::ASC_FRIEND_CREATE(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_FRIEND_CREATE packet;
    DecodePacket(packet, packetBytes);
    const std::string name = Utf16ToUtf8(packet.name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"friend_create: %I64d, %s, %d",
    //     static_cast<long long>(packet.requesterAccountID),
    //     name.c_str(),
    //     static_cast<int>(packet.resultCode))
    // L2_LOG_ERROR(Log,
    //     L"friend_create: %I64d, %s, %d",
    //     static_cast<long long>(packet.requesterAccountID),
    //     name.c_str(),
    //     static_cast<int>(packet.resultCode))
    L2_LOG_INFO(
        Log,
        "Account friend-create reply received for requester %lld and name '%s' (result %d); this handler only records the "
        "reply.",
        static_cast<long long>(packet.requesterAccountID), name.c_str(), static_cast<int>(packet.resultCode)
    );
    return false;
}

bool AccountServerStub::ASC_FRIEND_DELETE(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_FRIEND_DELETE packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* const socket = FindAccountSocket(packet.requesterAccountID);
    if (socket != nullptr) {
        const std::wstring name = Utf16ToWide(packet.name);
        SendAccountSystemMessage(
            socket, u"friend_delete : %I64d, %I64d, %s, %d", static_cast<long long>(packet.requesterAccountID),
            static_cast<long long>(packet.friendAccountID), name.c_str(), static_cast<int>(packet.resultCode)
        );
    }
    return false;
}

bool AccountServerStub::ASC_FRIEND(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_FRIEND packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* const socket = FindAccountSocket(packet.accountID);
    if (socket == nullptr) {
        return false;
    }
    if (packet.friendInfoList.empty()) {
        socket->SendSystemMessage(kSystemMessageContext, u"no friends");
        return false;
    }
    for (const protocol::FriendInfo& friendInfo : packet.friendInfoList) {
        const std::wstring name = Utf16ToWide(friendInfo.name);
        SendAccountSystemMessage(
            socket, u"friend_list : %I64d, %s, %d, %d", static_cast<long long>(friendInfo.accountID), name.c_str(),
            static_cast<int>(friendInfo.friendstatus), static_cast<int>(friendInfo.OnOffstatus)
        );
        if (friendInfo.friendstatus == 1) {
            Inst().Send_ACS_FRIEND_ONLINE(friendInfo.accountID, packet.accountID);
        }
    }
    return false;
}

bool AccountServerStub::ASC_POINT_UPDATE(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_POINT_UPDATE packet;
    DecodePacket(packet, packetBytes);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s], returned: %I64d, %d",
    //     L"bool __cdecl AccountServerStub::ASC_POINT_UPDATE(class CSocket *,const unsigned char *)",
    //     static_cast<long long>(packet.accountID),
    //     static_cast<int>(packet.resultCode))
    // L2_LOG_ERROR(Log,
    //     L"[%s], returned: %I64d, %d",
    //     L"bool __cdecl AccountServerStub::ASC_POINT_UPDATE(class CSocket *,const unsigned char *)",
    //     static_cast<long long>(packet.accountID),
    //     static_cast<int>(packet.resultCode))
    L2_LOG_INFO(
        Log, "Account point-update reply received for account %lld (result %d); this handler only records the reply.",
        static_cast<long long>(packet.accountID), static_cast<int>(packet.resultCode)
    );
    return false;
}

bool AccountServerStub::ASC_POINT(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_POINT packet;
    DecodePacket(packet, packetBytes);
    for (const protocol::PointInfo& point : packet.pointInfoList) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s], returned: %I64d, %d, %d",
        //     L"bool __cdecl AccountServerStub::ASC_POINT(class CSocket *,const unsigned char *)",
        //     static_cast<long long>(packet.accountID),
        //     static_cast<int>(point.pointType),
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(
        //             static_cast<std::uint64_t>(point.pointAmount))))
        // L2_LOG_ERROR(Log,
        //     L"[%s], returned: %I64d, %d, %d",
        //     L"bool __cdecl AccountServerStub::ASC_POINT(class CSocket *,const unsigned char *)",
        //     static_cast<long long>(packet.accountID),
        //     static_cast<int>(point.pointType),
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(
        //             static_cast<std::uint64_t>(point.pointAmount))))
        L2_LOG_INFO(
            Log, "Account %lld point reply: type %d, amount %lld; this handler only records the value.",
            static_cast<long long>(packet.accountID), static_cast<int>(point.pointType),
            static_cast<long long>(point.pointAmount)
        );
    }
    return false;
}

bool AccountServerStub::ASC_REGISTER_CHAT_NAME(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_REGISTER_CHAT_NAME packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* const socket = FindAccountSocket(packet.accountID);
    if (socket == nullptr) {
        return false;
    }
    if (packet.resultCode == -1 || packet.resultCode == 0) {
        socket->SendSystemMessage(0);
    } else if (packet.resultCode == 1) {
        if (socket->GetUser() == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] pUser is NULL",
            //     L"bool __cdecl AccountServerStub::ASC_REGISTER_CHAT_NAME(class CSocket *,const unsigned char *)")
            // L2_LOG_ERROR(Log,
            //     L"[%s] pUser is NULL",
            //     L"bool __cdecl AccountServerStub::ASC_REGISTER_CHAT_NAME(class CSocket *,const unsigned char *)")
            L2_LOG_WARN(
                Log,
                "Chat-name registration reply for account %lld has no attached player; the account socket still receives "
                "the result message.",
                static_cast<long long>(packet.accountID)
            );
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s], returned: %d",
        //     L"bool __cdecl AccountServerStub::ASC_REGISTER_CHAT_NAME(class CSocket *,const unsigned char *)",
        //     static_cast<int>(packet.resultCode))
        // L2_LOG_ERROR(Log,
        //     L"[%s], returned: %d",
        //     L"bool __cdecl AccountServerStub::ASC_REGISTER_CHAT_NAME(class CSocket *,const unsigned char *)",
        //     static_cast<int>(packet.resultCode))
        L2_LOG_ERROR(
            Log,
            "Chat-name registration reply for account %lld has unsupported result %d; the account socket still receives the "
            "result message.",
            static_cast<long long>(packet.accountID), static_cast<int>(packet.resultCode)
        );
    }
    const std::wstring chatName = Utf16ToWide(packet.chatName);
    SendAccountSystemMessage(
        socket, u"chat_name : %I64d, %s, %d", static_cast<long long>(packet.accountID), chatName.c_str(),
        static_cast<int>(packet.resultCode)
    );
    return false;
}

bool AccountServerStub::ASC_FRIEND_CREATE_REQUEST(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_FRIEND_CREATE_REQUEST packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* socket = nullptr;
    if (packet.status == -1) {
        socket = FindAccountSocket(packet.answerAccountID);
    } else if (packet.status == 0) {
        socket = FindAccountSocket(packet.requesterAccountID);
    }
    if (socket != nullptr) {
        const std::wstring name = Utf16ToWide(packet.Name);
        SendAccountSystemMessage(
            socket, u"friend_add_request : %I64d, %s, %d", static_cast<long long>(packet.answerAccountID), name.c_str(),
            static_cast<int>(packet.status)
        );
    }
    return false;
}

bool AccountServerStub::ASC_FRIEND_STATUS_UPDATE(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_FRIEND_STATUS_UPDATE packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* const socket = FindAccountSocket(packet.accountID);
    if (socket != nullptr) {
        const std::wstring name = Utf16ToWide(packet.Name);
        SendAccountSystemMessage(
            socket, u"friend_status_update : %I64d, %I64d, %s, %d", static_cast<long long>(packet.accountID),
            static_cast<long long>(packet.friendaccountID), name.c_str(), static_cast<int>(packet.onoffline)
        );
    }
    return false;
}

bool AccountServerStub::ASC_FRIEND_CREATE_ANSWER(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_FRIEND_CREATE_ANSWER packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* const socket = FindAccountSocket(packet.accountID);
    if (socket != nullptr) {
        const std::wstring name = Utf16ToWide(packet.Name);
        SendAccountSystemMessage(
            socket, u"friend_create_answer : %d, %I64d, %I64d, %s, %d", static_cast<int>(packet.resultCode),
            static_cast<long long>(packet.accountID), static_cast<long long>(packet.friendaccountID), name.c_str(),
            static_cast<int>(packet.onoffline)
        );
    }
    return false;
}

bool AccountServerStub::ASC_CHAT(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_CHAT packet;
    DecodePacket(packet, packetBytes);
    CUserSocket* const socket = FindAccountSocket(packet.receiverAccountID);
    if (socket != nullptr) {
        const std::wstring message = Utf16ToWide(packet.param);
        SendAccountSystemMessage(
            socket, u"friend_chat : %I64d, %I64d, %s", static_cast<long long>(packet.senderAccountID),
            static_cast<long long>(packet.receiverAccountID), message.c_str()
        );
    }
    return false;
}

bool AccountServerStub::ASC_LOAD_CLIENT_INI(CSocket*, const std::uint8_t* packetBytes) {
    L2_GUARD_FUNCTION();
    protocol::ASC_LOAD_CLIENT_INI packet;
    DecodePacket(packet, packetBytes);
    {
        L2_GUARD(accountMapSect);
        static_cast<void>(accountMap.lower_bound(AccountMapKey(packet.accountID)));
    }
    return false;
}

AccountServerPacketMap g_AccountServerPacketTable{AccountServerStub::DummyPacket, u""};
L2_BIND_PACKET(IsRegistered_ASC_DUMMY, g_AccountServerPacketTable, 0, AccountServerStub::DummyPacket);
L2_BIND_PACKET(
    IsRegistered_ASC_TEST_DB_SP_RETURN, g_AccountServerPacketTable, 1, AccountServerStub::ASC_TEST_DB_SP_RETURNED
);
L2_BIND_PACKET(
    IsRegistered_ASC_TEST_MORPHEUS_RETURN, g_AccountServerPacketTable, 2, AccountServerStub::ASC_TEST_MORPHEUS_RETURN
);
L2_BIND_PACKET(IsRegistered_ASC_CONNECT_RETURN, g_AccountServerPacketTable, 3, AccountServerStub::ASC_CONNECT_RETURN);
L2_BIND_PACKET(
    IsRegistered_ASC_ACCOUNT_CONNECTION_STATUS_UPDATE,
    g_AccountServerPacketTable,
    4,
    AccountServerStub::ASC_ACCOUNT_CONNECTION_STATUS_UPDATE
);
L2_BIND_PACKET(IsRegistered_ASC_FRIEND_CREATE, g_AccountServerPacketTable, 5, AccountServerStub::ASC_FRIEND_CREATE);
L2_BIND_PACKET(IsRegistered_ASC_FRIEND_DELETE, g_AccountServerPacketTable, 6, AccountServerStub::ASC_FRIEND_DELETE);
L2_BIND_PACKET(IsRegistered_ASC_FRIEND, g_AccountServerPacketTable, 7, AccountServerStub::ASC_FRIEND);
L2_BIND_PACKET(IsRegistered_ASC_POINT_UPDATE, g_AccountServerPacketTable, 8, AccountServerStub::ASC_POINT_UPDATE);
L2_BIND_PACKET(IsRegistered_ASC_POINT, g_AccountServerPacketTable, 9, AccountServerStub::ASC_POINT);
L2_BIND_PACKET(
    IsRegistered_ASC_REGISTER_CHAT_NAME, g_AccountServerPacketTable, 10, AccountServerStub::ASC_REGISTER_CHAT_NAME
);
L2_BIND_PACKET(
    IsRegistered_ASC_FRIEND_CREATE_REQUEST, g_AccountServerPacketTable, 11, AccountServerStub::ASC_FRIEND_CREATE_REQUEST
);
L2_BIND_PACKET(
    IsRegistered_ASC_FRIEND_STATUS_UPDATE, g_AccountServerPacketTable, 12, AccountServerStub::ASC_FRIEND_STATUS_UPDATE
);
L2_BIND_PACKET(
    IsRegistered_ASC_FRIEND_CREATE_ANSWER, g_AccountServerPacketTable, 13, AccountServerStub::ASC_FRIEND_CREATE_ANSWER
);
L2_BIND_PACKET(IsRegistered_ASC_CHAT, g_AccountServerPacketTable, 14, AccountServerStub::ASC_CHAT);
L2_BIND_PACKET(IsRegistered_ASC_LOAD_CLIENT_INI, g_AccountServerPacketTable, 15, AccountServerStub::ASC_LOAD_CLIENT_INI);
