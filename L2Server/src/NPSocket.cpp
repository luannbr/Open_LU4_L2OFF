#include "NPSocket.h"
#include "Shared/src/MemorySub.h"

#include "CConfig.h"
#include "NPSystem.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {


// Current NPSocket.cpp anonymous 0x400-byte scratch at 0x01a57720.
// Noise: The raw response trace is disabled. Its shared diagnostic scratch is unused.
// char g_DebugNPLoginBuffer[0x400]{}

int CurrentLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

[[noreturn]] void FailNPSocketOnRead(const std::source_location location = std::source_location::current()) {
    runtime::process::FailCheckSizeInt(location);
}

void AppendPacketHistory(char*& destination, std::uint64_t& remaining, int transactionId, int command) {
    const int written = std::snprintf(
        destination, static_cast<std::size_t>(remaining),
        // "(trid=%d, cmd=%d) ",
        "transaction %d / command %d; ", transactionId, command
    );
    if (written < 0 || static_cast<std::uint64_t>(written) >= remaining) {
        destination += remaining - 1;
        remaining = 1;
        *destination = '\0';
        return;
    }
    destination += written;
    remaining -= static_cast<std::uint64_t>(written);
}

} // namespace

CNPPacket::CNPPacket() : m_Request{}, m_pResponse(nullptr), m_pNPFunc(nullptr) {}

void CNPPacket::OnIOCallback(int, std::uint32_t, PortableOverlapped*) {
    g_Timer.BeginJob(JOB_NPSOCKET__TOTAL);
    const bool closeRequested = m_pNPFunc(static_cast<NPSystem::CNPSocket*>(m_pSocket), &m_Request, m_pResponse);
    if (closeRequested) {
        m_pSocket->Close();
    }
    L2_RELEASE(m_pResponse, ObjectRefType::CreateDelete);
    L2_RELEASE(m_pSocket, ObjectRefType::IoRetain);
    CAbstractPacket::g_nPendingPacket.fetch_sub(1, std::memory_order_seq_cst);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
    g_Timer.EndJob(JOB_NPSOCKET__TOTAL);
}

namespace NPSystem {

L2_DEFINE_OBJECT_CLASS_ID(CNPSocket)

L2_DEFINE_OBJECT_CLASS_NAME(CNPSocket, m_pClassName_CNPSocket)

L2_DEFINE_OBJECT_NEW(CNPSocket)

L2_DEFINE_OBJECT_ALLOCATOR(CNPSocket, m_pClassName_CNPSocket)

L2_DEFINE_OBJECT_ADD_REF(CNPSocket)

L2_DEFINE_OBJECT_RELEASE(CNPSocket)

CNPSocket::CNPSocket(std::uint64_t socket) : CIOSocketEx<IOBuffer32k>(socket, MAX_EXECUTION_QUEUE) {
    L2_GUARD_FUNCTION();
    Clear();
}

CNPSocket::~CNPSocket() { Clear(); }

void CNPSocket::Clear() {
    m_nMode = SM_READ_LEN;
    m_nPacketLength = 0;
    if (m_pResponse) {
        L2_RELEASE(m_pResponse, ObjectRefType::CreateDelete);
        m_pResponse = nullptr;
    }
    m_nLastCommandIndex = -1;
    std::memset(m_nLastCommands, 0, sizeof(m_nLastCommands));
    std::memset(m_nLastTRIDs, 0, sizeof(m_nLastTRIDs));
}

void CNPSocket::SendBuffer(int command, int transactionId, IOBuffer32k* buffer) {
    L2_GUARD_FUNCTION();
    if (m_nMode == SM_CLOSE) {
        return;
    }
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid buffer(pBuffer)",
        //     L"NPSocket.cpp",
        //     0x3c)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot send command %d (transaction %d): the outgoing buffer is null.",
        //     command, transactionId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot send command %d (transaction %d): the outgoing buffer is null.", command, transactionId
        );
        return;
    }

    const std::int32_t historyIndex =
        runtime::raw_atomic::IncrementI32(&m_nLastCommandIndex) & (MAX_DEBUG_PACKET_COUNT - 1);
    m_nLastCommands[historyIndex] = command;
    m_nLastTRIDs[historyIndex] = transactionId;
    Write(buffer);
}

void CNPSocket::GeneratePacketHistory(char* buffer, std::uint64_t bufferSize) {
    L2_GUARD_FUNCTION();
    if (!buffer || bufferSize == 0) {
        return;
    }

    char* destination = buffer;
    std::uint64_t remaining = bufferSize;
    for (int index = 0; index < MAX_DEBUG_PACKET_COUNT && remaining != 0; ++index) {
        AppendPacketHistory(destination, remaining, m_nLastTRIDs[index], m_nLastCommands[index]);
    }
}

