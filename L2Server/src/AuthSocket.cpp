#include "Auth.h"
#include "CConfig.h"
#include "DB.h"
#include "Default.h"
#include "NPSystem.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Protocol/src/SystemMessageIds.h"
#include "QueueTicket.h"
#include "Raid.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "User.h"
#include "UserSocket.h"
#include "VipAttendance.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <strings.h>
#include <sys/socket.h>
#endif

namespace {


int CurrentLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

// Noise: imported function addresses do not identify a live AuthD connection.
// int LegacySocketSymbolForLog() noexcept {
//     // Current 0x004aae99/0x004acb66 load the imported `socket` symbol for
//     // this diagnostic argument, instead of CAuthSocket::m_socket.
//     return static_cast<int>(
//         reinterpret_cast<std::intptr_t>(&::socket))
// }

void StoreLittleEndian16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

int CompareAccountNameCaseInsensitive(const char* left, const char* right) noexcept {
#if defined(_WIN32)
    return ::_stricmp(left, right);
#else
    return ::strcasecmp(left, right);
#endif
}

template <std::size_t Extent>
void CopyNarrowString(char (&destination)[Extent], const char* source) noexcept {
    static_assert(Extent > 0);
    std::size_t index = 0;
    while (index + 1 < Extent && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

std::wstring AccountNameForLoginLog(const char* accountName) {
    std::array<SharedUtilsWideChar, 15> targetName{};
    AnsiToUnicode(accountName, 0x0e, targetName.data(), 0x0e);

    std::wstring result;
    for (const SharedUtilsWideChar character : targetName) {
        if (character == 0) {
            break;
        }
        result.push_back(static_cast<wchar_t>(static_cast<std::uint16_t>(character)));
    }
    return result;
}

} // namespace

// The dispatch diagnostic identifies the existing no-op callback before posting.
bool DummyAuthPacket(CAuthSocket*, const unsigned char*);

L2_DEFINE_OBJECT_CLASS_ID(CAuthSocket)

L2_DEFINE_OBJECT_CLASS_NAME(CAuthSocket, m_pClassName_CAuthSocket)

L2_DEFINE_OBJECT_NEW(CAuthSocket)

L2_DEFINE_OBJECT_ALLOCATOR(CAuthSocket, m_pClassName_CAuthSocket)

CAuthSocket::CAuthSocket(std::uint64_t socket, CAuthCallback* packetTable) :
    CIOSocketEx<IOBuffer8k>(socket, 0x1000),
    m_socket(socket),
    m_threadCount(0),
    m_packetTable(packetTable),
    m_mode(SM_READ_LEN),
    m_packetLength(0) {
    L2_GUARD_FUNCTION();
    if (m_packetTable == authPacketTable) {
        // Noise: TCP connection establishment is reported once by CAuthD::ConnectAuthD.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"AuthSocket open %x(%x)",
        //     static_cast<unsigned int>(m_socket),
        //     static_cast<unsigned int>(
        //         reinterpret_cast<std::uintptr_t>(this)))
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid AuthPacketTable")
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Connection %llu uses an unrecognized reply table. Continuing with the supplied table.",
        //     static_cast<unsigned long long>(m_socket))
        L2_LOG_ERROR(
            Log, "[auth] Connection %llu uses an unrecognized reply table; continuing with the supplied table.",
            static_cast<unsigned long long>(m_socket)
        );
    }
}

CAuthSocket::~CAuthSocket() { m_packetTable = nullptr; }

L2_DEFINE_OBJECT_RELEASE(CAuthSocket)

bool CAuthSocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid auth socket.",
        //     L"AuthSocket.cpp",
        //     0x103)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Rejecting a write on closed connection %llu.",
        //     static_cast<unsigned long long>(m_socket))
        L2_LOG_ERROR(Log, "[auth] Rejecting a write on closed connection %llu.", static_cast<unsigned long long>(m_socket));
        return false;
    }
    // if (m_nPendingWrite.load(std::memory_order_relaxed) <
    //     0x800001) {
    const std::int32_t pendingWrite = m_nPendingWrite.load(std::memory_order_relaxed);
    if (pendingWrite < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"authsocket pending write > %d KB, Close!",
    //     0x2000)
    // Log.Add(
    //     LOG_ERROR,
    //     "[auth] Connection %llu pending writes reached %d bytes (limit 8388608). Rejecting the write and requesting
    //     closure.", static_cast<unsigned long long>(m_socket), pendingWrite)
    L2_LOG_ERROR(
        Log,
        "[auth] Connection %llu pending writes reached %d bytes (limit 8388608); rejecting the write and requesting "
        "closure.",
        static_cast<unsigned long long>(m_socket), pendingWrite
    );
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return false;
}

void CAuthSocket::OnClose() {
    L2_GUARD_FUNCTION();
    GuardHelper closed(L"AuthSocket Closed", CThreadManager::GetThreadID());
    // Log.Add(
    //     LOG_ERROR,
    //     L"AuthSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     CurrentLastError())
    // runtime::log::WriteErrorWithLocalCopy(
    //     Log,
    //     "[auth] Connection %llu closed. %s (reconnect mode %d. Last OS error observed: %d, may not identify the cause).",
    //     static_cast<unsigned long long>(m_socket),
    //     g_nUseReconnectAuth == 0
    //         ? "L2Server is terminating because automatic reconnect is disabled"
    //         : "authentication is being marked disconnected. Automatic reconnect is enabled",
    //     g_nUseReconnectAuth, CurrentLastError())
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log,
        "[auth] Connection %llu closed; %s (reconnect mode %d; last OS error observed: %d, may not identify the cause).",
        static_cast<unsigned long long>(m_socket),
        g_nUseReconnectAuth == 0 ? "L2Server is terminating because automatic reconnect is disabled"
                                 : "authentication is being marked disconnected; automatic reconnect is enabled",
        g_nUseReconnectAuth, CurrentLastError()
    );
    authd.SetAuthClosed();
    g_bAuthConnected = false;
    // Noise: the error above already includes this mandatory local copy.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"Auth socket is closed.")
    // Noise: the error above already includes this operator/remote destination.
    // Log.Add(
    //     LOG_ERROR,
    //     L"Auth socket is closed.")
    if (g_nUseReconnectAuth == 0) {
        runtime::process::FailCheckSizeInt();
    }
}

void CAuthSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CAuthSocket::SendV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    if (m_mode == SM_CLOSE) {
        return;
    }

    IOBuffer8k* const buffer = IOBuffer8k::Alloc();
    const int bodySize = VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x1ffe, format, arguments);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     LegacySocketSymbolForLog(),
        //     format)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Connection %llu packet formatter returned 0 (body capacity 8190 bytes, format %s). Passing a
        //     zero-length buffer to the writer.", static_cast<unsigned long long>(m_socket), format ? format : "(null)")
        L2_LOG_ERROR(
            Log,
            "[auth] Connection %llu packet formatter returned 0 (body capacity 8190 bytes, format %s); passing a "
            "zero-length buffer to the writer.",
            static_cast<unsigned long long>(m_socket), format ? format : "(null)"
        );
        buffer->m_dwSize = 0;
    } else {
        const std::uint16_t frameSize = static_cast<std::uint16_t>(bodySize + 2);
        StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
        buffer->m_dwSize = frameSize;
    }
    Write(buffer);
}

