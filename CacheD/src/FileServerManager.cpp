#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "FileServerManager.h"

#include "CacheSocket.h"
#include "Pledge.h"
#include "PledgeDb.h"
#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Log.h"
#include "Shared/src/net/Config.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <regex>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

// These four buffers are separate shipping process globals. Their original
// identifiers are absent. Keeping them file-local keeps FileServerManager
// ownership without adding them to the general CacheD configuration model.
std::string g_fileServerAddress;
std::string g_fileServerPort;
std::string g_fileServerChannel;
std::string g_fileServerAuthCode;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

NativeSocket LoadSocket(std::intptr_t socket) { return static_cast<NativeSocket>(socket); }

std::intptr_t StoreSocket(NativeSocket socket) { return static_cast<std::intptr_t>(socket); }

void CloseNativeSocket(NativeSocket socket) {
    if (socket != kInvalidSocket) {
        ::closesocket(socket);
    }
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

NativeSocket LoadSocket(std::intptr_t socket) { return static_cast<NativeSocket>(socket); }

std::intptr_t StoreSocket(NativeSocket socket) { return static_cast<std::intptr_t>(socket); }

void CloseNativeSocket(NativeSocket socket) {
    if (socket != kInvalidSocket) {
        ::close(socket);
    }
}
#endif

bool IsOpen(std::intptr_t socket) { return LoadSocket(socket) != kInvalidSocket; }

// void PrintFileServerSocketError(const char* message) { std::cout << message << std::endl }
int FileServerNativeError() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

const char* FileServerNativeErrorDomain() noexcept {
#if defined(_WIN32)
    return "winsock";
#else
    return "errno";
#endif
}

void PrintFileServerSocketError(
    const char* operation, const char* reason, const char* errorDomain, int error,
    std::intptr_t socket, const char* action) noexcept {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedLastError = ::GetLastError();
    const int savedSocketError = ::WSAGetLastError();
#endif
    char message[768]{};
    // std::snprintf(message, sizeof(message),
    //     "component=file_server event=transport_failed operation=%s reason=%s host_preview=\"%.253s\" port=%u socket=%lld error_domain=%s code=%d action=%s",
    //     operation, reason, g_fileServerAddress.c_str(),
    //     static_cast<unsigned int>(static_cast<unsigned short>(std::atoi(g_fileServerPort.c_str()))),
    //     static_cast<long long>(socket), errorDomain, error, action)
    // std::snprintf(message, sizeof(message),
    //     "[file server] %s failed: %s (host preview \"%.253s\", port %u, socket %lld, %s error %d). %s",
    //     operation, reason, g_fileServerAddress.c_str(),
    //     static_cast<unsigned int>(static_cast<unsigned short>(std::atoi(g_fileServerPort.c_str()))),
    //     static_cast<long long>(socket), errorDomain, error, action)
    if (std::strcmp(errorDomain, "none") == 0) {
        std::snprintf(message, sizeof(message),
            "[file server] %s failed: %s (host preview \"%.253s\", port %u, socket %lld). %s",
            operation, reason, g_fileServerAddress.c_str(),
            static_cast<unsigned int>(static_cast<unsigned short>(std::atoi(g_fileServerPort.c_str()))),
            static_cast<long long>(socket), action);
    } else {
        std::snprintf(message, sizeof(message),
            "[file server] %s failed: %s (host preview \"%.253s\", port %u, socket %lld, %s error %d). %s",
            operation, reason, g_fileServerAddress.c_str(),
            static_cast<unsigned int>(static_cast<unsigned short>(std::atoi(g_fileServerPort.c_str()))),
            static_cast<long long>(socket), errorDomain, error, action);
    }
    try {
        // Log.Add(LOG_WARN, "%s", message)
        L2_LOG_WARN(Log, "%s", message);
    } catch (...) {
        // In particular, logging must not throw from FileServerSocket's destructor.
        // runtime::logging::Write("cached", runtime::logging::Level::Warning, message)
        runtime::logging::Write("cached", runtime::logging::Level::Warning, message, __FILE__);
    }
    errno = savedErrno;
#if defined(_WIN32)
    ::WSASetLastError(savedSocketError);
    ::SetLastError(savedLastError);
#endif
}

std::string TargetConfigValue(const runtime::Config& config, const char* key, std::size_t targetCapacity) {
    std::string value = config.Get("FileServer", key);
    if (value.size() >= targetCapacity) {
        value.resize(targetCapacity - 1);
    }
    return value;
}

std::u16string AuditDecimal(std::int64_t value) { return Utf8ToUtf16(std::to_string(value)); }

void WritePledgeEmblemAudit(const CUser& user, const std::array<char16_t, 24>& pledgeName, int pledgeEmblemId) {
    // Current TimerExpired 0x004d4db0 emits the sparse direct route-6 record
    // below instead of calling CLogFormatter with numeric zero placeholders.
    std::array<std::u16string, 26> fields{};
    fields[0] = AuditDecimal(0x11e);
    fields[1] = AuditDecimal(user.GetId());
    fields[2] = AuditDecimal(user.GetAccountID());
    fields[5] = AuditDecimal(user.GetX());
    fields[6] = AuditDecimal(user.GetY());
    fields[7] = AuditDecimal(user.GetZ());
    // std::array iterators are not pointers on every standard library.
    const auto pledgeNameEnd = std::find(pledgeName.begin(), pledgeName.end(), u'\0');
    fields[8].assign(pledgeName.data(), static_cast<std::size_t>(pledgeNameEnd - pledgeName.begin()));
    fields[11] = AuditDecimal(user.GetRace());
    fields[12] = AuditDecimal(user.GetGender());
    fields[13] = AuditDecimal(user.GetClass());
    fields[14] = AuditDecimal(user.GetLevel());
    fields[15] = AuditDecimal(pledgeEmblemId);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

} // namespace

FileServerManager g_fileServerManager;

FileServerSocket::FileServerSocket() {
    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, 0);
    m_socket = StoreSocket(socket);
    if (socket == kInvalidSocket) {
        // PrintFileServerSocketError("socket create Error!")
        // PrintFileServerSocketError("socket_create", "native_call_failed", FileServerNativeErrorDomain(), FileServerNativeError(), m_socket, "return_disconnected")
        PrintFileServerSocketError("Socket creation", "native call failed", FileServerNativeErrorDomain(), FileServerNativeError(), m_socket, "Returning without a connection.");
        return;
    }

#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    const int enabled = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif

    hostent* const resolved = ::gethostbyname(g_fileServerAddress.c_str());
    if (!resolved || !resolved->h_addr_list || !resolved->h_addr_list[0]) {
        // PrintFileServerSocketError("gethostbyname Error!")
        // PrintFileServerSocketError("resolve_host", resolved ? "missing_ipv4_address" : "lookup_failed",
        // #if defined(_WIN32)
        //     resolved ? "none" : "winsock", resolved ? 0 : ::WSAGetLastError(),
        // #else
        //     resolved ? "none" : "h_errno", resolved ? 0 : h_errno,
        // #endif
        //     m_socket, "return_disconnected")
        PrintFileServerSocketError("Host lookup", resolved ? "IPv4 address missing" : "lookup failed",
#if defined(_WIN32)
            resolved ? "none" : "winsock", resolved ? 0 : ::WSAGetLastError(),
#else
            resolved ? "none" : "h_errno", resolved ? 0 : h_errno,
#endif
            m_socket, "Returning without a connection.");
        return;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(static_cast<unsigned short>(std::atoi(g_fileServerPort.c_str())));
    std::memcpy(&remote.sin_addr.s_addr, resolved->h_addr_list[0], sizeof(remote.sin_addr.s_addr));
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == -1) {
        // PrintFileServerSocketError("connect Error!")
        // PrintFileServerSocketError("connect", "native_call_failed", FileServerNativeErrorDomain(), FileServerNativeError(), m_socket, "return_disconnected")
        PrintFileServerSocketError("Connection", "native call failed", FileServerNativeErrorDomain(), FileServerNativeError(), m_socket, "Returning without a connection.");
        return;
    }
    m_connected = true;
}