bool CNPSocket::CheckPendingWrite() {
    L2_GUARD_FUNCTION();
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid np socket.",
        //     L"NPSocket.cpp",
        //     0x6d)
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot write to a closed socket, rejecting the pending write.")
        L2_LOG_WARN(Log, "[NP] Cannot write to a closed socket; rejecting the pending write.");
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"NP Socket Pending Write > %d KB, Close!",
    //     0x2000)
    // Log.Add(LOG_ERROR,
    //     "[NP] Pending writes exceeded 8388608 bytes (8 MiB), rejecting the write and closing the socket.")
    L2_LOG_ERROR(Log, "[NP] Pending writes exceeded 8388608 bytes (8 MiB); rejecting the write and closing the socket.");
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return false;
}

void CNPSocket::OnRead() {
    L2_GUARD_FUNCTION();
    if (m_nMode == SM_CLOSE) {
        Close();
        return;
    }

    IOBuffer32k* const readBuffer = m_pReadBuf;
    char* const bufferBegin = reinterpret_cast<char*>(readBuffer->m_Buffer.data());
    char* cursor = bufferBegin;
    char* const bufferEnd = bufferBegin + readBuffer->m_dwSize;

    if (m_pResponse && m_pResponse->IsNeedResetBuffer()) {
        m_pResponse->SetNeedResetBuffer(false);
        m_pResponse->ResetBuffer(bufferBegin);
        m_pResponse->SetBuffer(bufferBegin, static_cast<std::size_t>(bufferEnd - bufferBegin));
    }

    while (cursor != bufferEnd) {
        if (!m_pResponse) {
            m_pResponse = new CSTSResponse;
            if (!m_pResponse) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(e=%d) new CSTSResponse failed",
                //     L"NPSocket.cpp",
                //     0xa1,
                //     CurrentLastError())
                // Log.Add(LOG_ERROR,
                //     "[NP] Cannot allocate a response object, terminating.")
                L2_LOG_ERROR(Log, "[NP] Cannot allocate a response object; terminating.");
                FailNPSocketOnRead();
            }
            m_pResponse->Clear();
        }

        CSTSResponse* const response = m_pResponse;
        const std::size_t available = static_cast<std::size_t>(bufferEnd - cursor);
        response->SetBuffer(cursor, available);
        if (!response->PrepareParseHeader()) {
            if (available < IOBuffer32k::MaxSize) {
                Read(static_cast<std::uint32_t>(available));
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(e=%d) packet read failed(%S)",
                //     L"NPSocket.cpp",
                //     0xb3,
                //     CurrentLastError(),
                //     cursor)
                // Log.Add(LOG_ERROR,
                //     "[NP] Cannot read a complete response header: %llu buffered bytes reached the %u-byte capacity.
                //     clearing the response and closing the socket.", static_cast<unsigned long long>(available),
                //     static_cast<unsigned>(IOBuffer32k::MaxSize))
                L2_LOG_ERROR(
                    Log,
                    "[NP] Cannot read a complete response header: %llu buffered bytes reached the %u-byte capacity; "
                    "clearing the response and closing the socket.",
                    static_cast<unsigned long long>(available), static_cast<unsigned>(IOBuffer32k::MaxSize)
                );
                response->Clear();
                Close();
            }
            return;
        }

        // Noise: Per-response payload tracing exposes authentication data and adds routine receive chatter.
        // if (g_bDebugNPLogin) {
        //     if (available <
        //         static_cast<std::size_t>(
        //             std::numeric_limits<int>::max())) {
        //         std::size_t copied = 0
        //         while (copied + 2 <
        //                    sizeof(g_DebugNPLoginBuffer) &&
        //                copied < available &&
        //                cursor[copied] != '\0') {
        //             g_DebugNPLoginBuffer[copied] =
        //                 cursor[copied].
        //             ++copied.
        //         }
        //         g_DebugNPLoginBuffer[copied] = '\0'.
        //     } else {
        //         g_DebugNPLoginBuffer[0] = '\0'.
        //     }
        //     Log.Add(
        //         LOG_ERROR,
        //         "NPSocket<< %s",
        //         g_DebugNPLoginBuffer)
        // }

        if (!response->ParseHeader()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError(e=%d) packet parse header failed(%S)",
            //     L"NPSocket.cpp",
            //     0xd2,
            //     CurrentLastError(),
            //     cursor)
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot parse the response header (%llu buffered bytes). Reporting the failure and terminating.",
            //     static_cast<unsigned long long>(available))
            L2_LOG_ERROR(
                Log, "[NP] Cannot parse the response header (%llu buffered bytes); reporting the failure and terminating.",
                static_cast<unsigned long long>(available)
            );
            // Noise: The preceding error gives the parse failure and byte count. Raw NP responses may contain authentication
            // secrets. L2Util::ReportByteArrayToHexStr(
            //     L"NPBuffer",
            //     cursor,
            //     bufferEnd)
            g_NPD.SendCriticalMailToTC(this, false, response);
            FailNPSocketOnRead();
        }

        cursor = response->m_pBufferBodyStart;
        if (response->m_nBytesToDrop > 0) {
            const std::ptrdiff_t bodyAvailable = bufferEnd - cursor;
            if (bodyAvailable < response->m_nBytesToDrop) {
                response->m_nBytesToDrop -= static_cast<int>(bodyAvailable);
                response->SetNeedResetBuffer(true);
                break;
            }
            cursor += response->m_nBytesToDrop;
            response->Clear();
            continue;
        }

        if (!response->PrepareParseBody()) {
            if (response->m_nBodyLength > IOBuffer32k::MaxSize) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(e=%d) packet body size(%d) "
                //     L"is greater than NPSocket BufferSize(%d)",
                //     L"NPSocket.cpp",
                //     0xf4,
                //     CurrentLastError(),
                //     response->m_nBodyLength,
                //     IOBuffer32k::MaxSize)
                // Log.Add(LOG_ERROR,
                //     "[NP] Response body declares %d bytes, exceeding the %u-byte receive buffer.",
                //     response->m_nBodyLength, static_cast<unsigned>(IOBuffer32k::MaxSize))
                L2_LOG_ERROR(
                    Log, "[NP] Response body declares %d bytes, exceeding the %u-byte receive buffer.",
                    response->m_nBodyLength, static_cast<unsigned>(IOBuffer32k::MaxSize)
                );
            }
            response->SetNeedResetBuffer(true);
            Read(static_cast<std::uint32_t>(bufferEnd - cursor));
            return;
        }

        cursor = response->m_pBufferBodyEnd;
        if (!response->IsParsingComplete()) {
            g_NPD.SendCriticalMailToTC(this, true, response);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError(e=%d) packet parse body failed(cmd=%d)",
            //     L"NPSocket.cpp",
            //     0x10d,
            //     CurrentLastError(),
            //     static_cast<int>(response->m_nCommand))
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot parse the response body for command %d (transaction %u). Critical report requested, clearing
            //     the response.", static_cast<int>(response->m_nCommand), static_cast<unsigned>(response->m_nTRID))
            L2_LOG_ERROR(
                Log,
                "[NP] Cannot parse the response body for command %d (transaction %u); critical report requested, clearing "
                "the response.",
                static_cast<int>(response->m_nCommand), static_cast<unsigned>(response->m_nTRID)
            );
            response->Clear();
            continue;
        }
        if (!response->m_bIsKnownCommand) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError(e=%d) Unknown Command 2.",
            //     L"NPSocket.cpp",
            //     0x115,
            //     CurrentLastError())
            // Log.Add(LOG_WARN,
            //     "[NP] Discarding a response with an unrecognized command (%d).",
            //     static_cast<int>(response->m_nCommand))
            L2_LOG_WARN(
                Log, "[NP] Discarding a response with an unrecognized command (%d).", static_cast<int>(response->m_nCommand)
            );
            response->Clear();
            continue;
        }

        if (response->m_isResponse && response->m_isTransaction) {
            m_pResponse = nullptr;
            STSRequestTI request{};
            bool requestFound = false;
            {
                L2_GUARD(g_requestTransactionMapSect);
                const auto position = g_requestTransactionMap.find(static_cast<std::int32_t>(response->m_nTRID));
                if (position != g_requestTransactionMap.end()) {
                    request = position->second;
                    g_requestTransactionMap.erase(position);
                    requestFound = true;
                }
            }
            if (!requestFound) {
                continue;
            }
            if (static_cast<unsigned int>(request.m_nCommand) >= COMMAND_MAX) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(e=%d) request.m_nCommand "
                //     L"is out of range(command=%d)",
                //     L"NPSocket.cpp",
                //     0x143,
                //     CurrentLastError(),
                //     static_cast<int>(
                //         request.m_nCommand))
                // Log.Add(LOG_ERROR,
                //     "[NP] Request command %d is outside the handler table. Terminating.",
                //     static_cast<int>(request.m_nCommand))
                L2_LOG_ERROR(
                    Log, "[NP] Request command %d is outside the handler table; terminating.",
                    static_cast<int>(request.m_nCommand)
                );
                FailNPSocketOnRead();
            }

            response->AttachReadBuffer(readBuffer);
            CNPPacket* const packet = new CNPPacket;
            if (!packet) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(e=%d) new CNPPacket() failed",
                //     L"NPSocket.cpp",
                //     0x165,
                //     CurrentLastError())
                // Log.Add(LOG_ERROR,
                //     "[NP] Cannot allocate a response dispatch object, terminating.")
                L2_LOG_ERROR(Log, "[NP] Cannot allocate a response dispatch object; terminating.");
                FailNPSocketOnRead();
            }
            packet->m_pSocket = this;
            packet->m_Request = request;
            packet->m_pResponse = response;
            packet->m_pNPFunc = g_ResponseHandler[request.m_nCommand];
            L2_ADD_REF(this, ObjectRefType::IoRetain);
            packet->PostObject(static_cast<int>(response->m_nCommand));
            continue;
        }

        m_pResponse = nullptr;
        if (static_cast<unsigned int>(response->m_nCommand) >= COMMAND_MAX) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError(e=%d) notify.m_nCommand "
            //     L"is out of range(nCommand=%d)",
            //     L"NPSocket.cpp",
            //     0x181,
            //     CurrentLastError(),
            //     static_cast<int>(
            //         response->m_nCommand))
            // Log.Add(LOG_ERROR,
            //     "[NP] Notification command %d is outside the handler table. Terminating.",
            //     static_cast<int>(response->m_nCommand))
            L2_LOG_ERROR(
                Log, "[NP] Notification command %d is outside the handler table; terminating.",
                static_cast<int>(response->m_nCommand)
            );
            FailNPSocketOnRead();
        }

        response->AttachReadBuffer(readBuffer);
        CNPPacket* const packet = new CNPPacket;
        if (!packet) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError(e=%d) new CNPPacket() failed",
            //     L"NPSocket.cpp",
            //     0x1a4,
            //     CurrentLastError())
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot allocate a notification dispatch object, terminating.")
            L2_LOG_ERROR(Log, "[NP] Cannot allocate a notification dispatch object; terminating.");
            FailNPSocketOnRead();
        }
        packet->m_pSocket = this;
        packet->m_Request = {};
        packet->m_pResponse = response;
        packet->m_pNPFunc = g_ResponseHandler[response->m_nCommand];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        packet->PostObject(static_cast<int>(response->m_nCommand));
    }
    Read(0);
}

