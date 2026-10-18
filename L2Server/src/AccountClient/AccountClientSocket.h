#pragma once

#include "Shared/src/Socket.h"
#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace AccountProtocol {

enum ACCOUNT_CS_PROTOCOL : std::int32_t {
    ACS_DUMMY = 0,
    ACS_TEST_DB_SP = 1,
    ACS_TEST_MORPHEUS = 2,
    ACS_CONNECT = 3,
    ACS_ACCOUNT_CONNECTION_STATUS_UPDATE = 4,
    ACS_FRIEND_CREATE_REQUEST = 5,
    ACS_FRIEND_DELETE = 6,
    ACS_FRIEND = 7,
    ACS_POINT_UPDATE = 8,
    ACS_POINT = 9,
    ACS_REGISTER_CHAT_NAME = 10,
    ACS_FRIEND_CREATE_ANSWER = 11,
    ACS_FRIEND_ONLINE = 12,
    ACS_FRIEND_OFFLINE = 13,
    ACS_CHAT = 14,
    ACS_LOAD_CLIENT_INI = 15,
    ACS_SAVE_CLIENT_INI = 16,
    ACS_MAX = 17,
};

} // namespace AccountProtocol


class AccountClientSocket;

// Portable typed creation of the current 0x20-byte
// std::tr1::function<void(AccountClientSocket*)>. The shipping inline
// callable occupies the first 0x10 bytes and the active-manager pointer is
// at +0x18. The middle qword is not initialized: the current
// clone leaves that unused inline-storage lane untouched.
class AccountClientCloseFunction {
public:
    using Invoker = void (*)(void*, AccountClientSocket*);

    AccountClientCloseFunction() noexcept;
    AccountClientCloseFunction(Invoker invoker, void* context) noexcept;
    AccountClientCloseFunction(const AccountClientCloseFunction& other) noexcept;
    AccountClientCloseFunction(AccountClientCloseFunction&& other) noexcept;
    ~AccountClientCloseFunction() = default;

    AccountClientCloseFunction& operator=(const AccountClientCloseFunction&) = delete;
    AccountClientCloseFunction& operator=(AccountClientCloseFunction&&) = delete;

    explicit operator bool() const noexcept;
    void operator()(AccountClientSocket* socket) const;

private:
    Invoker m_invoker; // +0x00: target inline callable vptr lane
    void* m_context; // +0x08: captured AccountServerStub*
    [[maybe_unused]] void* m_reserved; // +0x10: unused current inline-storage lane
    const AccountClientCloseFunction* m_manager; // +0x18
};


using AccountServerPacketMap = FixedSizeCallbackMap<16, PacketFunc>;

extern AccountServerPacketMap g_AccountServerPacketTable;

class AccountClientSocket final : public CSocket {
public:
    AccountClientSocket(
        std::uint64_t socket, PacketFunc* packetTable, unsigned char maxProtocolNo, AccountClientCloseFunction closeFunction
    );
    ~AccountClientSocket() override = default;

    L2_DECLARE_OBJECT_TYPE(AccountClientSocket)

    void Send(const char* format, ...) override;
    void SendPacket(AccountProtocol::ACCOUNT_CS_PROTOCOL protocol, network::IPacket& packet);

protected:
    void OnClose() override;
    void OnRead() override;
    void OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const override;

private:
    unsigned char m_cMaxProtocolNo; // +0x108
    [[maybe_unused]] std::array<std::uint8_t, 7> m_protocolPadding;
    AccountClientCloseFunction m_onClose; // +0x110
};