FileServerSocket::~FileServerSocket() {
    if (!IsOpen(m_socket))
        return;
    if (::shutdown(LoadSocket(m_socket), 2) != 0) {
        // PrintFileServerSocketError("shutdown Error!")
        // Noise: shutdown of a socket that never connected follows an already reported setup failure.
        if (m_connected) {
            // PrintFileServerSocketError("shutdown", "native_call_failed", FileServerNativeErrorDomain(), FileServerNativeError(), m_socket, "close_socket")
            PrintFileServerSocketError("Shutdown", "native call failed", FileServerNativeErrorDomain(), FileServerNativeError(), m_socket, "Closing the socket.");
        }
    }
    CloseNativeSocket(LoadSocket(m_socket));
    m_socket = -1;
}

void FileServerSocket::SetEmblem(char* emblem, int size) {
    L2_GUARD_FUNCTION();

    m_emblem.append("------Lineage2ServerBoundary\r\n");
    m_emblem.append("Content-Disposition: form-data; name=\"channel\"\r\n\r\n");
    m_emblem.append(g_fileServerChannel);
    m_emblem.append("\r\n------Lineage2ServerBoundary\r\n");
    m_emblem.append("Content-Disposition: form-data; name=\"authcode\"\r\n\r\n");
    m_emblem.append(g_fileServerAuthCode);
    m_emblem.append("\r\n------Lineage2ServerBoundary\r\n");
    m_emblem.append("Content-Disposition: form-data; name=\"responsetype\"\r\n\r\n");
    m_emblem.append("xml");
    m_emblem.append("\r\n------Lineage2ServerBoundary\r\n");
    m_emblem.append(
        "Content-Disposition: form-data; name=\"attachment\"; "
        "filename=\"emblem.dds\"\r\n"
    );
    m_emblem.append("Content-Type: application/octet-stream\r\n\r\n");
    if (emblem && size > 0) {
        m_emblem.append(emblem, static_cast<std::size_t>(size));
    }
    m_emblem.append("\r\n------Lineage2ServerBoundary--");
}