void CAuthSocket::OnRead() {
    L2_GUARD_FUNCTION();
    if (m_mode == SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_mode == SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(readSize - packetOffset);
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength = static_cast<std::int32_t>(
                                 static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8U)
                             ) -
                2;
            if (m_packetLength < 1 || m_packetLength > 0x1ffe) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"authsocket %d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     m_packetLength)
                // Log.Add(
                //     LOG_ERROR,
                //     "[auth] Closing connection %llu: incoming body length %d is outside 1..8190 bytes.",
                //     static_cast<unsigned long long>(m_socket), m_packetLength)
                L2_LOG_ERROR(
                    Log, "[auth] Closing connection %llu: incoming body length %d is outside 1..8190 bytes.",
                    static_cast<unsigned long long>(m_socket), m_packetLength
                );
                m_mode = SM_CLOSE;
                Close();
                return;
            }
            m_mode = SM_READ_BODY;
            packetOffset += 2;
        } else if (m_mode != SM_READ_BODY) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Closing connection %llu: unexpected read state %d.",
            //     static_cast<unsigned long long>(m_socket), static_cast<int>(m_mode))
            L2_LOG_ERROR(
                Log, "[auth] Closing connection %llu: unexpected read state %d.", static_cast<unsigned long long>(m_socket),
                static_cast<int>(m_mode)
            );
            Close();
            return;
        }

        if (readSize < packetOffset + m_packetLength) {
            Read(readSize - packetOffset);
            return;
        }

        const std::uint8_t opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (m_packetTable == authPacketTable && opcode > 0x21) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[auth]unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Closing connection %llu: incoming primary protocol %u is outside supported IDs 0..33.",
            //     static_cast<unsigned long long>(m_socket), static_cast<unsigned>(opcode))
            L2_LOG_ERROR(
                Log, "[auth] Closing connection %llu: incoming primary protocol %u is outside supported IDs 0..33.",
                static_cast<unsigned long long>(m_socket), static_cast<unsigned>(opcode)
            );
            m_mode = SM_CLOSE;
            Close();
            return;
        }

        CAuthPacket* const packet = new CAuthPacket;
        packet->m_pSocket = this;
        m_pReadBuf->AddRef();
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_packetTable[opcode];
        if (packet->m_pFunc == &DummyAuthPacket) {
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Incoming primary protocol %u on connection %llu selects a no-op handler. The current handler
            //     performs no work.", static_cast<unsigned>(opcode), static_cast<unsigned long long>(m_socket))
            L2_LOG_WARN(
                Log,
                "[auth] Incoming primary protocol %u on connection %llu selects a no-op handler; the current handler "
                "performs no work.",
                static_cast<unsigned>(opcode), static_cast<unsigned long long>(m_socket)
            );
        }
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_packetLength;
        m_mode = SM_READ_LEN;
    }
    Read(0);
}

bool DummyAuthPacket(CAuthSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Noise: the dispatch diagnostic identifies the protocol and connection selecting this no-op.
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in CAuthSocket")
    return false;
}

