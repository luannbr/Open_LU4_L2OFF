#include "Captcha.h"
#include "CConfig.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <new>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {


int CurrentCaptchaLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

// int LegacyCaptchaSocketSymbolForLog() noexcept {
[[maybe_unused]] int LegacyCaptchaSocketSymbolForLog() noexcept {
    return static_cast<int>(reinterpret_cast<std::intptr_t>(&::socket));
}

void CloseCaptchaNativeSocket(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

void StoreLittleEndian16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}


std::uint32_t CaptchaConnectThread(void*) {
    CThreadManager::RegisterThisThread(ThreadID::CAPTCHA);
    const int threadId = CThreadManager::GetThreadID();
    g_IOBufferPools[threadId] = new (std::nothrow) CNewMwsrQueue<DefaultIOBuffer>(100);

    const auto run = [threadId] {
        for (;;) {
            g_dwGuardStartTick[threadId] = runtime::GetTickCount();
            g_CaptchaManager.Init();
            std::this_thread::sleep_for(std::chrono::milliseconds(g_CaptchaSetting.heartBeat));
        }
    };
    if (ExceptionMailing()) {
        // Log.Add(LOG_NORMAL, L"captcha in mailing mode")
        // L2_LOG_INFO(Log, L"captcha in mailing mode")
        L2_LOG_INFO(Log, "CAPTCHA connection worker started with exception-mail reporting enabled.");
        try {
            run();
        } catch (...) {
            runtime::process::HandleUnhandledSharedThreadException("Captcha.cpp", 0x17c, "Thu May 10 05:00:26 2018");
        }
    } else {
        run();
    }
    return 0;
}

} // namespace

CCaptchaManager g_CaptchaManager;
CCaptchaCallbackMap g_CaptchaPacketTable(DummyCaptchaPacket, u"");

CCaptchaManager::~CCaptchaManager() {
    // Current atexit thunk 00dd51e4 drops these two borrowed carriers before
    // the CObjectMap member destructor. It does not close either resource.
    m_hConnectThread = nullptr;
    m_pCaptchaSocket = nullptr;
}

namespace {

L2_BIND_PACKET(IsRegistered_CP_REPLY_CHECK_KEEP_ALIVE, g_CaptchaPacketTable, 0x01, CRReplyCheckKeepAlive);
L2_BIND_PACKET(IsRegistered_CP_REPLY_CHECK_VERSION, g_CaptchaPacketTable, 0x66, CRReplyCheckVersion);
L2_BIND_PACKET(IsRegistered_CP_REPLY_CAPTCHA_IMAGE, g_CaptchaPacketTable, 0x3ea, CRReplyCaptchaImage);

} // namespace

L2_DEFINE_OBJECT_NEW(CCaptchaJob)

L2_DEFINE_OBJECT_ALLOCATOR(CCaptchaJob, m_pClassName_CCaptchaJob)

CCaptchaJob::CCaptchaJob(const std::uint32_t accountId, const CAPTCHA::JOB_TYPE jobType) {
    // Current L2Server 0x0042be08 stores the account/type before advancing
    // the signed transaction sequence, then clears exactly the 14-byte
    // answer buffer and its parsed integer.
    m_nAccountId = accountId;
    m_jobType = jobType;
    L2_GUARD_FUNCTION();
    m_nTransactionId = g_CaptchaManager.CreateTransactionId();
    std::memset(m_answer.data(), 0, 0x0e);
    m_nAnswer = 0;
}

// Current L2Server CCaptchaJob::Release, Captcha.h line 0x68.
L2_DEFINE_OBJECT_RELEASE(CCaptchaJob)