void FileServerSocket::SetHeader() {
    L2_GUARD_FUNCTION();

    m_header.append("POST /admin/instantupload HTTP/1.1\r\n");
    m_header.append("Host: ");
    m_header.append(g_fileServerAddress);
    m_header.push_back(':');
    m_header.append(g_fileServerPort);
    m_header.append("\r\nConnection: close\r\nContent-Length: ");
    m_header.append(std::to_string(m_emblem.size()));
    m_header.append(
        "\r\nContent-Type: multipart/form-data; boundary="
        "----Lineage2ServerBoundary\r\n\r\n"
    );
}

void FileServerSocket::GetResponse(std::string& response) {
    L2_GUARD_FUNCTION();

    for (;;) {
        std::array<char, 0x400> buffer{};
        int received = -1;
        if (IsOpen(m_socket)) {
#if defined(_WIN32)
            received = ::recv(LoadSocket(m_socket), buffer.data(), 0x3ff, 0);
#else
            received = static_cast<int>(::recv(LoadSocket(m_socket), buffer.data(), 0x3ff, 0));
#endif
        }

        const int diagnosticReceiveError = received == -1 && IsOpen(m_socket) ? FileServerNativeError() : 0;
        // Noise: raw HTTP response chunks can expose credentials or signed URLs and bypass the operator logger.
        // std::cout << buffer.data()
        response.append(buffer.data(), std::strlen(buffer.data()));
        // if (received == 0)
        //     return.
        if (received == 0) {
            // Log.Add(LOG_DEBUG,
            //     "component=file_server event=response_read_finished accumulated_bytes=%zu result=eof http_status=unvalidated body=redacted",
            //     response.size())
            // Log.Add(LOG_DEBUG,
            //     "[file server] Response stream ended with %zu accumulated bytes. HTTP status has not been validated. Body omitted.",
            //     response.size())
            L2_LOG_DEBUG(Log,
                "[file server] Response stream ended with %zu accumulated bytes. HTTP status has not been validated; body omitted.",
                response.size());
            return;
        }
        if (received == -1) {
            // PrintFileServerSocketError("Recv Error!")
            // PrintFileServerSocketError("receive", IsOpen(m_socket) ? "native_call_failed" : "socket_not_open",
            //     IsOpen(m_socket) ? FileServerNativeErrorDomain() : "none", diagnosticReceiveError,
            //     m_socket, "return_accumulated_response")
            PrintFileServerSocketError("Receive", IsOpen(m_socket) ? "native call failed" : "socket is not open",
                IsOpen(m_socket) ? FileServerNativeErrorDomain() : "none", diagnosticReceiveError,
                m_socket, "Returning the accumulated response.");
            return;
        }
    }
}

