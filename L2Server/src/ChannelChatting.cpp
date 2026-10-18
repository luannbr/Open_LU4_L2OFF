#include "ChannelChatting.h"

#include "ChannelChattingSocket.h"
#include "User.h"

#include "Shared/src/Log.h"

#include <cwchar>
#include <limits>
#include <string>

ChannelChattingClient* ChannelChattingClient::m_pChannelChattingClient = nullptr;

ChannelChattingClient::ChannelChattingClient() :
    enabled(false),
    sendable(false),
    supressed(false),
    m_nConnectThread(0),
    m_hConnectThread(nullptr),
    m_pChannelChattingSocket(nullptr) {
    L2_GUARD_FUNCTION();
}

ChannelChattingClient& ChannelChattingClient::Inst() {
    L2_GUARD_FUNCTION();
    ChannelChattingClient* client = m_pChannelChattingClient;
    if (client == nullptr) {
        client = new ChannelChattingClient;
    }
    m_pChannelChattingClient = client;
    return *client;
}

bool ChannelChattingClient::SendKeepAlive() {
    L2_GUARD_FUNCTION();
    if (m_pChannelChattingSocket == nullptr ||
        m_pChannelChattingSocket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]ChannelChatting Socket is not valid",
        //     L"ChannelChatting.cpp",
        //     0xb4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]ChannelChatting Socket is not valid",
        //     L"ChannelChatting.cpp",
        //     0xb4)
        L2_LOG_WARN(Log, "Cannot send channel-chat keepalive: the connection socket is unavailable.");
        return false;
    }

    m_pChannelChattingSocket->Send("h", 2);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][ChannelChatting] Send KeepAlive Packet ",
    //     L"ChannelChatting.cpp",
    //     0xb9)
    // Noise: Routine keepalive transmission is not an error and does not confirm a peer reply.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][ChannelChatting] Send KeepAlive Packet ",
    //     L"ChannelChatting.cpp",
    //     0xb9)
    return true;
}

bool ChannelChattingClient::SendLogOut(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (m_pChannelChattingSocket == nullptr ||
        m_pChannelChattingSocket->m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]ChannelChatting Socket is not valid",
        //     L"ChannelChatting.cpp",
        //     199)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]ChannelChatting Socket is not valid",
        //     L"ChannelChatting.cpp",
        //     199)
        L2_LOG_WARN(
            Log, "Cannot notify channel chat of character %u logout: the connection socket is unavailable.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    const char16_t* const lengthName = user->GetName2(nullptr);
    const std::u16string serializedLengthName = std::u16string(lengthName);
    const int nameLength = static_cast<int>(serializedLengthName.size());
    const auto packetNameLength = static_cast<std::int16_t>(nameLength);
    const std::int64_t byteLength = static_cast<std::int64_t>(nameLength) * 2;
    const char16_t* const firstName = user->GetName2(nullptr);
    const std::u16string firstSerializedName = std::u16string(firstName);
    m_pChannelChattingSocket->Send("hhb", 5, packetNameLength, byteLength, firstSerializedName.c_str());
    const char16_t* const secondName = user->GetName2(nullptr);
    const std::u16string secondSerializedName = std::u16string(secondName);
    m_pChannelChattingSocket->Send("hhbd", 6, packetNameLength, byteLength, secondSerializedName.c_str(), 2);
    return false;
}