int CCaptchaJob::ConvertAnswerToInt(const char16_t* answer) {
    // Current L2Server 0x0042b7f0 scans all ten digits for
    // each of exactly six UTF-16 positions and performs the double-to-int
    // conversion after every matching digit.
    static constexpr std::array<char16_t, 10> kAnswerCharacters{u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7', u'8', u'9'};
    L2_GUARD_FUNCTION();
    int result = 0;
    int exponent = 5;
    for (int position = 0; position < 6; ++position, --exponent) {
        for (int digit = 0; digit < 10; ++digit) {
            if (answer[position] == kAnswerCharacters[static_cast<std::size_t>(digit)]) {
                const double power = std::pow(10.0, static_cast<double>(exponent));
                result = static_cast<int>(power * static_cast<double>(digit) + static_cast<double>(result));
            }
        }
    }
    return result;
}

void CCaptchaJob::SetAnswer(const char16_t* answer) {
    // Current L2Server 0x0042c010 copies all 14 bytes, including the final
    // terminator lane supplied by the reply decoder.
    L2_GUARD_FUNCTION();
    std::memcpy(m_answer.data(), answer, 0x0e);
    m_nAnswer = ConvertAnswerToInt(m_answer.data());
}

L2_DEFINE_OBJECT_NEW(CCaptchaSocket)

L2_DEFINE_OBJECT_ALLOCATOR(CCaptchaSocket, m_pClassName_CCaptchaSocket)

CCaptchaSocket::CCaptchaSocket(const std::uint64_t socket) : CIOSocket(socket, 0x1000) {
    // Current L2Server 0x0042c59c.
    L2_GUARD_FUNCTION();
    m_socket = socket;
    m_threadCount = 0;
    m_packetTable = g_CaptchaPacketTable.GetData();
    m_mode = SocketMode::SM_READ_LEN;
    m_packetLength = 0;
    // Log.Add(
    //     LOG_NORMAL,
    //     L"captcha socket open %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // L2_LOG_INFO(Log,
    //     L"captcha socket open %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    L2_LOG_INFO(
        Log, "CAPTCHA socket %llu created (object %p); protocol validation is pending.",
        static_cast<unsigned long long>(m_socket), static_cast<void*>(this)
    );
}

CCaptchaSocket::~CCaptchaSocket() { m_packetTable = nullptr; }

L2_DEFINE_OBJECT_RELEASE(CCaptchaSocket)

bool CCaptchaSocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid captcha socket.",
        //     L"Captcha.cpp",
        //     0xe7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Write to invalid captcha socket.",
        //     L"Captcha.cpp",
        //     0xe7)
        L2_LOG_ERROR(Log, "CAPTCHA write rejected: native socket is closed (socket object %p).", static_cast<void*>(this));
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"captcha socket pending write > %d KB, Close!",
    //     0x2000)
    // L2_LOG_ERROR(Log,
    //     L"captcha socket pending write > %d KB, Close!",
    //     0x2000)
    L2_LOG_ERROR(
        Log, "CAPTCHA socket %llu pending writes exceed 8 MiB; write check fails and reports the stack.",
        static_cast<unsigned long long>(m_socket)
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

void CCaptchaSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CCaptchaSocket::SendV(const char* format, va_list arguments) {
    // Current L2Server 0x0042d3e4 uses the complete 0x5000-byte buffer.
    // only the two-byte framing header is reserved.
    L2_GUARD_FUNCTION();
    if (m_mode == SocketMode::SM_CLOSE) {
        return;
    }
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const int bodySize = VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    std::uint16_t frameSize = static_cast<std::uint16_t>(bodySize);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     LegacyCaptchaSocketSymbolForLog(),
        //     format)
        // L2_LOG_ERROR(Log,
        //     L"%d: assemble too large packet. format %s",
        //     LegacyCaptchaSocketSymbolForLog(),
        //     format)
        L2_LOG_ERROR(
            Log,
            "CAPTCHA socket %llu could not assemble packet format '%s' within 20478 bytes; passing a zero-length buffer to "
            "Write.",
            static_cast<unsigned long long>(m_socket), format ? format : "<null>"
        );
    } else {
        frameSize = static_cast<std::uint16_t>(bodySize + 2);
        StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
    }
    buffer->m_dwSize = frameSize;
    Write(buffer);
}