void FileServerSocket::Send() {
    L2_GUARD_FUNCTION();
    // if (!IsOpen(m_socket))
    //     return.
    if (!IsOpen(m_socket)) {
        PrintFileServerSocketError("Send", "socket is not open", "none", 0,
            m_socket, "Request was not sent.");
        return;
    }

    const std::string packet = m_header + m_emblem;
    const int length = static_cast<int>(std::min<std::size_t>(packet.size(), static_cast<std::size_t>(INT_MAX)));
#if defined(_WIN32)
    // ::send(LoadSocket(m_socket), packet.data(), length, 0)
    const auto diagnosticSentBytes = ::send(LoadSocket(m_socket), packet.data(), length, 0);
#else
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    // ::send(LoadSocket(m_socket), packet.data(), static_cast<std::size_t>(length), flags)
    const auto diagnosticSentBytes = ::send(LoadSocket(m_socket), packet.data(), static_cast<std::size_t>(length), flags);
#endif
    if (diagnosticSentBytes != length) {
        const int diagnosticSendError = diagnosticSentBytes < 0 ? FileServerNativeError() : 0;
        // Log.Add(LOG_WARN,
        //     "component=file_server event=send_incomplete socket=%lld requested_bytes=%d sent_bytes=%lld reason=%s error_domain=%s code=%d action=return_without_retry payload=redacted",
        //     static_cast<long long>(m_socket), length, static_cast<long long>(diagnosticSentBytes),
        //     diagnosticSentBytes < 0 ? "native_call_failed" : "short_write",
        //     diagnosticSentBytes < 0 ? FileServerNativeErrorDomain() : "none", diagnosticSendError)
        // Log.Add(LOG_WARN,
        //     "[file server] Incomplete send on socket %lld: requested %d bytes, send returned %lld. %s (%s error %d). Returning without retry.",
        //     static_cast<long long>(m_socket), length, static_cast<long long>(diagnosticSentBytes),
        //     diagnosticSentBytes < 0 ? "native call failed" : "short write",
        //     diagnosticSentBytes < 0 ? FileServerNativeErrorDomain() : "none", diagnosticSendError)
        // L2_LOG_WARN(Log,
        //     "[file server] Incomplete send on socket %lld: requested %d bytes, send returned %lld. %s (%s error %d). Returning without retry.",
        //     static_cast<long long>(m_socket), length, static_cast<long long>(diagnosticSentBytes),
        //     diagnosticSentBytes < 0 ? "native call failed" : "short write",
        //     diagnosticSentBytes < 0 ? FileServerNativeErrorDomain() : "none", diagnosticSendError)
        if (diagnosticSentBytes < 0) {
            L2_LOG_WARN(Log,
                "[file server] Send failed on socket %lld: requested %d bytes, %s error %d. Returning without retry.",
                static_cast<long long>(m_socket), length,
                FileServerNativeErrorDomain(), diagnosticSendError);
        } else {
            L2_LOG_WARN(Log,
                "[file server] Incomplete send on socket %lld: sent %lld of %d bytes. Remaining bytes will not be retried.",
                static_cast<long long>(m_socket), static_cast<long long>(diagnosticSentBytes), length);
        }
    }
}

bool FileServerSocket::GetDownloadURL(std::string& response, std::string& downloadUrl) {
    L2_GUARD_FUNCTION();

    const std::regex expression("<url>(.*)</url>");
    std::smatch match;
    if (!std::regex_search(response, match, expression)) {
        return false;
    }
    downloadUrl = match[1].str();
    return true;
}

FileServerManager::FileServerManager() = default;

bool FileServerManager::TestConnection() {
    L2_GUARD_FUNCTION();
    FileServerSocket socket;
    return socket.m_connected;
}

