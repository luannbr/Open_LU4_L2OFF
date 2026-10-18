#include "Shared/src/Socket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"

#include "Shared/src/Runtime/RawAtomic.h"
#if defined(L2_BUILD_L2SERVER)
#include "Shared/src/proto/Stream.h"
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#if defined(L2_BUILD_L2SERVER)
// Current L2Server Socket.obj storage at 0x20c85d50. Both Send routes use
// interlocked increment. The image fixes the process initial value to zero.
std::int32_t g_nTotalOutgoingPacketCount = 0;
std::int32_t g_nTotalOutgoingNpcPacketCount = 0;
#endif

void SocketSecureKey::SetKey(const KeyType& key) { std::memcpy(m_Key, key, sizeof(m_Key)); }

void SocketSecureKey::IncreaseKey(int amount) {
    // Socket.cpp stores the running position in key bytes 8..15. Decode it
    // as little-endian so the Windows result is kept even on
    // a host with a different native byte order.
    std::uint64_t position = 0;
    for (std::size_t index = 0; index < sizeof(position); ++index) {
        position |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(m_Key[8 + index])) << (index * 8U);
    }
    position += static_cast<std::uint64_t>(static_cast<std::int64_t>(amount));
    for (std::size_t index = 0; index < sizeof(position); ++index) {
        m_Key[8 + index] = static_cast<char>(position >> (index * 8U));
    }
}

SocketSecureKey::KeyType& SocketSecureKey::GetKey() { return m_Key; }

bool SocketSecureKey::IsEnabled() { return m_bIsEnabled; }

void SocketSecureKey::SetEnabled(bool enabled) { m_bIsEnabled = enabled; }

CSocket::DummyLock::DummyLock() = default;

void CSocket::DummyLock::Enter() {}

void CSocket::DummyLock::Leave() {}

void CSocket::Encrypt(std::uint8_t* buffer, SocketSecureKey& key, int length) {
    // Current L2NPC.exe 0x005367c8, Socket.cpp:32-49.
    SocketSecureKey::KeyType& bytes = key.GetKey();
    buffer[0] ^= static_cast<std::uint8_t>(bytes[0]);
    for (int index = 1; index < length; ++index) {
        buffer[index] ^= static_cast<std::uint8_t>(bytes[index & SocketSecureKey::CircularMask]) ^ buffer[index - 1];
    }
    key.IncreaseKey(length);
}

void CSocket::EncryptEx(std::uint8_t* destination, std::uint8_t* source, SocketSecureKey& key, int length) {
    // Linker-eliminated in the current image. The current PDB signature and
    // NASC64_2015 Shared/Socket.cpp:54-71 provide the body.
    SocketSecureKey::KeyType& bytes = key.GetKey();
    destination[0] = source[0] ^ static_cast<std::uint8_t>(bytes[0]);
    for (int index = 1; index < length; ++index) {
        destination[index] =
            source[index] ^ destination[index - 1] ^ static_cast<std::uint8_t>(bytes[index & SocketSecureKey::CircularMask]);
    }
    key.IncreaseKey(length);
}

CSocket::CSocket(std::uint64_t socket, int serializerLimit) : CIOSocket(socket, serializerLimit) {
    L2_GUARD_FUNCTION();
    m_Socket = socket;
    m_Addr.s_addr = 0;
    m_Mode = SocketMode::SM_READ_LEN;
    m_pPacketTable = nullptr;
    m_nPacketLen = 0;
    m_nPacketCount = 0;
    m_nOldPacketCount = 0;
    m_nShuffleSeed = 0;
}

CSocket::~CSocket() {
    L2_GUARD_FUNCTION();
    m_pPacketTable = nullptr;
}

CSocket* CSocket::Allocate(std::uint64_t) {
    L2_GUARD_FUNCTION();
    return nullptr;
}

void CSocket::SetAddress(::in_addr address) {
    L2_GUARD_FUNCTION();
    m_Addr = address;
}

::in_addr CSocket::GetAddress() const {
    L2_GUARD_FUNCTION();
    return m_Addr;
}

void CSocket::IssueRead() {
    L2_GUARD_FUNCTION();
    Read(0);
}

int CSocket::AsyncSend(DefaultIOBuffer* buffer) {
    return m_WriteControl.AddExecutable(this, &portable::ExecuteCSocketAsyncSend, buffer, L"AsyncSend");
}