void CCaptchaSocket::OnClose() {
    // Current L2Server 0x0042b6dc clears the manager's combined connected /
    // sendable state after publishing the exact 0x51d socket audit.
    L2_GUARD_FUNCTION();
    m_mode = SocketMode::SM_CLOSE;
    const int error = CurrentCaptchaLastError();
    // Log.Add(
    //     LOG_ERROR,
    //     L"CCaptchaSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     error)
    // L2_LOG_ERROR(Log,
    //     L"CCaptchaSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     error)
    L2_LOG_WARN(
        Log, "CAPTCHA socket %llu closed (object %p); thread last-error snapshot %d.",
        static_cast<unsigned long long>(m_socket), static_cast<void*>(this), error
    );
    Log.Add(
        LOG_DB, u"%d,,,,,,,,,,,%d,%x,%x,%d,,,,,,,,,,,", 0x51d, 1, static_cast<unsigned int>(m_socket),
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(this)), error
    );
    g_CaptchaManager.m_bConnected = false;
    g_CaptchaManager.m_bSendable = false;
    g_CaptchaManager.m_alignment072To078[0] = std::byte{};
    g_CaptchaManager.m_alignment072To078[1] = std::byte{};
}

void CCaptchaSocket::OnRead() {
    // Current L2Server 0x0042cdb0: two-byte frame length, two-byte opcode,
    // and a body extent of 1..0x4ffe inclusive.
    L2_GUARD_FUNCTION();
    if (m_mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_mode == SocketMode::SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(readSize - packetOffset);
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength = static_cast<std::int32_t>(
                                 static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8U)
                             ) -
                2;
            if (m_packetLength < 1 || m_packetLength > 0x4ffe) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"captcha socket %d: bad packet size %d",
                //     LegacyCaptchaSocketSymbolForLog(),
                //     m_packetLength)
                // L2_LOG_ERROR(Log,
                //     L"captcha socket %d: bad packet size %d",
                //     LegacyCaptchaSocketSymbolForLog(),
                //     m_packetLength)
                L2_LOG_ERROR(
                    Log, "CAPTCHA socket %llu received body length %d outside 1..20478; closing connection.",
                    static_cast<unsigned long long>(m_socket), m_packetLength
                );
                m_mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        } else if (m_mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }

        if (readSize < packetOffset + m_packetLength) {
            Read(readSize - packetOffset);
            return;
        }

        const std::uint8_t* const opcodeBytes = m_pReadBuf->m_Buffer.data() + packetOffset;
        const std::uint32_t opcode =
            static_cast<std::uint32_t>(opcodeBytes[0]) | (static_cast<std::uint32_t>(opcodeBytes[1]) << 8U);
        if (opcode > 0x3ea) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Captcha] unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     L"[Captcha] unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log, "CAPTCHA socket %llu received opcode %u above 1002; closing connection.",
                static_cast<unsigned long long>(m_socket), static_cast<unsigned>(opcode)
            );
            m_mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new CCaptchaPacket;
        packet->m_pSocket = this;
        m_pReadBuf->AddRef();
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_packetTable[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_packetLength;
        m_mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

void CCaptchaManager::ConnectToCaptchaServer() {
    // Current L2Server 0x0042b8f8.
    L2_GUARD_FUNCTION();
    if (!m_bConnected) {
        runtime::ResumeOwnedThread(m_hConnectThread);
    }
}

void CCaptchaManager::RequestCheckVersion() {
    // Current L2Server 0x0042b990.
    L2_GUARD_FUNCTION();
    if (m_bConnected && g_CaptchaSetting.enable) {
        if (m_pCaptchaSocket == nullptr) {
            m_bConnected = false;
        } else {
            m_pCaptchaSocket->Send("hhh", 0x65, 1, 0x656e);
        }
    }
}

bool CCaptchaManager::Connect() {
    // Current L2Server 0x0042c7d8.
    L2_GUARD_FUNCTION();
    if (m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Captcha] Already connected to Captcha Server")
        // L2_LOG_ERROR(Log,
        //     L"[Captcha] Already connected to Captcha Server")
        L2_LOG_WARN(Log, "CAPTCHA connect request skipped: manager already reports a connection.");
        return false;
    }

    const std::uint64_t socket = static_cast<std::uint64_t>(::socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr = g_CaptchaSetting.serverAddr;
    address.sin_port = htons(static_cast<std::uint16_t>(g_CaptchaSetting.serverPort));
    if (::connect(static_cast<int>(socket), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nLogTick > 60000U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Captcha] Can't connect to Captcha Server")
            // L2_LOG_ERROR(Log,
            //     L"[Captcha] Can't connect to Captcha Server")
            L2_LOG_ERROR(
                Log, "CAPTCHA TCP connection to %u.%u.%u.%u:%u failed; connection worker will retry.",
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 24U) & 255U,
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 16U) & 255U,
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 8U) & 255U,
                static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) & 255U, static_cast<unsigned>(ntohs(address.sin_port))
            );
            m_nLogTick = runtime::GetTickCount();
        }
        CloseCaptchaNativeSocket(socket);
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"[Captcha] Connected to Captcha Server")
    // L2_LOG_INFO(Log,
    //     L"[Captcha] Connected to Captcha Server")
    L2_LOG_INFO(
        Log, "CAPTCHA TCP connection established on socket %llu; protocol validation is pending.",
        static_cast<unsigned long long>(socket)
    );
    m_pCaptchaSocket = new CCaptchaSocket(socket);
    m_pCaptchaSocket->Initialize();
    Log.Add(
        LOG_DB, u"%d,,,,,,,,,,,%d,%x,%x,%d,,,,,,,,,,,", 0x51d, 0, static_cast<unsigned int>(socket),
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(m_pCaptchaSocket)), 0
    );
    return true;
}