bool AuthRequestAboutToPlay(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::uint32_t accountId = 0;
    int timeLeft = 0;
    int loginFlag = 0;
    int warnFlag = 0;
    std::array<char, 0x0f> accountName{};
    const unsigned char* cursor = ::Disassemble(
        packet, "dsddd", &accountId, static_cast<int>(accountName.size()), accountName.data(), &timeLeft, &loginFlag,
        &warnFlag
    );

    int stat = 0;
    cursor = ::Disassemble(cursor, "d", &stat);

    char chargeMethod = 0;
    int reservations = 0;
    cursor = ::Disassemble(cursor, "cd", &chargeMethod, &reservations);
    if (!g_bUseAuthTimeManageSystem) {
        chargeMethod = 0;
        reservations = 0;
    }

    int restTimeChina = 0;
    int playTimeChina = 0;
    cursor = ::Disassemble(cursor, "dd", &restTimeChina, &playTimeChina);

    std::array<char, 9> pcCafeCode{};
    cursor = ::Disassemble(cursor, "s", static_cast<int>(pcCafeCode.size()), pcCafeCode.data());

    std::uint32_t clientAddress = 0;
    cursor = ::Disassemble(cursor, "d", &clientAddress);

    unsigned short premiumLevel = 0;
    if (!g_ShutDownSystem) {
        premiumLevel = static_cast<unsigned short>((static_cast<std::uint32_t>(loginFlag) >> 10U) & 1U);
    } else {
        char ignoredResult1 = 0;
        int ignoredValue = 0;
        char ignoredResult2 = 0;
        cursor = ::Disassemble(cursor, "cdc", &ignoredResult1, &ignoredValue, &ignoredResult2);
        (void)ignoredResult1;
        (void)ignoredValue;
        (void)ignoredResult2;

        for (std::size_t offset = 0; offset < sizeof(authd.m_PlayTimeInfo); offset += 3) {
            cursor = ::Disassemble(cursor, "b", 3, &authd.m_PlayTimeInfo[offset]);
        }
        cursor = ::Disassemble(cursor, "h", &premiumLevel);

        short extraLength = 0;
        cursor = ::Disassemble(cursor, "h", &extraLength);
        if (extraLength > 0) {
            void* const extraData = std::malloc(static_cast<std::size_t>(extraLength));
            if (extraData != nullptr) {
                cursor = ::Disassemble(cursor, "b", static_cast<int>(extraLength), extraData);
                std::free(extraData);
            }
        }
    }

    int markingTime = 0;
    // int dormantType = 0.
    int dormantType = DormantConst::DT_NORMAL;
    if (g_ShutDownSystem && g_DormantUserSystem) {
        int lastLogoutTime = 0;
        std::uint64_t dormantBits = 0;
        ::Disassemble(cursor, "ddQ", &lastLogoutTime, &markingTime, &dormantBits);

        std::array<std::uint8_t, sizeof(dormantBits)> dormantBytes{};
        std::memcpy(dormantBytes.data(), &dormantBits, dormantBytes.size());
        dormantType = dormantBytes[0];

        if (g_bNewDormantBitset) {
            const int now = static_cast<int>(std::time(nullptr));
            const bool notDormantServer = g_bNotDormantServer || g_nWorldId > 100;
            bool activateMark = false;
            std::int64_t markValue = dormantType;

            if (lastLogoutTime == 0) {
                // dormantType = 2.
                dormantType = DormantConst::DT_NEWBIE;
                markValue = notDormantServer ? 0x102 : 2;
                activateMark = true;
            } else {
                const std::uint32_t elapsedBits =
                    static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(lastLogoutTime);
                const std::uint32_t thresholdBits = static_cast<std::uint32_t>(g_DormantUserDay) * UINT32_C(86400);
                const std::int32_t elapsed = static_cast<std::int32_t>(elapsedBits);
                const std::int32_t threshold = static_cast<std::int32_t>(thresholdBits);
                if (lastLogoutTime > 0 && elapsed > threshold) {
                    // dormantType = 3.
                    dormantType = DormantConst::DT_DORMANT;
                    markValue = notDormantServer ? 0x103 : 3;
                    activateMark = true;
                } else if (lastLogoutTime < 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Negative Number - Parameter : "
                    //     L"tLastLogout(%d)",
                    //     L"AuthSocket.cpp",
                    //     0x1a7,
                    //     lastLogoutTime)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[auth] Account %u has a negative last-logout timestamp %d in the login reply. Continuing
                    //     dormant-account evaluation.", static_cast<unsigned>(accountId), lastLogoutTime)
                    L2_LOG_ERROR(
                        Log,
                        "[auth] Account %u has a negative last-logout timestamp %d in the login reply; continuing "
                        "dormant-account evaluation.",
                        static_cast<unsigned>(accountId), lastLogoutTime
                    );
                } else if (markingTime > 0 && !notDormantServer && dormantBytes[1] == 1) {
                    markValue = dormantType;
                    activateMark = true;
                }
            }

            if (activateMark) {
                markingTime = now;
                authd.AuthRequestActivateMark(static_cast<int>(accountId), markingTime, markValue, dormantType);
            }
        }
    }

    {
        L2_GUARD(accountMapSect);
        const auto existing = accountMap.find(accountId);
        if (existing != accountMap.end() && existing->second != nullptr) {
            CUserSocket* const socket = existing->second;
            if (socket->m_nCloseReason != SCR_REMAIN) {
                if ((static_cast<std::uint32_t>(socket->GetRaidStatus()) & static_cast<std::uint32_t>(URS_ON_RAID)) != 0) {
                    User* const socketUser = socket->GetUser();
                    if (socketUser != nullptr) {
                        RaidClientManager::inst().DisconnectRaidUser(
                            static_cast<int>(socket->GetServerId()), static_cast<int>(socketUser->GetDBID())
                        );
                    }
                }

                authd.AuthPlayFail(static_cast<int>(accountId), 0x0e);
                socket->SendSystemMessage(SystemMessageId::DUPLICATE_LOGIN_ACCOUNT_SECURITY_NOTICE);
                socket->Send("cd", 0xb0, 1);
                // Log.Add(
                //     LOG_NORMAL,
                //     L"[%S][%d]Kick (%d)(%d)(%s)",
                //     "AuthSocket.cpp",
                //     0x1f6,
                //     socket->GetAccountID(),
                //     static_cast<int>(socket->GetServerId()),
                //     socket->GetAccountName())
                // Log.Add(
                //     LOG_WARN,
                //     "[auth] Rejecting duplicate login for account %u. Closing its existing client connection (socket
                //     account %u, socket server ID %u).", static_cast<unsigned>(accountId),
                //     static_cast<unsigned>(socket->GetAccountID()), static_cast<unsigned>(socket->GetServerId()))
                L2_LOG_WARN(
                    Log,
                    "[auth] Rejecting duplicate login for account %u; closing its existing client connection (socket "
                    "account %u, socket server ID %u).",
                    static_cast<unsigned>(accountId), static_cast<unsigned>(socket->GetAccountID()),
                    static_cast<unsigned>(socket->GetServerId())
                );
                socket->Close();

                if (accountId != socket->GetAccountID()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d]account map error. erase and continue. "
                    //     L"[%d](%d)(%s)",
                    //     "AuthSocket.cpp",
                    //     0x1fd,
                    //     accountId,
                    //     socket->GetAccountID(),
                    //     socket->GetAccountName())
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[auth] Account-map key %u points to socket account %u. Erasing the mismatched entry during
                    //     duplicate-login cleanup.", static_cast<unsigned>(accountId),
                    //     static_cast<unsigned>(socket->GetAccountID()))
                    L2_LOG_ERROR(
                        Log,
                        "[auth] Account-map key %u points to socket account %u; erasing the mismatched entry during "
                        "duplicate-login cleanup.",
                        static_cast<unsigned>(accountId), static_cast<unsigned>(socket->GetAccountID())
                    );
                    accountMap.erase(accountId);
                }

                CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
                if (user) {
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     L"[%S][%d]Kick (%d)(%d)(%s)",
                    //     "AuthSocket.cpp",
                    //     0x205,
                    //     user->GetAccountID(),
                    //     user->GetServerId(),
                    //     user->GetName())
                    // Log.Add(
                    //     LOG_WARN,
                    //     "[auth] Duplicate-login cleanup found a world user for account %u (user account %u, user server ID
                    //     %u), checking its client connection.", static_cast<unsigned>(accountId),
                    //     static_cast<unsigned>(user->GetAccountID()), static_cast<unsigned>(user->GetServerId()))
                    L2_LOG_WARN(
                        Log,
                        "[auth] Duplicate-login cleanup found a world user for account %u (user account %u, user server ID "
                        "%u); checking its client connection.",
                        static_cast<unsigned>(accountId), static_cast<unsigned>(user->GetAccountID()),
                        static_cast<unsigned>(user->GetServerId())
                    );
                    CUserSocket* const userSocket = user->GetSocket();
                    if (userSocket != nullptr) {
                        userSocket->SendSystemMessage(SystemMessageId::DUPLICATE_LOGIN_ACCOUNT_SECURITY_NOTICE);
                        userSocket->Send("cd", 0xb0, 1);
                        userSocket->Close();
                    }
                }
                NPSystem::CNPD::LoginStep_Logout(accountId);
                return false;
            }

            if (socket->GetAccountID() == accountId) {
                // Log.Add(
                //     LOG_NORMAL,
                //     L"Cannot kick (%d)(%s)",
                //     socket->GetAccountID(),
                //     socket->GetAccountName())
                // Log.Add(
                //     LOG_WARN,
                //     "[auth] Rejecting login for account %u: its existing session is retained for delayed combat logout
                //     (close reason 4).", static_cast<unsigned>(accountId))
                L2_LOG_WARN(
                    Log,
                    "[auth] Rejecting login for account %u: its existing session is retained for delayed combat logout "
                    "(close reason 4).",
                    static_cast<unsigned>(accountId)
                );
                authd.AuthPlayFail(static_cast<int>(accountId), 0x0e);
                return false;
            }

            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]account map error. erase and continue. "
            //     L"[%d](%d)(%s)",
            //     "AuthSocket.cpp",
            //     0x1de,
            //     accountId,
            //     socket->GetAccountID(),
            //     socket->GetAccountName())
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Account-map key %u points to retained socket account %u. Erasing the mismatched entry and
            //     continuing login.", static_cast<unsigned>(accountId), static_cast<unsigned>(socket->GetAccountID()))
            L2_LOG_ERROR(
                Log,
                "[auth] Account-map key %u points to retained socket account %u; erasing the mismatched entry and "
                "continuing login.",
                static_cast<unsigned>(accountId), static_cast<unsigned>(socket->GetAccountID())
            );
            accountMap.erase(accountId);
        }
    }

    stAccountAuthInfo accountInfo;
    accountInfo.nWarnFlag = warnFlag;
    accountInfo.nTimeLeft = timeLeft;
    accountInfo.nStat = stat;
    accountInfo.nLoginFlag = loginFlag;
    accountInfo.cChargeMethod = chargeMethod;
    accountInfo.nReservations = reservations;
    accountInfo.nRestTime_china = restTimeChina;
    accountInfo.nPlayTime_china = playTimeChina;
    CopyNarrowString(accountInfo.sPCCafeCode, pcCafeCode.data());
    accountInfo.client_addr.s_addr = clientAddress;
    CopyNarrowString(accountInfo.sAccountName, accountName.data());
    accountInfo.nPremiumLevelFlag = premiumLevel;
    accountInfo.nMarkingTime = markingTime;
    accountInfo.nDormantType = dormantType;

    const auto publishAccountInfo = [&](WAITER_STATUS waiterStatus) {
        accountInfo.WaiterStatus = waiterStatus;
        {
            L2_WRITE_GUARD(AccountAuthInfoLock);
            g_AccountAuthInfoMap[accountId] = accountInfo;
        }
    };

    const auto continueLogin = [&](bool requestPlayTime) {
        if (requestPlayTime) {
            db.RequestLoadPlayTime(static_cast<int>(accountId));
        } else if (g_nAgeCheckType == 1) {
            authd.AuthRequestAgeKoreanStyle(static_cast<int>(accountId));
        } else {
            authd.AuthPlayOk(static_cast<int>(accountId));
        }
    };

    if (!g_bQueueTicketEnabled && g_nServerUserCount <= g_nServerUserCountLimit - 1) {
        publishAccountInfo(WAITER_NO);
        const bool ordinaryAccount = (loginFlag & 0x30) == 0 && (premiumLevel & 1U) == 0;
        continueLogin(ordinaryAccount && g_nPlayTimeLimit > 0);
        return false;
    }

    if (!g_bQueueTicketBuilderOff) {
        const bool forceLogin = (loginFlag & 0x30) != 0;
        const bool premiumPass = (premiumLevel & 1U) != 0 && g_nServerUserCount <= g_nPremiumLimit - 1;
        if (forceLogin || premiumPass) {
            publishAccountInfo(WAITER_VIPPASS);
            continueLogin(false);
            return false;
        }

        for (const char* builderAccount : g_mapBuilderAccount) {
            if (builderAccount != nullptr && CompareAccountNameCaseInsensitive(builderAccount, accountName.data()) == 0) {
                publishAccountInfo(WAITER_VIPPASS);
                continueLogin(false);
                return false;
            }
        }
    }

    if (g_bQueueTicketEnabled) {
        QueueTicket* const queueTicket = QueueTicket::Inst();
        if (queueTicket->GetTotalWaiterCount() < g_nWaiterMaxCount) {
            publishAccountInfo(WAITER_QUEUETICKET);
            continueLogin(g_nPlayTimeLimit > 0);
            queueTicket->AddWaiter(static_cast<int>(accountId), nullptr);

            {
                L2_WRITE_GUARD(g_AccountVIPLevelByDBLock);
                if (g_AccountVIPLevelByDBMap.find(accountId) == g_AccountVIPLevelByDBMap.end()) {
                    g_AccountVIPLevelByDBMap.emplace(accountId, stCacheDAccountVIPLevel{});
                }
            }
            return false;
        }
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[%s][%d],[SHANGHAI_QUEUETICKET] queue ticket full, "
        //     L"account : %d",
        //     L"AuthSocket.cpp",
        //     0x330,
        //     accountId)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[auth] Login rejected for account %u: the login queue is full.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_INFO(Log, "[auth] Login rejected for account %u: the login queue is full.", static_cast<unsigned>(accountId));
    }

    authd.AuthPlayFail(static_cast<int>(accountId), 0x0f);
    NPSystem::CNPD::LoginStep_Logout(accountId);
    const std::wstring wideAccountName = AccountNameForLoginLog(accountName.data());
    Log.AddT(
        LOG_BOTH,
        u"%I64d,,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,,,,,%s,,,,",
        static_cast<std::int64_t>(0x343), static_cast<std::int64_t>(accountId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(g_nServerUserCount), static_cast<std::int64_t>(g_nServerUserCountLimit),
        static_cast<std::int64_t>(stat), static_cast<std::int64_t>(timeLeft), static_cast<std::int64_t>(warnFlag),
        wideAccountName.c_str()
    );
    return false;
}