void portable::ExecuteCSocketAsyncSend(MemoryObject* owner, void* argument) {
    static_cast<CSocket*>(owner)->__async_AsyncSend(argument);
}

#if defined(L2_BUILD_L2SERVER)
CSocket::OutboundHook CSocket::s_outboundHook = nullptr;
#endif

void CSocket::__async_AsyncSend(void* argument) {
    L2_GUARD_FUNCTION();
    auto* buffer = static_cast<DefaultIOBuffer*>(argument);
#if defined(L2_BUILD_L2SERVER)
    if (s_outboundHook != nullptr) {
        DefaultIOBuffer* const replaced = s_outboundHook(this, buffer);
        if (replaced == nullptr) {
            // _Write releases an empty buffer together with the
            // IoSocketWrite reference the sender took for it.
            buffer->m_dwSize = 0;
            _Write(buffer);
            return;
        }
        if (replaced != buffer) {
            buffer->Release();
            buffer = replaced;
        }
    }
#endif
    if (m_nOutKey.IsEnabled()) {
        Encrypt(buffer->m_Buffer.data() + 2, m_nOutKey, static_cast<int>(buffer->m_dwSize) - 2);
    }
    _Write(buffer);
}

int CSocket::AsyncSendConstBuffer(DefaultIOBuffer* buffer) {
    return m_WriteControl.AddExecutable(
        this, &portable::ExecuteCSocketAsyncSendConstBuffer, buffer, L"AsyncSendConstBuffer"
    );
}

void portable::ExecuteCSocketAsyncSendConstBuffer(MemoryObject* owner, void* argument) {
    static_cast<CSocket*>(owner)->__async_AsyncSendConstBuffer(argument);
}