bool CCaptchaManager::Init() {
    // Current L2Server 0x0042d034 returns false on every path.
    // the connect worker ignores the return value.
    L2_GUARD_FUNCTION();
    if (g_CaptchaSetting.enable && !m_bConnected) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nLogTick > 60000U) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[Captcha] Connecting Captcha Server...")
            // L2_LOG_INFO(Log,
            //     L"[Captcha] Connecting Captcha Server...")
            L2_LOG_INFO(Log, "Attempting connection to the configured CAPTCHA server.");
        }
        if (!Connect()) {
            m_bConnected = false;
        } else {
            m_bConnected = true;
            RequestCheckVersion();
        }
    }
    return false;
}

void CCaptchaManager::CreateThread() {
    // Current L2Server 0x0042d270 creates the worker suspended and registers
    // its handle in ThreadManager before ConnectToCaptchaServer resumes it.
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(CaptchaConnectThread, nullptr, true);
    m_nConnectThread = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::CAPTCHA, m_hConnectThread);
}

std::int64_t CCaptchaManager::CreateTransactionId() {
    // Current L2Server 0x0042ba68 uses the signed 32-bit sequence counter.
    L2_GUARD_FUNCTION();
    return static_cast<std::int64_t>(runtime::raw_atomic::AddI32(&m_nTransactionId, 1));
}

bool CCaptchaManager::PushCaptchaJob(CCaptchaJob* const job) {
    // Current L2Server 0x0042d810.
    L2_GUARD_FUNCTION();
    if (job != nullptr) {
        m_mapJobs.Insert(static_cast<int>(job->m_nTransactionId), job);
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d][Captcha] Push Null CaptchaJob.",
    //     L"Captcha.cpp",
    //     0x2c2)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d][Captcha] Push Null CaptchaJob.",
    //     L"Captcha.cpp",
    //     0x2c2)
    L2_LOG_ERROR(Log, "Cannot register a null CAPTCHA job.");
    return false;
}

CSPointer<CCaptchaJob> CCaptchaManager::PopCaptchaJob(const std::int64_t transactionId) {
    // Current L2Server 0x0042d8f4 indexes by the low dword.
    L2_GUARD_FUNCTION();
    const int key = static_cast<int>(transactionId);
    CSPointer<CCaptchaJob> job = m_mapJobs.FindObject(key);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Cannot Find CaptchaJob. TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x2d9,
        //     key)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Cannot Find CaptchaJob. TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x2d9,
        //     key)
        L2_LOG_WARN(
            Log, "Cannot remove CAPTCHA job for transaction %lld (map key %d): job not found.",
            static_cast<long long>(transactionId), key
        );
        return job;
    }
    m_mapJobs.DeleteObject(key);
    return job;
}

