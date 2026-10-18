#pragma once

#include "AccountClientSocket.h"

#include "L2Server/src/AccountJobFinishSignal.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/proto/AccountPacket.h"

#include <cstddef>
#include <cstdint>

class AccountServerStub {
public:
    AccountServerStub();
    ~AccountServerStub() = default;

    AccountServerStub(const AccountServerStub&) = delete;
    AccountServerStub& operator=(const AccountServerStub&) = delete;

    static AccountServerStub& Inst();

    void Init(in_addr serverAddress, std::uint16_t serverPort);
    bool Connect(int timeoutSeconds);
    void ConnectCallback(bool connected);

    void Send(const char* format, ...);
    void Send(AccountProtocol::ACCOUNT_CS_PROTOCOL protocol, network::IPacket& packet);

    void ACS_TEST_DB_SP();
    void ASC_TEST_MORPHEUS();
    void Send_ACS_CONNECT(int connectedServerNumber);
    void Send_ACS_FRIEND_DELETE(std::int64_t requesterAccountID, std::int64_t friendAccountID);
    void Send_ACS_FRIEND(std::int64_t accountID);
    void Send_ACS_POINT_UPDATE(std::int64_t accountID, char pointType, std::int64_t pointAmount);
    void Send_ACS_POINT(std::int64_t accountID);
    void Send_ACS_FRIEND_CREATE_ANSWER(
        std::int64_t requesterAccountID, std::int64_t answerAccountID, std::int16_t resultCode
    );
    void Send_ACS_FRIEND_ONLINE(std::int64_t accountID, std::int64_t friendAccountID);
    void Send_ACS_FRIEND_OFFLINE(std::int64_t accountID);
    void Send_ACS_FRIEND_CREATE_REQUEST(std::int64_t requesterAccountID, std::u16string friendName);
    void Send_ACS_CHAT(std::int64_t senderAccountID, std::int64_t receiverAccountID, std::u16string message);
    void Send_ACS_REGISTER_CHAT_NAME(std::int64_t accountID, std::u16string chatName);

    static bool DummyPacket(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_TEST_DB_SP_RETURNED(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_TEST_MORPHEUS_RETURN(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_CONNECT_RETURN(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_ACCOUNT_CONNECTION_STATUS_UPDATE(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_FRIEND_CREATE(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_FRIEND_DELETE(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_FRIEND(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_POINT_UPDATE(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_POINT(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_REGISTER_CHAT_NAME(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_FRIEND_CREATE_REQUEST(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_FRIEND_STATUS_UPDATE(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_FRIEND_CREATE_ANSWER(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_CHAT(CSocket* socket, const std::uint8_t* packet);
    static bool ASC_LOAD_CLIENT_INI(CSocket* socket, const std::uint8_t* packet);

private:
    static void OnAccountSocketClosed(void* context, AccountClientSocket* socket);

    in_addr m_serverAddress; // +0x00
    std::uint16_t m_serverPort; // +0x04
    CSPointer<AccountClientSocket> m_accountClientSocket; // +0x08
    volatile std::int32_t m_sendable; // +0x18
    [[maybe_unused]] void* m_hConnectThread; // +0x20
    [[maybe_unused]] std::uint32_t m_connectThreadId; // +0x28
    SingleJobFinishSignal m_connectSignal; // +0x30
};