void CSocket::__async_AsyncSendConstBuffer(void* argument) {
    // Linker-eliminated in current L2NPC. The current PDB keeps the method.
    // NASC64_2015 Shared/Socket.cpp:162-300 supplies the matching 20480-byte
    // implementation. Unlike __async_AsyncSend, this path keeps its input
    // immutable and queues a private encrypted/copy buffer.
    L2_GUARD_FUNCTION();
    auto* source = static_cast<DefaultIOBuffer*>(argument);
    if (source->m_dwSize == 0 || m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
        source->Release();
        return;
    }
#if defined(L2_BUILD_L2SERVER)
    if (s_outboundHook != nullptr) {
        // The shared source is never modified; a rewrite comes back as a copy.
        DefaultIOBuffer* const replaced = s_outboundHook(this, source);
        if (replaced != source) {
            source->Release();
            if (replaced == nullptr) {
                return;
            }
            source = replaced;
        }
    }
#endif

    const std::int32_t sourceSize = static_cast<std::int32_t>(source->m_dwSize);
    m_nPendingWrite.fetch_add(sourceSize, std::memory_order_acq_rel);
    if (!CheckPendingWrite()) {
        source->Release();
        Close();
        return;
    }
    g_nSocketPendingWrite.fetch_add(sourceSize, std::memory_order_acq_rel);

    const auto copyForQueue = [this, source]() {
        auto* const copy = DefaultIOBuffer::Alloc();
        copy->m_dwSize = source->m_dwSize;
        if (m_nOutKey.IsEnabled()) {
            std::memcpy(copy->m_Buffer.data(), source->m_Buffer.data(), 2);
            EncryptEx(
                copy->m_Buffer.data() + 2, source->m_Buffer.data() + 2, m_nOutKey, static_cast<int>(source->m_dwSize) - 2
            );
        } else {
            std::memcpy(copy->m_Buffer.data(), source->m_Buffer.data(), source->m_dwSize);
        }
        return copy;
    };

    if (!m_pLastBuf) {
        DefaultIOBuffer* const copy = copyForQueue();
        m_pFirstBuf = copy;
        m_pLastBuf = copy;
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        PortableOverlapped* const writeOverlapped = reinterpret_cast<PortableOverlapped*>(m_overlappedWrite.data());
        const portable::SocketWriteSubmission submission =
            portable::SubmitSocketWrite(this, copy->m_Buffer.data(), static_cast<int>(copy->m_dwSize), writeOverlapped);
        if (submission.IsFailed()) {
            const int error = GetSocketLastError();
            if (!IsExpectedSocketCloseError(error)) {
                // LogIOError(
                //     "CIOSocket::Write %x(%x) "
                //     "err=%d",
                //     static_cast<unsigned int>(
                //         m_hSocket.load(
                //             std::memory_order_acquire)),
                //     static_cast<unsigned int>(
                //         reinterpret_cast<
                //             std::uintptr_t>(
                //                 this)),
                //     error)
                // LogIOError(
                //     "[network] Cannot submit the copied write buffer of %u bytes on connection %llu (socket error %d)
                //     clearing the write queue and discarding the copy.", static_cast<unsigned int>(copy->m_dwSize),
                //     static_cast<unsigned long long>(
                //         m_hSocket.load(std::memory_order_acquire)),
                //     error)
                L2_LOG_IO_ERROR(
                    "[network] Cannot submit the copied write buffer of %u bytes on connection %llu (socket error %d); "
                    "clearing the write queue and discarding the copy.",
                    static_cast<unsigned int>(copy->m_dwSize),
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error
                );
            }
            // Noise: The unexpected send error is reported above. Expected terminal errors do not need a second error
            // record. LogIOError(
            //     L"[%s][%d] AsyncSendConstBuffer - "
            //     L"WSASend fail. error(%d)",
            //     L"..\\Shared\\Socket.cpp",
            //     0xfc,
            //     error)
            m_pFirstBuf = nullptr;
            m_pLastBuf = nullptr;
            copy->Release();
            L2_RELEASE(this, ObjectRefType::IoRetain);
        } else if (submission.IsCompleted()) {
            if (!portable::PostIOCompletion(this, submission.transferred, writeOverlapped)) {
                OnIOCallback(1, submission.transferred, writeOverlapped);
            }
        }
    } else if (m_pFirstBuf != m_pLastBuf && m_pLastBuf->m_dwSize + source->m_dwSize <= 0x5000U) {
        std::uint8_t* const destination = m_pLastBuf->m_Buffer.data() + m_pLastBuf->m_dwSize;
        if (m_nOutKey.IsEnabled()) {
            std::memcpy(destination, source->m_Buffer.data(), 2);
            EncryptEx(destination + 2, source->m_Buffer.data() + 2, m_nOutKey, static_cast<int>(source->m_dwSize) - 2);
        } else {
            std::memcpy(destination, source->m_Buffer.data(), source->m_dwSize);
        }
        m_pLastBuf->m_dwSize += source->m_dwSize;
    } else {
        DefaultIOBuffer* const copy = copyForQueue();
        m_pLastBuf->m_pNext = copy;
        m_pLastBuf = copy;
    }
    source->Release();
}

void CSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

#if defined(L2_BUILD_L2SERVER)
void CSocket::Send(S_PROTOCOL protocol, network::IPacket& packet) { Send(protocol, static_cast<S_EX_PROTOCOL>(0), packet); }