void CCaptchaManager::RequestCaptchaImage(const std::uint32_t accountId) {
    // Current L2Server 0x0042e2b0.
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Captcha Server Connection Is Closed.",
        //     L"Captcha.cpp",
        //     0x25c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Captcha Server Connection Is Closed.",
        //     L"Captcha.cpp",
        //     0x25c)
        L2_LOG_WARN(
            Log, "Cannot request CAPTCHA for account %u: connected=%s, protocol ready=%s.", static_cast<unsigned>(accountId),
            m_bConnected ? "yes" : "no", m_bSendable ? "yes" : "no"
        );
        return;
    }

    User* user = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account != accountMap.end() && account->second != nullptr) {
            user = account->second->GetUser();
        }
    }

    if (user != nullptr && user->m_nCaptchaConfirmTransactionId != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Captcha Job Is Already Progress. AccountId[%d]",
        //     L"Captcha.cpp",
        //     0x270,
        //     accountId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Captcha Job Is Already Progress. AccountId[%d]",
        //     L"Captcha.cpp",
        //     0x270,
        //     accountId)
        L2_LOG_WARN(
            Log, "CAPTCHA request for account %u skipped: confirmation transaction is already active.",
            static_cast<unsigned>(accountId)
        );
        return;
    }

    void* const storage = CCaptchaJob::operator new(sizeof(CCaptchaJob), true);
    CCaptchaJob* const job = storage == nullptr ? nullptr : ::new (storage) CCaptchaJob(accountId, CAPTCHA::JT_REQUEST);
    PushCaptchaJob(job);
    m_pCaptchaSocket->Send("hQdh", 0x3e9, job->m_nTransactionId, 0x4b0, 0x656e);
}

bool DummyCaptchaPacket(CCaptchaSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in CCaptchaSocket")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in CCaptchaSocket")
    L2_LOG_ERROR(Log, "Reserved CAPTCHA packet handler invoked.");
    return false;
}

bool CRReplyCheckKeepAlive(CCaptchaSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "CRReplyCheckKeepAlive called in CCaptchaSocket")
    // L2_LOG_ERROR(Log,
    //     "CRReplyCheckKeepAlive called in CCaptchaSocket")
    // Noise: routine CAPTCHA keep-alive reply. The existing handler return value is unchanged.
    return false;
}

bool CRReplyCheckVersion(CCaptchaSocket*, const unsigned char* packet) {
    // Current L2Server 0x0042b4a0.
    L2_GUARD_FUNCTION();
    std::int16_t version = 0;
    std::int16_t language = 0;
    Disassemble(packet, "hh", &version, &language);
    if (version == 1 && language == 0x656e) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Captcha] Connect Captcha Server Success!")
        // L2_LOG_INFO(Log,
        //     L"[Captcha] Connect Captcha Server Success!")
        L2_LOG_INFO(Log, "CAPTCHA protocol accepted: version 1, language 0x656e; requests enabled.");
        g_CaptchaManager.m_bSendable = true;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Captcha] Captcha Server Version is not match with Game Server[%d][%d]",
        //     static_cast<int>(version),
        //     static_cast<int>(language))
        // L2_LOG_ERROR(Log,
        //     L"[Captcha] Captcha Server Version is not match with Game Server[%d][%d]",
        //     static_cast<int>(version),
        //     static_cast<int>(language))
        L2_LOG_ERROR(
            Log,
            "CAPTCHA protocol mismatch: received version %d, language 0x%04x; expected version 1, language 0x656e. Existing "
            "sendable flag is unchanged.",
            static_cast<int>(version), static_cast<unsigned>(static_cast<std::uint16_t>(language))
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Captcha] Captcha Server Will Not Work!!")
        // L2_LOG_ERROR(Log,
        //     L"[Captcha] Captcha Server Will Not Work!!")
        // Noise: the preceding version-mismatch diagnostic already explains this failure. No readiness state changes here.
    }
    return false;
}