bool AuthRequestKickAccount(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    char kickReason = 0;
    std::array<char, 0x0f> accountName{};
    ::Disassemble(packet, "dcs", &accountId, &kickReason, static_cast<int>(accountName.size()), accountName.data());

    // Log.Add(
    //     LOG_NORMAL,
    //     "[auth] AuthD requested disconnection of account %u (reason %d).",
    //     static_cast<unsigned>(accountId), static_cast<int>(kickReason))
    L2_LOG_INFO(
        Log, "[auth] AuthD requested disconnection of account %u (reason %d).", static_cast<unsigned>(accountId),
        static_cast<int>(kickReason)
    );
    {
        L2_GUARD(g_ReadyAccountMapSect);
        const auto ready = g_ReadyAccountMap.find(accountId);
        if (ready == g_ReadyAccountMap.end()) {
            // Noise: a kick need not find a pending-login entry. Active account and world-user lookups follow.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] DualLogin-KickAccount : account not exists "
            //     L"in ReadyAccountMap [%d]",
            //     "AuthSocket.cpp",
            //     0x35b,
            //     accountId)
        } else {
            g_ReadyAccountMap.erase(accountId);
        }
    }

    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account == accountMap.end()) {
            // Noise: absence from the account map does not end kick processing. World-user lookup follows.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] KickAccount received. but not exist account : "
            //     L"account_id[%d][%S]",
            //     "AuthSocket.cpp",
            //     0x398,
            //     accountId,
            //     accountName.data())
        } else if (account->second == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] KickAccount received. but not exist socket : "
            //     L"account_id[%d][%S]",
            //     "AuthSocket.cpp",
            //     0x394,
            //     accountId,
            //     accountName.data())
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] AuthD requested a kick for account %u, but its account-map entry has no socket. Continuing
            //     world-user lookup (reason %d).", static_cast<unsigned>(accountId), static_cast<int>(kickReason))
            L2_LOG_ERROR(
                Log,
                "[auth] AuthD requested a kick for account %u, but its account-map entry has no socket; continuing "
                "world-user lookup (reason %d).",
                static_cast<unsigned>(accountId), static_cast<int>(kickReason)
            );
        } else {
            CUserSocket* const socket = account->second;
            if (kickReason == 7) {
                socket->SendSystemMessage(SystemMessageId::DUPLICATE_LOGIN_ACCOUNT_SECURITY_NOTICE);
                socket->Send("cd", 0xb0, 1);
            } else if (kickReason == 11) {
                socket->Send("cd", 0xb0, 2);
            } else if (kickReason == 34 && (g_nCountryCode == 4 || !g_bFreeOpenServer)) {
                socket->SendSystemMessage(SystemMessageId::PREPAID_TIME_EXPIRED_NO_RESERVED_PAYMENT);
            }
            // Noise: the account kick request and reason are reported once at handler entry.
            // Log.Add(
            //     LOG_NORMAL,
            //     L"Auth Kick (%d)(%d)(%s)",
            //     socket->GetAccountID(),
            //     static_cast<int>(kickReason),
            //     socket->GetAccountName())

            if ((static_cast<std::uint32_t>(socket->GetRaidStatus()) & static_cast<std::uint32_t>(URS_ON_RAID)) != 0) {
                User* const user = socket->GetUser();
                if (user != nullptr) {
                    RaidClientManager::inst().DisconnectRaidUser(
                        static_cast<int>(socket->GetServerId()), static_cast<int>(user->GetDBID())
                    );
                }
            }
            socket->m_nCloseReason = SCR_AUTH_KICK;
            socket->SetCloseTimer();
        }
    }

    {
        L2_WRITE_GUARD(KeyLock);
        g_OneTimeKeyMap.erase(accountId);
    }

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (!user) {
        // Noise: a kick may arrive before world entry or after logout. Its request is reported once above.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] KickAccount received. but not exist UserSP : "
        //     L"account_id[%d][%S]",
        //     "AuthSocket.cpp",
        //     0x3bd,
        //     accountId,
        //     accountName.data())
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Noise: the world user has no client connection to close. Its kick request is reported once above.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] KickAccount received. but not exist user socket : "
        //     L"account_id[%d][%S]",
        //     "AuthSocket.cpp",
        //     0x3b7,
        //     accountId,
        //     accountName.data())
        return false;
    }
    if (kickReason == 7) {
        socket->SendSystemMessage(SystemMessageId::DUPLICATE_LOGIN_ACCOUNT_SECURITY_NOTICE);
        socket->Send("cd", 0xb0, 1);
    } else if (kickReason == 11) {
        socket->Send("cd", 0xb0, 2);
    }
    socket->m_nCloseReason = SCR_AUTH_KICK;
    socket->SetCloseTimer();
    // Noise: the account kick request is reported once. This is the second possible connection lookup.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Auth Kick (%d)(%d)(%s)",
    //     user->GetAccountID(),
    //     user->GetServerId(),
    //     user->GetName())
    return false;
}