void CSocket::Send(S_PROTOCOL protocol, S_EX_PROTOCOL extendedProtocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (m_Mode == SM_CLOSE) {
        return;
    }

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    runtime::raw_atomic::AddI32(&g_nTotalOutgoingPacketCount, 1);

    const std::int32_t protocolValue = static_cast<std::int32_t>(protocol);
    const bool isExtended = protocolValue == 0xfe;
    const int headerSize = isExtended ? 5 : 3;
    network::Stream stream(buffer->m_Buffer.data() + headerSize, 0x5000 - headerSize);
    if (!packet.Encode(&stream)) {
        if (isExtended) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[network] Cannot encode outgoing opcode 0xfe:0x%04x on connection %llu (payload capacity %d bytes)
            //     discarding the packet.", static_cast<unsigned int>(static_cast<std::uint16_t>(
            //         static_cast<std::int32_t>(extendedProtocol))),
            //     static_cast<unsigned long long>(
            //         m_hSocket.load(std::memory_order_acquire)),
            //     0x5000 - headerSize)
            L2_LOG_ERROR(
                Log,
                "[network] Cannot encode outgoing opcode 0xfe:0x%04x on connection %llu (payload capacity %d bytes); "
                "discarding the packet.",
                static_cast<unsigned int>(static_cast<std::uint16_t>(static_cast<std::int32_t>(extendedProtocol))),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), 0x5000 - headerSize
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[network] Cannot encode outgoing opcode 0x%02x on connection %llu (payload capacity %d bytes). Discarding
            //     the packet.", static_cast<unsigned int>(static_cast<std::uint8_t>(protocolValue)), static_cast<unsigned
            //     long long>(
            //         m_hSocket.load(std::memory_order_acquire)),
            //     0x5000 - headerSize)
            L2_LOG_ERROR(
                Log,
                "[network] Cannot encode outgoing opcode 0x%02x on connection %llu (payload capacity %d bytes); discarding "
                "the packet.",
                static_cast<unsigned int>(static_cast<std::uint8_t>(protocolValue)),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), 0x5000 - headerSize
            );
        }
        buffer->Release();
        return;
    }

    const std::uint16_t frameSize = static_cast<std::uint16_t>(headerSize + (stream.GetCursor() - stream.GetBuffer()));
    const std::uint8_t opcode = static_cast<std::uint8_t>(protocolValue);
    const std::uint16_t extendedOpcode =
        isExtended ? static_cast<std::uint16_t>(static_cast<std::int32_t>(extendedProtocol)) : 0;

    buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameSize >> 8U);
    buffer->m_Buffer[2] = opcode;
    if (isExtended) {
        buffer->m_Buffer[3] = static_cast<std::uint8_t>(extendedOpcode);
        buffer->m_Buffer[4] = static_cast<std::uint8_t>(extendedOpcode >> 8U);
    }

    OnSending(opcode, extendedOpcode, frameSize);
    buffer->m_dwSize = frameSize;
    L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
    AsyncSend(buffer);
}
#endif

void CSocket::SendV(const char* format, va_list arguments) {
    // Current L2NPC.exe 0x005371a4. Donor Shared/Socket.cpp:359-410.
    L2_GUARD_FUNCTION();
    if (m_Mode == SocketMode::SM_CLOSE) {
        return;
    }

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    runtime::raw_atomic::AddI32(&g_nTotalOutgoingPacketCount, 1);
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    const std::uint16_t encodedBodySize = static_cast<std::uint16_t>(bodySize);
    if (encodedBodySize == 0 || encodedBodySize > 0x5000) {
        // Zero-length overflow results are already diagnosed by VAssemble.
        if (encodedBodySize > 0x5000) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[network] Assembler returned invalid body length %d (16-bit length %u) on connection %llu. Discarding the
            //     packet.", bodySize, static_cast<unsigned int>(encodedBodySize), static_cast<unsigned long long>(
            //         m_hSocket.load(std::memory_order_acquire)))
            L2_LOG_ERROR(
                Log,
                "[network] Assembler returned invalid body length %d (16-bit length %u) on connection %llu; discarding the "
                "packet.",
                bodySize, static_cast<unsigned int>(encodedBodySize),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
            );
        }
        buffer->Release();
        return;
    }

    const std::uint16_t frameSize = static_cast<std::uint16_t>(encodedBodySize + 2);
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameSize >> 8U);

    const std::uint8_t opcode = buffer->m_Buffer[2];
    std::uint16_t extendedOpcode = 0;
    if (opcode == 0xfe) {
        extendedOpcode = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(buffer->m_Buffer[3]) | (static_cast<std::uint16_t>(buffer->m_Buffer[4]) << 8U)
        );
    }