bool CRReplyCaptchaImage(CCaptchaSocket*, const unsigned char* packet) {
    // Current L2Server 0x0042de60. Every rejection consumes the outstanding
    // job. A valid image leaves it live until confirmation or refresh.
    L2_GUARD_FUNCTION();
    std::int64_t transactionId = 0;
    int result = 0;
    const unsigned char* cursor = Disassemble(packet, "Qd", &transactionId, &result);
    if (result != 0) {
        g_CaptchaManager.PopCaptchaJob(transactionId);
        return false;
    }

    std::int16_t imageSize = 0;
    std::uint8_t imageFormat = 2;
    std::int8_t answerSize = 0;
    cursor = Disassemble(cursor, "hcc", &imageSize, &imageFormat, &answerSize);
    if (imageFormat != 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Return Image Format Is Not DDS Format. Confirm The CAPTCHA Server. TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x53,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Return Image Format Is Not DDS Format. Confirm The CAPTCHA Server. TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x53,
        //     transactionId)
        L2_LOG_ERROR(
            Log, "CAPTCHA transaction %lld: image format %u is not DDS (2); rejecting image and attempting job removal.",
            static_cast<long long>(transactionId), static_cast<unsigned>(imageFormat)
        );
        g_CaptchaManager.PopCaptchaJob(transactionId);
        return false;
    }
    if (imageSize < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Return Image Size Is Invalid!! TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x5b,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Return Image Size Is Invalid!! TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x5b,
        //     transactionId)
        L2_LOG_ERROR(
            Log, "CAPTCHA transaction %lld: image size %d is below 1; rejecting image and attempting job removal.",
            static_cast<long long>(transactionId), static_cast<int>(imageSize)
        );
        g_CaptchaManager.PopCaptchaJob(transactionId);
        return false;
    }
    if (imageSize > 0x4000) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Return Image Size Is Too Large!! TransactionId[%d], ImageSize[%d]",
        //     L"Captcha.cpp",
        //     0x65,
        //     transactionId,
        //     static_cast<int>(imageSize))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Return Image Size Is Too Large!! TransactionId[%d], ImageSize[%d]",
        //     L"Captcha.cpp",
        //     0x65,
        //     transactionId,
        //     static_cast<int>(imageSize))
        L2_LOG_ERROR(
            Log, "CAPTCHA transaction %lld: image size %d exceeds 16384 bytes; rejecting image and attempting job removal.",
            static_cast<long long>(transactionId), static_cast<int>(imageSize)
        );
        g_CaptchaManager.PopCaptchaJob(transactionId);
        return false;
    }
    if (answerSize < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Return Answer Letters Size Is Invalid!! TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x6d,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Return Answer Letters Size Is Invalid!! TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x6d,
        //     transactionId)
        L2_LOG_ERROR(
            Log, "CAPTCHA transaction %lld: answer byte count %d is below 1; rejecting image and attempting job removal.",
            static_cast<long long>(transactionId), static_cast<int>(answerSize)
        );
        g_CaptchaManager.PopCaptchaJob(transactionId);
        return false;
    }
    if (answerSize >= 0x0d) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Return Answer Letters Size Is Too Large!! TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x75,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Return Answer Letters Size Is Too Large!! TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x75,
        //     transactionId)
        L2_LOG_ERROR(
            Log, "CAPTCHA transaction %lld: answer byte count %d exceeds 12; rejecting image and attempting job removal.",
            static_cast<long long>(transactionId), static_cast<int>(answerSize)
        );
        g_CaptchaManager.PopCaptchaJob(transactionId);
        return false;
    }

    std::array<std::uint8_t, 0x4000> image{};
    cursor = Disassemble(cursor, "b", static_cast<int>(imageSize), image.data());
    std::array<char16_t, 7> answer{};
    Disassemble(cursor, "b", static_cast<int>(answerSize) + 1, answer.data());
    g_CaptchaManager.SetAnswerAndSendImageToClient(transactionId, answer.data(), imageSize, image.data());
    return false;
}