bool FileServerManager::Initialize() {
    L2_GUARD_FUNCTION();

    runtime::Config config;
    // config.Load(runtime::ConfigFile("./l2cache.ini").string(), false)
    const std::string configPath = runtime::ConfigFile("./l2cache.ini").string();
    config.Load(configPath, false);
    g_fileServerAddress = TargetConfigValue(config, "Server", 0x400);
    if (g_fileServerAddress.empty()) {
        // Log.AddT(LOG_ERROR, L"[FileServer] Server Field is Empty!")
        // Log.Add(
        //     LOG_WARN,
        //     "component=file_server event=configuration_rejected section=FileServer field=Server reason=empty_value ready_flag_retained=%s action=return_failure",
        //     m_ready ? "true" : "false")
        // Log.Add(
        //     LOG_WARN,
        //     "[file server] [FileServer] Server is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // L2_LOG_WARN(Log,
        //     "[file server] [FileServer] Server is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // Previous: L2_LOG_WARN(Log, "File server initialization failed: [FileServer] Server is empty in %s. Previous readiness flag remains %s.", configPath.c_str(), m_ready ? "set" : "clear")
        L2_LOG_WARN(Log,
            "[file server] Initialization failed: [FileServer] Server is empty in %s. Previous readiness flag remains %s.",
            configPath.c_str(), m_ready ? "set" : "clear");
        return false;
    }

    g_fileServerPort = TargetConfigValue(config, "Port", 8);
    if (g_fileServerPort.empty()) {
        // Log.AddT(LOG_ERROR, L"[FileServer] Port Field is Empty!")
        // Log.Add(
        //     LOG_WARN,
        //     "component=file_server event=configuration_rejected section=FileServer field=Port reason=empty_value ready_flag_retained=%s action=return_failure",
        //     m_ready ? "true" : "false")
        // Log.Add(
        //     LOG_WARN,
        //     "[file server] [FileServer] Port is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // L2_LOG_WARN(Log,
        //     "[file server] [FileServer] Port is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // Previous: L2_LOG_WARN(Log, "File server initialization failed: [FileServer] Port is empty in %s. Previous readiness flag remains %s.", configPath.c_str(), m_ready ? "set" : "clear")
        L2_LOG_WARN(Log,
            "[file server] Initialization failed: [FileServer] Port is empty in %s. Previous readiness flag remains %s.",
            configPath.c_str(), m_ready ? "set" : "clear");
        return false;
    }

    g_fileServerChannel = TargetConfigValue(config, "Channel", 0x400);
    if (g_fileServerChannel.empty()) {
        // Log.AddT(LOG_ERROR, L"[FileServer] Channel Field is Empty!")
        // Log.Add(
        //     LOG_WARN,
        //     "component=file_server event=configuration_rejected section=FileServer field=Channel reason=empty_value ready_flag_retained=%s action=return_failure",
        //     m_ready ? "true" : "false")
        // Log.Add(
        //     LOG_WARN,
        //     "[file server] [FileServer] Channel is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // L2_LOG_WARN(Log,
        //     "[file server] [FileServer] Channel is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // Previous: L2_LOG_WARN(Log, "File server initialization failed: [FileServer] Channel is empty in %s. Previous readiness flag remains %s.", configPath.c_str(), m_ready ? "set" : "clear")
        L2_LOG_WARN(Log,
            "[file server] Initialization failed: [FileServer] Channel is empty in %s. Previous readiness flag remains %s.",
            configPath.c_str(), m_ready ? "set" : "clear");
        return false;
    }

    g_fileServerAuthCode = TargetConfigValue(config, "AuthCode", 0x400);
    if (g_fileServerAuthCode.empty()) {
        // Log.AddT(LOG_ERROR, L"[FileServer] AuthCode Field is Empty!")
        // Log.Add(
        //     LOG_WARN,
        //     "component=file_server event=configuration_rejected section=FileServer field=AuthCode reason=empty_value ready_flag_retained=%s action=return_failure",
        //     m_ready ? "true" : "false")
        // Log.Add(
        //     LOG_WARN,
        //     "[file server] [FileServer] AuthCode is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // L2_LOG_WARN(Log,
        //     "[file server] [FileServer] AuthCode is empty. Initialization failed. Previous readiness remains %s.",
        //     m_ready ? "ready" : "unavailable")
        // Previous: L2_LOG_WARN(Log, "File server initialization failed: [FileServer] AuthCode is empty in %s. Previous readiness flag remains %s.", configPath.c_str(), m_ready ? "set" : "clear")
        L2_LOG_WARN(Log,
            "[file server] Initialization failed: [FileServer] AuthCode is empty in %s. Previous readiness flag remains %s.",
            configPath.c_str(), m_ready ? "set" : "clear");
        return false;
    }

    m_ready = TestConnection();
    // Log.Add(m_ready ? LOG_NORMAL : LOG_WARN,
    //     "component=file_server event=connection_check_finished connected=%s readiness=%s authentication=unchecked worker_start=caller_owned",
    //     m_ready ? "true" : "false", m_ready ? "ready" : "unavailable")
    // Log.Add(m_ready ? LOG_NORMAL : LOG_WARN,
    //     "[file server] TCP connection check %s. Service marked %s. Authentication is unchecked. The caller starts the worker.",
    //     m_ready ? "succeeded" : "failed", m_ready ? "ready" : "unavailable")
    L2_LOG(Log, m_ready ? LOG_NORMAL : LOG_WARN,
        "[file server] TCP connection check %s; service marked %s. Authentication is unchecked; the caller starts the worker.",
        m_ready ? "succeeded" : "failed", m_ready ? "ready" : "unavailable");
    return m_ready;
}