void CNPSocket::OnClose() {
    L2_GUARD_FUNCTION();
    GuardHelper closed(L"NPSocket Closed", CThreadManager::GetThreadID());
    const int lastError = CurrentLastError();
    const int socketError = GetSocketLastError();
    // Noise: The socket/system error codes are included in the single fatal record below, with the required local copy.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d]NPError(e=%d) (e=%d)NP Socket Closed",
    //     L"NPSocket.cpp",
    //     0x1c0,
    //     socketError,
    //     lastError)
    g_NPD.SetClosed();

    if (g_nUseReconnectAuth == 0) {
        // Noise: One fatal console record keeps the mandatory local-file copy and ERROR destination.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"NP socket is closed.")
        // Log.Add(
        //     LOG_ERROR,
        //     L"NP socket is closed.")
        // runtime::log::WriteErrorWithLocalCopy(Log,
        //     "[NP] Connection closed: socket error %d, last system error %d. Terminating.",
        //     socketError, lastError)
        L2_LOG_ERROR_WITH_LOCAL_COPY(
            Log, "[NP] Connection closed: socket error %d, last system error %d; terminating.", socketError, lastError
        );
        runtime::process::FailCheckSizeInt();
    }

    // Noise: One fatal console record keeps the mandatory local-file copy and ERROR destination.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"NP socket is closed.")
    // Log.Add(
    //     LOG_ERROR,
    //     L"NP socket is closed.")
    // runtime::log::WriteErrorWithLocalCopy(Log,
    //     "[NP] Connection closed: socket error %d, last system error %d. Terminating.",
    //     socketError, lastError)
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log, "[NP] Connection closed: socket error %d, last system error %d; terminating.", socketError, lastError
    );
    runtime::process::FailCheckSizeInt();
}

} // namespace NPSystem