bool AuthRequestUserNum(CAuthSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    authd.AuthUserNum(g_nServerUserCount, g_nServerUserCountLimit);
    return false;
}

bool AuthRequestServerVersion(CAuthSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    authd.AuthSendServerVersion();
    return false;
}

bool AuthRequestPing(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int value = 0;
    ::Disassemble(packet, "d", &value);
    authd.AuthPing(value);
    return false;
}

bool AuthReplyReconnect(CAuthSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    ++authd.m_nReplyReconnBufNum;
    // Noise: per-block acknowledgements are summarized when the expected block count is reached.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Receive [%d]th block",
    //     authd.m_nReplyReconnBufNum)
    if (authd.m_nReplyReconnBufNum == authd.m_nMaxReconnBufNum) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "[auth] AuthD acknowledged all %d expected reconnect blocks. Issuing the completion notification.",
        //     authd.m_nReplyReconnBufNum)
        L2_LOG_INFO(
            Log, "[auth] AuthD acknowledged all %d expected reconnect blocks; issuing the completion notification.",
            authd.m_nReplyReconnBufNum
        );
        authd.SendAuthConnected();
    }
    return false;
}

bool AuthRequestNotifyRemainTime(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    int remainingTime = 0;
    int reservations = 0;
    ::Disassemble(packet, "ddd", &accountId, &remainingTime, &reservations);

    L2_GUARD(accountMapSect);
    const auto account = accountMap.find(accountId);
    if (account != accountMap.end() && account->second != nullptr) {
        CUserSocket* const socket = account->second;
        const int secondsWithinHour = remainingTime % 3600;
        socket->Send(
            "chcchcccc", 0x62, SystemMessageId::PREPAID_USAGE_TIME_AND_RESERVATIONS_REMAINING, 3, 0x15,
            static_cast<int>(static_cast<std::int16_t>(remainingTime / 3600)), 0x14,
            static_cast<int>(static_cast<std::int8_t>(secondsWithinHour / 60)), 0x14,
            static_cast<int>(static_cast<std::int8_t>(reservations))
        );
        socket->ChangeReservation(reservations);
    }
    return false;
}

bool AuthRequestApplyNewChargeItem(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    int remainingTime = 0;
    int reservations = 0;
    ::Disassemble(packet, "ddd", &accountId, &remainingTime, &reservations);

    L2_GUARD(accountMapSect);
    const auto account = accountMap.find(accountId);
    if (account != accountMap.end() && account->second != nullptr) {
        CUserSocket* const socket = account->second;
        const int secondsWithinHour = remainingTime % 3600;
        socket->Send(
            "chcchcc", 0x62, SystemMessageId::NEXT_PREPAID_RESERVATION_ACTIVATED_WITH_TIME_REMAINING, 2, 0x15,
            static_cast<int>(static_cast<std::int16_t>(remainingTime / 3600)), 0x14,
            static_cast<int>(static_cast<std::int8_t>(secondsWithinHour / 60))
        );

        const std::uint32_t remainingBits = socket->m_nLeftTime - static_cast<std::uint32_t>(socket->GetSocketUseTime());
        std::int32_t currentRemainingTime = 0;
        std::memcpy(&currentRemainingTime, &remainingBits, sizeof(currentRemainingTime));
        if (currentRemainingTime < -30000 || currentRemainingTime > 30000) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"APPLY NEW CHARGEITEM PACKET Error!! "
            //     L"Remain time(%d) is error!",
            //     currentRemainingTime)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Account %u has observed remaining prepaid time %d outside -30000..30000. Applying the new charge
            //     item with %d seconds and %d reservations.", static_cast<unsigned>(accountId), currentRemainingTime,
            //     remainingTime, reservations)
            L2_LOG_ERROR(
                Log,
                "[auth] Account %u has observed remaining prepaid time %d outside -30000..30000; applying the new charge "
                "item with %d seconds and %d reservations.",
                static_cast<unsigned>(accountId), currentRemainingTime, remainingTime, reservations
            );
        }
        socket->ChangeTimeLeft(remainingTime, reservations);
    }
    return false;
}

bool AuthReplyAgeForKoreanStyle(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    int result = 0;
    int age = 0;
    int gender = 0;
    ::Disassemble(packet, "dccc", &accountId, &result, &age, &gender);
    (void)gender;

    if (result == 0) {
        authd.AuthPlayFail(static_cast<int>(accountId), 0x0e);
        NPSystem::CNPD::LoginStep_Logout(accountId);
        return false;
    }

    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "AuthSocket.cpp",
            //     0x423,
            //     accountId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Age reply for account %u has no authentication-cache entry. Skipping the age update and continuing
            //     login approval.", static_cast<unsigned>(accountId))
            L2_LOG_ERROR(
                Log,
                "[auth] Age reply for account %u has no authentication-cache entry; skipping the age update and continuing "
                "login approval.",
                static_cast<unsigned>(accountId)
            );
        } else {
            account->second.nAge = age;
        }
    }
    authd.AuthPlayOk(static_cast<int>(accountId));
    return false;
}

namespace AuthPacketHandler {

bool ServerSideLoginUserAck(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    char result = 0;
    int timeLeft = 0;
    int loginFlag = 0;
    int reservations = 0;
    int restTime = 0;
    int playTime = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "dcddddd", &accountId, &result, &timeLeft, &loginFlag, &reservations, &restTime, &playTime);

    unsigned short premiumLevel = 0;
    if (g_ShutDownSystem) {
        int ignoredAccountId = 0;
        char ignoredResult = 0;
        cursor = ::Disassemble(cursor, "dc", &ignoredAccountId, &ignoredResult);
        (void)ignoredAccountId;
        (void)ignoredResult;

        for (std::size_t offset = 0; offset < sizeof(authd.m_PlayTimeInfo); offset += 3) {
            // Current 0x004ab890 loads the byte value, not its address.
            cursor = ::Disassemble(cursor, "b", 3, authd.m_PlayTimeInfo[offset]);
        }
        cursor = ::Disassemble(cursor, "h", &premiumLevel);

        short extraLength = 0;
        cursor = ::Disassemble(cursor, "h", &extraLength);
        if (extraLength > 0) {
            unsigned char* const extra = new unsigned char[static_cast<std::size_t>(extraLength)];
            ::Disassemble(cursor, "b", static_cast<int>(extraLength), extra);
            delete[] extra;
        }
    }