bool FileServerManager::RequestRegistEmblem(char* emblem, int size, int userSocketId, int pledgeId, int pledgeSocketId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);

    if (size < 0 || static_cast<std::size_t>(size) > 0x10080U || (size != 0 && emblem == nullptr)) {
        // Log.Add(LOG_WARN,
        //     "component=file_server event=emblem_registration_rejected pledge_id=%d user_socket_id=%d bytes=%d max_bytes=65664 buffer_present=%s action=return_failure",
        //     pledgeId, userSocketId, size, emblem ? "true" : "false")
        // Log.Add(LOG_WARN,
        //     "[file server] Emblem rejected for pledge %d, user socket %d: %d bytes (limit 65664), buffer %s.",
        //     pledgeId, userSocketId, size, emblem ? "present" : "missing")
        L2_LOG_WARN(Log,
            "[file server] Emblem rejected for pledge %d, user socket %d: %d bytes (limit 65664), buffer %s.",
            pledgeId, userSocketId, size, emblem ? "present" : "missing");
        return false;
    }

    PendingRegistration* const registration = new (std::nothrow) PendingRegistration{};
    // if (!registration)
    //     return false
    if (!registration) {
        // Log.Add(LOG_ERROR,
        //     "component=file_server event=emblem_registration_rejected pledge_id=%d user_socket_id=%d reason=allocation_failed requested_bytes=%zu action=return_failure",
        //     pledgeId, userSocketId, sizeof(PendingRegistration))
        // Log.Add(LOG_ERROR,
        //     "[file server] Emblem rejected for pledge %d, user socket %d: could not allocate %zu bytes for the request.",
        //     pledgeId, userSocketId, sizeof(PendingRegistration))
        L2_LOG_ERROR(Log,
            "[file server] Emblem rejected for pledge %d, user socket %d: could not allocate %zu bytes for the request.",
            pledgeId, userSocketId, sizeof(PendingRegistration));
        return false;
    }

    registration->size = size;
    registration->userSocketId = userSocketId;
    registration->pledgeId = pledgeId;
    registration->pledgeSocketId = pledgeSocketId;
    if (size != 0) {
        std::memcpy(registration->emblem.data(), emblem, static_cast<std::size_t>(size));
    }
    m_requests.push_back(registration);
    return true;
}