#if defined(L2_BUILD_L2SERVER)
    if (opcode == 0xfe && extendedOpcode == 0x008e && std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] SendV format=%s body=%u frame=%u "
        //     "user=%02x%02x%02x%02x html_first=%02x%02x\n",
        //     format,
        //     static_cast<unsigned int>(encodedBodySize),
        //     static_cast<unsigned int>(frameSize),
        //     static_cast<unsigned int>(buffer->m_Buffer[5]),
        //     static_cast<unsigned int>(buffer->m_Buffer[6]),
        //     static_cast<unsigned int>(buffer->m_Buffer[7]),
        //     static_cast<unsigned int>(buffer->m_Buffer[8]),
        //     static_cast<unsigned int>(buffer->m_Buffer[9]),
        //     static_cast<unsigned int>(buffer->m_Buffer[10]))
        // std::fflush(stderr)
        // Keep the explicit trace switch visible even when ordinary DEBUG is
        // disabled. Only check fields inside the assembled frame.
        if (encodedBodySize >= 9U) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "[quest trace] Prepared HTML packet: body %u bytes, frame %u bytes. User bytes %02x %02x %02x %02x, first
            //     HTML bytes %02x %02x, format %s.", static_cast<unsigned int>(encodedBodySize), static_cast<unsigned
            //     int>(frameSize), static_cast<unsigned int>(buffer->m_Buffer[5]), static_cast<unsigned
            //     int>(buffer->m_Buffer[6]), static_cast<unsigned int>(buffer->m_Buffer[7]), static_cast<unsigned
            //     int>(buffer->m_Buffer[8]), static_cast<unsigned int>(buffer->m_Buffer[9]), static_cast<unsigned
            //     int>(buffer->m_Buffer[10]), format)
            L2_LOG_INFO(
                Log,
                "[quest trace] Prepared HTML packet: body %u bytes, frame %u bytes; user bytes %02x %02x %02x %02x, first "
                "HTML bytes %02x %02x; format %s.",
                static_cast<unsigned int>(encodedBodySize), static_cast<unsigned int>(frameSize),
                static_cast<unsigned int>(buffer->m_Buffer[5]), static_cast<unsigned int>(buffer->m_Buffer[6]),
                static_cast<unsigned int>(buffer->m_Buffer[7]), static_cast<unsigned int>(buffer->m_Buffer[8]),
                static_cast<unsigned int>(buffer->m_Buffer[9]), static_cast<unsigned int>(buffer->m_Buffer[10]), format
            );
        } else {
            // Log.Add(
            //     LOG_WARN,
            //     "[quest trace] HTML packet body is too short for the traced fields (%u bytes, frame %u bytes). Skipping
            //     payload details.", static_cast<unsigned int>(encodedBodySize), static_cast<unsigned int>(frameSize))
            L2_LOG_WARN(
                Log,
                "[quest trace] HTML packet body is too short for the traced fields (%u bytes, frame %u bytes); skipping "
                "payload details.",
                static_cast<unsigned int>(encodedBodySize), static_cast<unsigned int>(frameSize)
            );
        }
    }
#endif
    OnSending(opcode, extendedOpcode, frameSize);
    buffer->m_dwSize = frameSize;
    L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
    AsyncSend(buffer);
}

void CSocket::OnSending(std::uint8_t, std::uint16_t, int) const {}

CUserSocket* CSocket::CastUserSocket() { return nullptr; }

void CSocket::SetKey(std::uint64_t key) {
    L2_GUARD_FUNCTION();
    static constexpr std::uint8_t privateKey[8] = {
        0xc8,
        0x27,
        0x93,
        0x01,
        0xa1,
        0x6c,
        0x31,
        0x97,
    };
    SocketSecureKey::KeyType combinedKey{};
    for (std::size_t index = 0; index < 8; ++index) {
        combinedKey[index] = static_cast<char>(key >> (index * 8U));
        combinedKey[index + 8] = static_cast<char>(privateKey[index]);
    }
    m_nInKey.SetKey(combinedKey);
    m_nInKey.SetEnabled(true);
    m_nOutKey.SetKey(combinedKey);
    m_nOutKey.SetEnabled(true);
}

void CSocket::SetShuffleNum(int shuffleSeed) {
    L2_GUARD_FUNCTION();
    m_nShuffleSeed = shuffleSeed;
}

int CSocket::GetShuffleNum() {
    L2_GUARD_FUNCTION();
    return m_nShuffleSeed;
}

std::u16string CSocket::GetIPAddressString() {
    L2_GUARD_FUNCTION();
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&m_Addr.s_addr);
    char16_t address[1000]{};
    FormatUtf16To(
        address, 999, u"%d.%d.%d.%d", static_cast<int>(bytes[0]), static_cast<int>(bytes[1]), static_cast<int>(bytes[2]),
        static_cast<int>(bytes[3])
    );
    return std::u16string(address);
}

std::uint32_t CSocket::GetIPAddressUINT() {
    L2_GUARD_FUNCTION();
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&m_Addr.s_addr);
    return static_cast<std::uint32_t>(bytes[0]) * 0x1000000U + static_cast<std::uint32_t>(bytes[1]) * 0x10000U +
        static_cast<std::uint32_t>(bytes[2]) * 0x100U + static_cast<std::uint32_t>(bytes[3]);
}