    {
        L2_GUARD(authd.m_Lock);
        const auto connected = authd.m_sendUserList.find(static_cast<std::int32_t>(accountId));
        if (connected == authd.m_sendUserList.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"ServerSideLoginUserAck : cannot find account:%d",
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Resynchronization reply for account %u has no pending transfer entry. Continuing
            //     authentication-cache lookup (result %d).", static_cast<unsigned>(accountId), static_cast<int>(result))
            L2_LOG_WARN(
                Log,
                "[auth] Resynchronization reply for account %u has no pending transfer entry; continuing "
                "authentication-cache lookup (result %d).",
                static_cast<unsigned>(accountId), static_cast<int>(result)
            );
        } else {
            delete connected->second;
            authd.m_sendUserList.erase(connected);
        }
    }

    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "ServerSideLoginUserAck : cannot find account_id [%d]",
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Resynchronization reply for account %u has no authentication-cache entry. Skipping its cached
            //     login-data update (result %d).", static_cast<unsigned>(accountId), static_cast<int>(result))
            L2_LOG_WARN(
                Log,
                "[auth] Resynchronization reply for account %u has no authentication-cache entry; skipping its cached "
                "login-data update (result %d).",
                static_cast<unsigned>(accountId), static_cast<int>(result)
            );
        } else if (result == 0) {
            account->second.nTimeLeft = timeLeft;
            account->second.nLoginFlag = loginFlag;
            account->second.nReservations = reservations;
            account->second.nRestTime_china = restTime;
            account->second.nPlayTime_china = playTime;
            account->second.nPremiumLevelFlag = premiumLevel;
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "ServerSideLoginUserAck : Result Error [%d][%d]",
            //     accountId,
            //     static_cast<int>(result))
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] AuthD rejected resynchronization for account %u (result %d). Cached login data is unchanged.",
            //     static_cast<unsigned>(accountId), static_cast<int>(result))
            L2_LOG_WARN(
                Log, "[auth] AuthD rejected resynchronization for account %u (result %d); cached login data is unchanged.",
                static_cast<unsigned>(accountId), static_cast<int>(result)
            );
        }
    }
    return false;
}

bool GetUserData(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    char result = 0;
    unsigned short dataLength = 0;
    const unsigned char* const data = ::Disassemble(packet, "dch", &accountId, &result, &dataLength);

    if (result == 0) {
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] AuthD rejected user-data retrieval for account %u (result 0). Requesting closure if connected and
        //     clearing pending read/write state.", static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log,
            "[auth] AuthD rejected user-data retrieval for account %u (result 0); requesting closure if connected and "
            "clearing pending read/write state.",
            static_cast<unsigned>(accountId)
        );
        {
            L2_GUARD(accountMapSect);
            const auto account = accountMap.find(accountId);
            if (account != accountMap.end() && account->second != nullptr) {
                account->second->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }

        {
            L2_WRITE_GUARD(AuthUserDataInfoLock);
            const auto userData = g_AuthUserDataInfoMap.find(accountId);
            if (userData != g_AuthUserDataInfoMap.end()) {
                runtime::raw_atomic::ExchangeI32(&userData->second.nRWType, NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT);
                runtime::raw_atomic::ExchangeI32(
                    &userData->second.nRWReason, NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT
                );
            }
        }
        return false;
    }

    std::int32_t rwType = NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT;
    {
        L2_READ_GUARD(AuthUserDataInfoLock);
        const auto userData = g_AuthUserDataInfoMap.find(accountId);
        if (userData == g_AuthUserDataInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][ATTEND_ERR] SQ_GET_USER_DATA Packet Received. "
            //     L"But Can't Find Account Id In AuthUserDataInfoMap!!",
            //     L"AuthSocket.cpp",
            //     0x553,
            //     accountId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] User-data reply for account %u has no pending user-data record. Ignoring its payload.",
            //     static_cast<unsigned>(accountId))
            L2_LOG_ERROR(
                Log, "[auth] User-data reply for account %u has no pending user-data record; ignoring its payload.",
                static_cast<unsigned>(accountId)
            );
        } else {
            // Current L2Server.exe 0x004abcf8 reads map-node +0x60, which is
            // AuthUserDataInfo::nRWType at value +0x44. The recovered source
            // previously mistook this lane for eJob at value +0x3c, leaving the
            // initial read permanently in progress after a successful reply.
            rwType = runtime::raw_atomic::LoadI32Relaxed(&userData->second.nRWType);
        }
    }

    if (rwType == NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_READ) {
        authd.AuthSetUserData(static_cast<int>(accountId), dataLength, data);
    } else if (rwType == NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_WRITE) {
        authd.AuthWriteUserData(static_cast<int>(accountId), dataLength, data);
    }
    return false;
}

bool WriteUserDataAck(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    char result = 0;
    ::Disassemble(packet, "dc", &accountId, &result);

    AuthUserDataInfo info;
    int useTime = 0;
    int playTime = 0;
    int rwType = NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT;
    int rwReason = NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT;
    {
        L2_READ_GUARD(AuthUserDataInfoLock);
        const auto userData = g_AuthUserDataInfoMap.find(accountId);
        if (userData == g_AuthUserDataInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "No AuthUserDataInfo : account[%d]",
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] User-data write acknowledgement for account %u has no pending user-data record (result %d)
            //     continuing without a recorded write reason.", static_cast<unsigned>(accountId), static_cast<int>(result))
            L2_LOG_WARN(
                Log,
                "[auth] User-data write acknowledgement for account %u has no pending user-data record (result %d); "
                "continuing without a recorded write reason.",
                static_cast<unsigned>(accountId), static_cast<int>(result)
            );
        } else {
            info = userData->second;
            useTime = userData->second.nUseTime;
            playTime = userData->second.nPlayTime;
            rwType = userData->second.nRWType;
            rwReason = userData->second.nRWReason;
            runtime::raw_atomic::ExchangeI32(&userData->second.nRWType, NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT);
            runtime::raw_atomic::ExchangeI32(&userData->second.nRWReason, NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT);
        }
    }

    int maxCharacterId = 0;
    int maxLevel = 0;
    {
        L2_READ_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "AuthSocket.cpp",
            //     0x59e,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] User-data write acknowledgement for account %u has no authentication-cache entry. Continuing with
            //     zero character maxima.", static_cast<unsigned>(accountId))
            L2_LOG_WARN(
                Log,
                "[auth] User-data write acknowledgement for account %u has no authentication-cache entry; continuing with "
                "zero character maxima.",
                static_cast<unsigned>(accountId)
            );
        } else if (g_bDualBillingSystem) {
            maxCharacterId = account->second.nMaxCharId;
            maxLevel = account->second.nMaxLevel;
        }
    }

    if (rwReason == NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_CHARACTER_COUNT) {
        authd.RequestLogout(static_cast<int>(accountId), useTime, playTime, maxCharacterId, maxLevel, 0);
        return false;
    }
    if (rwReason != NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE) {
        return false;
    }

    VipAttendanceSystem& attendance = VipAttendanceSystem::Inst();
    if (info.eJob == NEW_AUTH_USER_DATA::AJ_RESET) {
        attendance.ReceiveToAuthResetVipAttendance(accountId, result, info);
        return false;
    }
    if (info.eJob == NEW_AUTH_USER_DATA::AJ_UPDATE) {
        CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
        if (!user || user->GetSocket() == nullptr) {
            if (attendance.IsAcquiredVIPAttendanceFlag(accountId)) {
                attendance.SendToAuthRollbackVipAttendance(accountId);
                // Log.Add(
                //     LOG_ERROR,
                //     "[NO_ERROR][VIPAttendance] Rollback AuthUserDataInfo : "
                //     "account[%d]",
                //     accountId)
                // Log.Add(
                //     LOG_NORMAL,
                //     "[auth] Requested VIP attendance rollback for account %u because its user or client connection is no
                //     longer available.", static_cast<unsigned>(accountId))
                L2_LOG_INFO(
                    Log,
                    "[auth] Requested VIP attendance rollback for account %u because its user or client connection is no "
                    "longer available.",
                    static_cast<unsigned>(accountId)
                );
                attendance.ReleaseVIPAttendanceFlag(accountId);
            }
        } else {
            user->OnRequestVipAttendanceReward(result);
        }
        return false;
    }
    if (info.eJob == NEW_AUTH_USER_DATA::AJ_SET_BUILDER) {
        CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
        if (user && user->GetSocket() != nullptr) {
            attendance.AddUserAttendanceInfo(user->GetSocket(), info);
            attendance.SendVipAttendanceItemList(user.get());
        }
    }
    (void)rwType;
    return false;
}