void FileServerManager::TimerExpired() {
    L2_GUARD_FUNCTION();

    PendingRegistration* pending = nullptr;
    {
        L2_GUARD(m_lock);
        if (!m_requests.empty()) {
            pending = m_requests.front();
            m_requests.pop_front();
        }
    }
    if (!pending)
        return;
    std::unique_ptr<PendingRegistration> registration(pending);

    DBConn connection(runtime::process::CurrentDatabaseId());
    FileServerSocket socket;
    std::string response;
    std::string downloadUrl;
    socket.SetEmblem(registration->emblem.data(), registration->size);
    socket.SetHeader();
    socket.Send();
    socket.GetResponse(response);
    if (!socket.GetDownloadURL(response, downloadUrl)) {
        // Log.Add(LOG_WARN,
        //     "component=file_server event=emblem_registration_failed stage=response_url pledge_id=%d user_socket_id=%d response_bytes=%zu reason=url_not_found action=discard_request retry=none response=redacted",
        //     registration->pledgeId, registration->userSocketId, response.size())
        // Log.Add(LOG_WARN,
        //     "[file server] Emblem registration failed for pledge %d, user socket %d: no URL in the %zu-byte response. Discarding the request without retry.",
        //     registration->pledgeId, registration->userSocketId, response.size())
        L2_LOG_WARN(Log,
            "[file server] Emblem registration failed for pledge %d, user socket %d: no URL in the %zu-byte response. Discarding the request without retry.",
            registration->pledgeId, registration->userSocketId, response.size());
        return;
    }

    int pledgeEmblemId = 0;
    connection.Bind(&pledgeEmblemId);
    constexpr const wchar_t* statement = L"{Call lin_SetPledgeNewEmblem (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&registration->pledgeId);
    connection.BindParameter(downloadUrl.c_str());
    connection.ExecutePrepared(statement);
    if (!connection.Fetch() || pledgeEmblemId == 0) {
        // Log.Add(LOG_ERROR,
        //     "component=file_server event=emblem_registration_failed stage=persistence pledge_id=%d user_socket_id=%d emblem_id=%d reason=fetch_failed_or_zero_id action=discard_request retry=none url=redacted",
        //     registration->pledgeId, registration->userSocketId, pledgeEmblemId)
        // Log.Add(LOG_ERROR,
        //     "[file server] Emblem registration failed for pledge %d, user socket %d: database fetch failed or returned zero (emblem ID %d). Discarding the request without retry.",
        //     registration->pledgeId, registration->userSocketId, pledgeEmblemId)
        L2_LOG_ERROR(Log,
            "[file server] Emblem registration failed for pledge %d, user socket %d: database fetch failed or returned zero (emblem ID %d). Discarding the request without retry.",
            registration->pledgeId, registration->userSocketId, pledgeEmblemId);
        return;
    }

    CSocket* const activeSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (activeSocket) {
        activeSocket->Send("chddd", 0xc6, 0x16, registration->userSocketId, registration->pledgeSocketId, pledgeEmblemId);
    }

    // Log.Add(activeSocket ? LOG_DEBUG : LOG_WARN,
    //     "component=file_server event=emblem_registered pledge_id=%d user_socket_id=%d emblem_id=%d reply=%s delivery=unconfirmed url=redacted",
    //     registration->pledgeId, registration->userSocketId, pledgeEmblemId,
    //     activeSocket ? "send_requested" : "skipped_no_active_world_socket")
    // Log.Add(activeSocket ? LOG_DEBUG : LOG_WARN,
    //     "[file server] Emblem registered for pledge %d, user socket %d (emblem ID %d). Reply %s. Delivery is unconfirmed.",
    //     registration->pledgeId, registration->userSocketId, pledgeEmblemId,
    //     activeSocket ? "send requested" : "skipped because no world socket is active")
    L2_LOG(Log, activeSocket ? LOG_DEBUG : LOG_WARN,
        "[file server] Emblem registered for pledge %d, user socket %d (emblem ID %d); reply %s. Delivery is unconfirmed.",
        registration->pledgeId, registration->userSocketId, pledgeEmblemId,
        activeSocket ? "send requested" : "skipped because no world socket is active");

    std::array<char16_t, 24> pledgeName{};
    std::uint32_t rulerId = 0;
    CPledgeSP pledge = g_pledgeDb.GetPledge(registration->pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        L2_READ_GUARD(pledge->m_pledgeLock);
        std::copy_n(pledge->m_name, pledgeName.size(), pledgeName.begin());
        rulerId = pledge->m_rulerId;
    }

    CUserSP user = g_userDb.GetUser(static_cast<int>(rulerId), true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        WritePledgeEmblemAudit(*user.get(), pledgeName, pledgeEmblemId);
    }
}
