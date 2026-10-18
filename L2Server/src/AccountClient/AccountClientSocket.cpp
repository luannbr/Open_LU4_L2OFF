#include "AccountClientSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/proto/Stream.h"

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <utility>

AccountClientCloseFunction::AccountClientCloseFunction() noexcept :
    m_invoker(nullptr), m_context(nullptr), m_manager(nullptr) {}

AccountClientCloseFunction::AccountClientCloseFunction(Invoker invoker, void* context) noexcept :
    m_invoker(invoker), m_context(context), m_manager(invoker != nullptr ? this : nullptr) {}

AccountClientCloseFunction::AccountClientCloseFunction(const AccountClientCloseFunction& other) noexcept :
    m_invoker(other.m_invoker), m_context(other.m_context), m_manager(other.m_manager != nullptr ? this : nullptr) {}

AccountClientCloseFunction::AccountClientCloseFunction(AccountClientCloseFunction&& other) noexcept :
    m_invoker(other.m_invoker), m_context(other.m_context), m_manager(other.m_manager != nullptr ? this : nullptr) {
    other.m_manager = nullptr;
}

AccountClientCloseFunction::operator bool() const noexcept { return m_manager != nullptr; }

void AccountClientCloseFunction::operator()(AccountClientSocket* socket) const {
    if (m_manager == nullptr || m_invoker == nullptr) {
        std::abort();
    }
    m_invoker(m_context, socket);
}

AccountClientSocket::AccountClientSocket(
    const std::uint64_t socket,
    PacketFunc* const packetTable,
    const unsigned char maxProtocolNo,
    AccountClientCloseFunction closeFunction
) :
    CSocket(socket, 0x1000), m_cMaxProtocolNo(maxProtocolNo), m_onClose(std::move(closeFunction)) {
    // Current L2Server 0x0041aee8 publishes the inherited table only after
    // cloning the close callable.
    m_pPacketTable = packetTable;
}

void AccountClientSocket::OnClose() {
    // Current L2Server 0x0041ae00 does not call CSocket's
    // close body. The captured stub owns publication and FunctionChain
    // release.
    L2_GUARD_FUNCTION();
    m_onClose(this);
}

void AccountClientSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void AccountClientSocket::SendPacket(const AccountProtocol::ACCOUNT_CS_PROTOCOL protocol, network::IPacket& packet) {
    // Current L2Server 0x0041b7cc. This AccountD frame has no extended
    // opcode and bypasses CSocket::OnSending.
    L2_GUARD_FUNCTION();
    if (m_Mode == SM_CLOSE) {
        return;
    }

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    runtime::raw_atomic::AddI32(&g_nTotalOutgoingPacketCount, 1);
    network::Stream stream(buffer->m_Buffer.data() + 3, 0x4ffd);
    if (!packet.Encode(&stream)) {
        buffer->Release();
        return;
    }

    const std::uint16_t frameSize = static_cast<std::uint16_t>(3 + (stream.GetCursor() - stream.GetBuffer()));
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameSize >> 8U);
    buffer->m_Buffer[2] = static_cast<std::uint8_t>(protocol);
    buffer->m_dwSize = frameSize;
    L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
    AsyncSend(buffer);
}

void AccountClientSocket::OnRead() {
    // Current L2Server 0x0041b980, including its two inherited RelayClient
    // diagnostic strings and the generic opcode+1 CPacketEx callback view.
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
                //     "RelayClientSocket %x: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen)
                // L2_LOG_ERROR(Log,
                //     "RelayClientSocket %x: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen)
                L2_LOG_ERROR(
                    Log,
                    "Account socket 0x%llx received invalid body length %d; expected 1 through 20478 bytes. Closing the "
                    "connection.",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), m_nPacketLen
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
            //     "RelayClientSocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     "RelayClientSocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log,
                "Account socket 0x%llx received opcode 0x%02x outside its %u-entry protocol table; closing the connection.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), static_cast<unsigned>(opcode),
                static_cast<unsigned>(m_cMaxProtocolNo)
            );
            m_Mode = SM_CLOSE;
            Close();
            return;
        }

        auto* const pendingPacket = new CPacketEx<DefaultIOBuffer, CSocket, PacketFunc>;
        pendingPacket->m_pSocket = this;
        pendingPacket->m_pBuf = m_pReadBuf;
        pendingPacket->m_pFunc = m_pPacketTable[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        pendingPacket->PostObject(packetOffset);

        packetOffset += m_nPacketLen;
        m_Mode = SM_READ_LEN;
    }
    Read(0);
}

void AccountClientSocket::OnSending(std::uint8_t, std::uint16_t, int) const {}