bool CompleteServerSideLoginUserList(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int receivedUserCount = 0;
    ::Disassemble(packet, "d", &receivedUserCount);
    if (receivedUserCount != authd.m_lastSendUserCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "CompleteServerSideLoginUserList Send [%d] != Receive [%d]",
        //     authd.m_nReconnectCompleteUserCount,
        //     authd.m_nReconnectTryUserCount)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] AuthD acknowledged %d accounts for a batch advertising %d. Continuing with the next resynchronization
        //     batch.", receivedUserCount, authd.m_lastSendUserCount)
        L2_LOG_ERROR(
            Log,
            "[auth] AuthD acknowledged %d accounts for a batch advertising %d; continuing with the next resynchronization "
            "batch.",
            receivedUserCount, authd.m_lastSendUserCount
        );
    }
    authd.SendConnectedUserList();
    return false;
}

bool BanUserByServerRequestAck(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId = 0;
    char result = 0;
    ::Disassemble(packet, "dc", &accountId, &result);
    return false;
}

bool AuthCheckCharMaxLevel(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId = 0;
    ::Disassemble(packet, "d", &accountId);
    db.RequestLoadAccountMaxLevel(accountId);
    return false;
}

bool AuthActivateMark(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId = 0;
    int result = 0;
    ::Disassemble(packet, "dd", &accountId, &result);
    return false;
}

bool AuthWaitUserAck(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId;
    ::Disassemble(packet, "d", &accountId);
    return false;
}

bool ReplyWebSessionID(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId = 0;
    char result = 0;
    char webSessionId[16] = {};
    char ignoredWebSessionId[16] = {};
    char status = 0;
    ::Disassemble(packet, "dcbbc", &accountId, &result, 0x10, webSessionId, 0x10, ignoredWebSessionId, &status);
    (void)result;
    (void)ignoredWebSessionId;
    (void)status;

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (user) {
        user->GetSocket()->SetWebSessionId(webSessionId);
        user->GetSocket()->Send("chb", 0xfe, 0x104, 0x10, webSessionId);
    }
    return false;
}

bool ReplyCloseWebSessionID(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    int result = 0;
    ::Disassemble(packet, "dd", &accountId, &result);

    const auto account = g_AccountAuthInfoMap.find(accountId);
    if (result != 0 || account == g_AccountAuthInfoMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] account not found or session cannot close.."
        //     L"account_id(%d) result(%d)",
        //     "AuthSocket.cpp",
        //     0x616,
        //     accountId,
        //     result)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Web-session close reply for account %u: result %d. Authentication-cache entry %s.",
        //     static_cast<unsigned>(accountId), result, account == g_AccountAuthInfoMap.end() ? "missing" : "present")
        L2_LOG_WARN(
            Log, "[auth] Web-session close reply for account %u: result %d; authentication-cache entry %s.",
            static_cast<unsigned>(accountId), result, account == g_AccountAuthInfoMap.end() ? "missing" : "present"
        );
    }
    return false;
}

bool Auth2ndPasswordAck(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    char requestType = 0;
    char ignoredValue = 0;
    std::uint32_t accountId = 0;
    int resultCode = 0;
    unsigned short responseValue = 0;
    unsigned short ignoredResponseValue = 0;
    int finalResponseValue = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ccddhhd", &requestType, &ignoredValue, &accountId, &resultCode, &responseValue, &ignoredResponseValue,
        &finalResponseValue
    );
    (void)ignoredValue;
    (void)ignoredResponseValue;

    L2_GUARD(accountMapSect);
    const auto account = accountMap.find(accountId);
    if (account == accountMap.end() || account->second == nullptr) {
        return false;
    }

    CUserSocket* const socket = account->second;
    if (requestType == 0 || requestType == 1 || requestType == 2) {
        int clientResult = resultCode;
        if (resultCode == 0) {
            clientResult = 0;
        } else if (resultCode == 0x3d) {
            clientResult = 1;
        } else if (resultCode == 0x3e) {
            clientResult = 2;
        }
        socket->Send(
            "chcdd", 0xfe, 0x107, static_cast<int>(requestType), clientResult, static_cast<unsigned int>(responseValue)
        );
        return false;
    }

    if (requestType == 3) {
        int clientResult = resultCode;
        if (resultCode == 0) {
            socket->SetSecondaryAuthState(CUserSocket::SAS_COMPLETED);
            clientResult = 0;
        } else if (resultCode == 0x3d) {
            clientResult = 1;
        } else if (resultCode == 0x3e) {
            clientResult = 2;
        }
        socket->Send("chdd", 0xfe, 0x106, clientResult, static_cast<unsigned int>(responseValue));
        return false;
    }

    if (requestType == 4) {
        int secondaryResult = 0;
        ::Disassemble(cursor, "d", &secondaryResult);

        int clientResult = resultCode;
        if (resultCode < 0x3d || resultCode == 0x42) {
            if (secondaryResult > 0) {
                socket->SetSecondaryAuthState(CUserSocket::SAS_NOT_NEED);
                clientResult = 2;
            } else if (resultCode == 0) {
                clientResult = 1;
            } else if (resultCode == 0x42) {
                clientResult = 0;
            } else {
                clientResult = -1;
            }
        }
        socket->Send("chdd", 0xfe, 0x105, clientResult, finalResponseValue);
    }
    return false;
}

bool AuthGetValueInfo(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    char ignoredValue1 = 0;
    char ignoredValue2 = 0;
    int remainingTime = 0;
    int reservations = 0;
    char ignoredValue3 = 0;
    int ignoredValue4 = 0;
    char ignoredBlock[16] = {};
    char ignoredValue5 = 0;
    ::Disassemble(
        packet, "dccddcdbc", &accountId, &ignoredValue1, &ignoredValue2, &remainingTime, &reservations, &ignoredValue3,
        &ignoredValue4, 0x10, ignoredBlock, &ignoredValue5
    );
    (void)ignoredValue1;
    (void)ignoredValue2;
    (void)ignoredValue3;
    (void)ignoredValue4;
    (void)ignoredBlock;
    (void)ignoredValue5;

    L2_GUARD(accountMapSect);
    const auto account = accountMap.find(accountId);
    if (account == accountMap.end() || account->second == nullptr) {
        return false;
    }

    CUserSocket* const socket = account->second;
    const int payKind = socket->m_nPayKind;
    if (payKind != 3 && payKind != 1 && payKind != 4) {
        return false;
    }

    const int secondsWithinHour = remainingTime % 3600;
    const std::int8_t minutes = static_cast<std::int8_t>(secondsWithinHour / 60);
    const std::int16_t hours = static_cast<std::int16_t>(remainingTime / 3600);
    if (payKind == 4) {
        socket->Send(
            "chcchcccc", 0x62, SystemMessageId::PREPAID_USAGE_TIME_AND_RESERVATIONS_REMAINING, 3, 0x15,
            static_cast<int>(hours), 0x14, static_cast<int>(minutes), 0x14,
            static_cast<int>(static_cast<std::int8_t>(reservations))
        );
    } else if (payKind == 1) {
        socket->Send(
            "chcchcc", 0x62, SystemMessageId::INDIVIDUAL_ACCOUNT_USAGE_HOURS_AND_MINUTES_REMAINING, 2, 0x15,
            static_cast<int>(hours), 0x14, static_cast<int>(minutes)
        );
    } else {
        socket->Send(
            "chccccc", 0x62, SystemMessageId::PC_CAFE_PREPAID_HOURS_AND_MINUTES_REMAINING, 2, 0x14,
            static_cast<int>(static_cast<std::int8_t>(remainingTime / 3600)), 0x14, static_cast<int>(minutes)
        );
    }
    socket->ChangeTimeLeft(remainingTime, reservations);
    return false;
}