void CCaptchaManager::RefreshCaptchaImage(const std::uint32_t accountId, const std::int64_t transactionId) {
    // Current L2Server 0x0042e4c0.
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Captcha Server Connection Is Closed.",
        //     L"Captcha.cpp",
        //     0x284)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Captcha Server Connection Is Closed.",
        //     L"Captcha.cpp",
        //     0x284)
        L2_LOG_WARN(
            Log, "Cannot refresh CAPTCHA transaction %lld for account %u: connected=%s, protocol ready=%s.",
            static_cast<long long>(transactionId), static_cast<unsigned>(accountId), m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return;
    }

    CSPointer<CCaptchaJob> oldJob = PopCaptchaJob(transactionId);
    if (oldJob) {
        void* const storage = CCaptchaJob::operator new(sizeof(CCaptchaJob), true);
        CCaptchaJob* const job = storage == nullptr ? nullptr : ::new (storage) CCaptchaJob(accountId, CAPTCHA::JT_REFRESH);
        PushCaptchaJob(job);
        m_pCaptchaSocket->Send("hQdh", 0x3e9, job->m_nTransactionId, 0x4b0, 0x656e);
    }
}

CAPTCHA::ANSWER_CONFIRM_RESULT CCaptchaManager::ConfirmAnswer(const std::int64_t transactionId, const int answer) {
    // Current L2Server 0x0042e648 consumes the job even for a wrong answer.
    L2_GUARD_FUNCTION();
    CSPointer<CCaptchaJob> job = PopCaptchaJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Invalid Captcha Job - TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x29f,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Invalid Captcha Job - TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x29f,
        //     transactionId)
        L2_LOG_WARN(
            Log, "CAPTCHA confirmation rejected for transaction %lld: job not found.", static_cast<long long>(transactionId)
        );
        return CAPTCHA::ACR_INVALID;
    }
    return job->m_nAnswer == answer ? CAPTCHA::ACR_SUCCESS : CAPTCHA::ACR_FAIL;
}

bool CCaptchaManager::SetAnswerAndSendImageToClient(
    const std::int64_t transactionId, const char16_t* answer, const std::int16_t imageSize, const std::uint8_t* image
) {
    // Current L2Server 0x0042d9fc.
    L2_GUARD_FUNCTION();
    CSPointer<CCaptchaJob> job = m_mapJobs.FindObject(static_cast<int>(transactionId));
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Cannot Find CaptchaJob. TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x31a,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Cannot Find CaptchaJob. TransactionId[%d]",
        //     L"Captcha.cpp",
        //     0x31a,
        //     transactionId)
        L2_LOG_WARN(
            Log, "Cannot deliver CAPTCHA image for transaction %lld: job not found.", static_cast<long long>(transactionId)
        );
        return false;
    }

    job->SetAnswer(answer);
    User* user = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(job->m_nAccountId);
        if (account != accountMap.end() && account->second != nullptr) {
            user = account->second->GetUser();
        }
    }

    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Cannot Find User. TransactionId[%d] / AccountId[%d]",
        //     L"Captcha.cpp",
        //     0x312,
        //     transactionId,
        //     job->m_nAccountId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Cannot Find User. TransactionId[%d] / AccountId[%d]",
        //     L"Captcha.cpp",
        //     0x312,
        //     transactionId,
        //     job->m_nAccountId)
        L2_LOG_WARN(
            Log, "Cannot deliver CAPTCHA transaction %lld to account %u: player unavailable; attempting job removal.",
            static_cast<long long>(transactionId), static_cast<unsigned>(job->m_nAccountId)
        );
        PopCaptchaJob(transactionId);
        return false;
    }

    user->StartCaptchaConfirm(transactionId, job->m_jobType);
    const int confirmTimes = user->m_CaptchaConfirmTimes;
    const int remainingSeconds =
        runtime::SubtractInt32Wrapping(g_CaptchaSetting.confirmExpiredSeconds, user->GetCaptchaConfirmSec());
    CUserSocket* const socket = user->GetSocket();
    if (remainingSeconds < 1) {
        socket->Send("chccdh", 0x62, SystemMessageId::ILLEGAL_PROGRAM_VERIFICATION_FAILURE_PENALTY, 1, 4, 0xd6f7, 1);
        user->ActivateCaptchaDebuff();
        PopCaptchaJob(transactionId);
        user->EndCaptchaConfirm();
        user->GetSocket()->Send("chc", 0xfe, 0x18c, 0);
    } else {
        socket->Send(
            "chQcdb", 0xfe, 0x18b, transactionId, runtime::SubtractInt32Wrapping(3, confirmTimes), remainingSeconds,
            static_cast<int>(imageSize), image
        );
    }
    return true;
}
