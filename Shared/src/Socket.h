#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/IOC.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <string>


#if defined(L2_BUILD_L2SERVER)
enum S_PROTOCOL : std::int32_t;
enum S_EX_PROTOCOL : std::int32_t;
#endif

class SocketSecureKey {
public:
    using KeyType = char[16];
    enum : int {
        CircularMask = 15,
    };

    SocketSecureKey() = default;

    void SetKey(const KeyType& key);
    void IncreaseKey(int amount);
    KeyType& GetKey();
    bool IsEnabled();
    void SetEnabled(bool enabled);

private:
    bool m_bIsEnabled = false;
    KeyType m_Key{};
};

// Shared/socket.h CSocket. Every product keeps the complete 0x108-byte
// layout and the SetAddress/GetAddress/OnSending/CastUserSocket slots.
// L2Server additionally creates the two typed client-packet Send slots
// declared below.
class CSocket : public CIOSocket {
public:
    explicit CSocket(std::uint64_t socket = 0, int serializerLimit = 0x1000);
    ~CSocket() override;

    L2_DECLARE_OBJECT_TYPE(CSocket)

    static void Encrypt(std::uint8_t* buffer, SocketSecureKey& key, int length);
    static void EncryptEx(std::uint8_t* destination, std::uint8_t* source, SocketSecureKey& key, int length);

    std::u16string GetIPAddressString();
    std::uint32_t GetIPAddressUINT();

    virtual void SetAddress(::in_addr address);
    virtual ::in_addr GetAddress() const;

#if defined(L2_BUILD_L2SERVER)
    // L2Server's Shared/Socket.cpp specialization appends these two typed
    // client-packet slots after SetAddress/GetAddress. L2NPC does not
    // create them, so keep the source-level specialization explicit.
    // Keep the inherited Send overloads visible beside the typed slots.
    using CIOSocket::Send;
    virtual void Send(S_PROTOCOL protocol, S_EX_PROTOCOL extendedProtocol, network::IPacket& packet);
    virtual void Send(S_PROTOCOL protocol, network::IPacket& packet);
#endif

    void IssueRead();
    void __async_AsyncSend(void* argument);
    int AsyncSend(DefaultIOBuffer* buffer);
    void __async_AsyncSendConstBuffer(void* argument);
    int AsyncSendConstBuffer(DefaultIOBuffer* buffer);

    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;

    virtual void OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const;
    virtual CUserSocket* CastUserSocket();

    static CSocket* Allocate(std::uint64_t socket);
#if defined(L2_BUILD_L2SERVER)
    // Portable, not target code: lets the game server rewrite or drop a
    // client packet just before it is encrypted (LU4 client,
    // L2Server/src/Portable/Lu4Protocol.h). Each buffer holds one packet. The
    // hook returns what to send: the same buffer, a new one it allocated (the
    // caller releases the original), or nullptr to send nothing.
    using OutboundHook = DefaultIOBuffer* (*)(CSocket* socket, DefaultIOBuffer* buffer);
    static OutboundHook s_outboundHook;
#endif
    void SetKey(std::uint64_t key);
    void SetShuffleNum(int shuffleSeed);
    int GetShuffleNum();

    class DummyLock {
    public:
        DummyLock();
        void Enter();
        void Leave();
    };

    // Public at target offsets +0xb0 and +0xb8.
    std::uint64_t m_Socket;
    ::in_addr m_Addr;

    // The current RaidDelegatePacket reaches this physical callback lane
    // directly through the user's socket at +0xc0.
    PacketFunc* m_pPacketTable;

protected:
    // Target offsets +0xc8..+0x103. Alignment holes are compiler padding,
    // not source members.
    DummyLock m_OutSect;
    SocketMode m_Mode;
    std::int32_t m_nPacketLen;
    std::int32_t m_nPacketCount;
    std::int32_t m_nOldPacketCount;
    SocketSecureKey m_nInKey;
    SocketSecureKey m_nOutKey;
    std::int32_t m_nShuffleSeed;

    // Not a target source field. MSVC does not let a derived class reuse the
    // four alignment bytes after m_nShuffleSeed, while the Itanium ABI used
    // by Clang/GCC does. Materialize that ABI-only tail so derived target
    // fields still begin at CSocket +0x108 on every host. It deliberately
    // has no initializer: the current constructor leaves the matching
    // MSVC padding bytes untouched.
    std::array<std::uint8_t, 4> m_msvcTailPadding;

    void OnRead() override = 0;
};


namespace portable {

void ExecuteCSocketAsyncSend(MemoryObject* owner, void* argument);
void ExecuteCSocketAsyncSendConstBuffer(MemoryObject* owner, void* argument);

} // namespace portable