bool AuthGetCharMaxLevel(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    char result = 0;
    int maxCharacterId = 0;
    int maxLevel = 0;
    ::Disassemble(packet, "dcdd", &accountId, &result, &maxCharacterId, &maxLevel);
    (void)result;

    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "AuthSocket.cpp",
            //     0x6bb,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Maximum-character-level reply for account %u has no authentication-cache entry. Ignoring the
            //     update.", static_cast<unsigned>(accountId))
            L2_LOG_WARN(
                Log,
                "[auth] Maximum-character-level reply for account %u has no authentication-cache entry; ignoring the "
                "update.",
                static_cast<unsigned>(accountId)
            );
        } else {
            account->second.nMaxCharId = maxCharacterId;
            account->second.nMaxLevel = maxLevel;
        }
    }
    return false;
}

bool AuthRequestAppTokenAck(CAuthSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::uint32_t accountId = 0;
    int result = 0;
    char authNToken[0x65]{};
    ::Disassemble(packet, "ddb", &accountId, &result, static_cast<int>(sizeof(authNToken)), authNToken);

    if (!g_bUseNPLogin) {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account == accountMap.end() || account->second == nullptr || !account->second->m_bActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][STEAM] Send AuthNToken Fail!! UserSocket Is "
            //     L"NULL. AccountId = %d, Result = %d, AuthNToken = %s",
            //     L"AuthSocket.cpp",
            //     0x71e,
            //     accountId,
            //     result,
            //     authNToken)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Cannot deliver the application-token reply for account %u: no active client connection (result
            //     %d), ignoring the reply.", static_cast<unsigned>(accountId), result)
            L2_LOG_WARN(
                Log,
                "[auth] Cannot deliver the application-token reply for account %u: no active client connection (result %d); "
                "ignoring the reply.",
                static_cast<unsigned>(accountId), result
            );
            return false;
        }

        if (result != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][STEAM] Response AuthNToken Fail!! "
            //     L"AccountId = %d, Result = %d",
            //     L"AuthSocket.cpp",
            //     0x710,
            //     accountId,
            //     result)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] AuthD application-token request failed for account %u (result %d). Forwarding the failure reply to
            //     the client.", static_cast<unsigned>(accountId), result)
            L2_LOG_ERROR(
                Log,
                "[auth] AuthD application-token request failed for account %u (result %d); forwarding the failure reply to "
                "the client.",
                static_cast<unsigned>(accountId), result
            );
        }
        account->second->Send("chcb", 0xfe, 0x1b2, result == 0, static_cast<int>(sizeof(authNToken)), authNToken);
        return false;
    }

    if (result == 0) {
        if (!NPSystem::g_NPD.SendAuthLoginTokenForServer(static_cast<int>(accountId), authNToken)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError(a=%d, e=%d) fail to GetAppTokenAck()",
            //     L"AuthSocket.cpp",
            //     0x72d,
            //     accountId,
            //     6)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Cannot forward the application token for account %u to NP authentication. Rejecting login with
            //     error 6.", static_cast<unsigned>(accountId))
            L2_LOG_ERROR(
                Log,
                "[auth] Cannot forward the application token for account %u to NP authentication; rejecting login with "
                "error 6.",
                static_cast<unsigned>(accountId)
            );
            CUserSocket::OnLoginFailed(
                nullptr, accountId,
                // 6)
                NPSystem::LEC_NPFAIL_GSLOGIN_REQ
            );
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to "
        //     L"GetAppTokenAck(nResult=%d)",
        //     L"AuthSocket.cpp",
        //     0x726,
        //     accountId,
        //     4,
        //     result)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] AuthD application-token request failed for account %u (result %d). Rejecting NP login with error 4.",
        //     static_cast<unsigned>(accountId), result)
        L2_LOG_ERROR(
            Log,
            "[auth] AuthD application-token request failed for account %u (result %d); rejecting NP login with error 4.",
            static_cast<unsigned>(accountId), result
        );
        CUserSocket::OnLoginFailed(
            nullptr, accountId,
            // 4)
            NPSystem::LEC_GPFAIL_APPTOKEN_ACK
        );
    }
    return false;
}

} // namespace AuthPacketHandler

// Current .data image 0x015c4180..0x015c4298. OnRead accepts opcodes
// 0x00..0x21. Slot 0x22 is the physical sentinel kept by the original
// array extent.
CAuthCallback authPacketTable[35] = {
    &AuthRequestAboutToPlay, // 0x00
    &AuthRequestKickAccount, // 0x01
    &AuthRequestUserNum, // 0x02
    &AuthRequestServerVersion, // 0x03
    &AuthRequestPing, // 0x04
    &AuthReplyReconnect, // 0x05
    &DummyAuthPacket, // 0x06
    &DummyAuthPacket, // 0x07
    &DummyAuthPacket, // 0x08
    &DummyAuthPacket, // 0x09
    &AuthReplyAgeForKoreanStyle, // 0x0a
    &AuthRequestNotifyRemainTime, // 0x0b
    &AuthRequestApplyNewChargeItem, // 0x0c
    &AuthPacketHandler::GetUserData, // 0x0d
    &AuthPacketHandler::WriteUserDataAck, // 0x0e
    &DummyAuthPacket, // 0x0f
    &AuthPacketHandler::AuthGetValueInfo, // 0x10
    &DummyAuthPacket, // 0x11
    &DummyAuthPacket, // 0x12
    &AuthPacketHandler::AuthWaitUserAck, // 0x13
    &DummyAuthPacket, // 0x14
    &AuthPacketHandler::ServerSideLoginUserAck, // 0x15
    &AuthPacketHandler::CompleteServerSideLoginUserList, // 0x16
    &AuthPacketHandler::Auth2ndPasswordAck, // 0x17
    &AuthPacketHandler::BanUserByServerRequestAck, // 0x18
    &DummyAuthPacket, // 0x19
    &DummyAuthPacket, // 0x1a
    &AuthPacketHandler::ReplyWebSessionID, // 0x1b
    &DummyAuthPacket, // 0x1c
    &AuthPacketHandler::ReplyCloseWebSessionID, // 0x1d
    &AuthPacketHandler::AuthCheckCharMaxLevel, // 0x1e
    &AuthPacketHandler::AuthGetCharMaxLevel, // 0x1f
    &AuthPacketHandler::AuthActivateMark, // 0x20
    &AuthPacketHandler::AuthRequestAppTokenAck, // 0x21
    &DummyAuthPacket, // 0x22
};
