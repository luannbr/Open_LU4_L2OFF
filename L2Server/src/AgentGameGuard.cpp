#include "AgentGameGuard.h"

#include "CConfig.h"
#include "Default.h"
#include "User.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdint>
#include <cwchar>
#include <string>

namespace {

std::u16string AuthDataText(const _GG_AUTH_DATA& data) {
    std::array<char16_t, 50> text{};
    FormatUtf16To(
        text.data(), text.size(), u"%d %d %d %d", static_cast<int>(data.dwIndex), static_cast<int>(data.dwValue1),
        static_cast<int>(data.dwValue2), static_cast<int>(data.dwValue3)
    );
    return std::u16string(text.data());
}

void WriteBuilderQueryLog(
    User& user,
    CUserSocket& socket,
    std::uint32_t lastRequestTick,
    std::uint32_t lastResponseTick,
    const _GG_AUTH_DATA& query
) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x5a0, &user, nullptr, nullptr, nullptr, nullptr, lastRequestTick, lastResponseTick, socket.m_nLastTimeCheck,
        runtime::raw_atomic::LoadI32Relaxed(&socket.m_nRecvPingCount), query.dwIndex, query.dwValue1, query.dwValue2,
        query.dwValue3, 0, 0, 0
    );
}

void WriteBuilderResponseLog(
    User& user,
    CUserSocket& socket,
    std::uint32_t lastRequestTick,
    std::uint32_t lastResponseTick,
    const _GG_AUTH_DATA& answer,
    bool skipAuthentication
) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x5a1, &user, nullptr, nullptr, nullptr, nullptr, lastRequestTick, lastResponseTick, socket.m_nLastTimeCheck,
        runtime::raw_atomic::LoadI32Relaxed(&socket.m_nRecvPingCount), answer.dwIndex, answer.dwValue1, answer.dwValue2,
        answer.dwValue3, skipAuthentication ? 1 : 0, 0, 0
    );
}

} // namespace

GameGuardAgent::GameGuardAgent(Agent* previous) : Agent(previous, GAME_GUARD) {
    m_noResponceCount = 0;
    const std::uint32_t tick = runtime::GetTickCount();
    m_lastRequestTick = tick;
    m_lastResponseTick = tick;
}

GameGuardAgent::~GameGuardAgent() = default;

void GameGuardAgent::CheckGameGuardInvalidVersion(User* user, std::uint32_t reason) {
    // Current L2Server.exe 0x00451314.
    if (user != nullptr && (reason == 0x0cU || reason == 0x0eU)) {
        user->GetSocket()->Send("ch", 0xfe, 0x49);
    }
}

bool GameGuardAgent::QueryFirstImpl(PacketList::iterator current) {
    // Current L2Server.exe 0x00451358.
    L2_GUARD_FUNCTION();
    if (!g_bCheckGameGuardAuth) {
        (*current)->Assemble("dddd", 0, 0, 0, 0);
        return true;
    }

    const std::uint32_t error = m_csa.GetAuthQuery();
    if (error != 0U) {
        if (!myUser->IsBuilder()) {
            const FVector auditPosition = myUser->GetPos();
            CLogFormatter audit;
            const std::u16string auditIpAddress = myUser->GetSocket()->GetIPAddressString();
            const std::u16string auditTargetIp = std::u16string(auditIpAddress.c_str());
            const std::u16string auditTargetName = std::u16string(myUser->GetName());
            const int auditLevel = myUser->Level();
            const std::uint32_t auditAccountId = myUser->GetAccountID();
            const std::uint32_t auditDatabaseId = myUser->GetDBID();
            audit.WriteAuditLog(
                0x585, auditDatabaseId, auditAccountId, 0, 0, runtime::Win64CvttDoubleToInt32(auditPosition.x),
                runtime::Win64CvttDoubleToInt32(auditPosition.y), runtime::Win64CvttDoubleToInt32(auditPosition.z),
                auditTargetIp.c_str(), u"", u"", 6, 0, 0, error, 1, 0, 0, 0, 0, auditLevel, auditTargetName.c_str(),
                myUser->m_sAccountName.data(), u"", u"", 0
            );
            // Log.Add(
            //     LOG_OPT_ERROR,
            //     L"[GameGuard] [%s] Time over! (timer expired)",
            //     myUser->GetName())
            // L2_LOG(Log,
            //     LOG_OPT_ERROR,
            //     L"[GameGuard] [%s] Time over! (timer expired)",
            //     myUser->GetName())
            L2_LOG(
                Log, LOG_OPT_ERROR, "GameGuard could not create the first query for player '%s' (error %u); %s.",
                Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error),
                g_bCheckGameGuardAuthCheckOnly ? "check-only mode keeps the query path running"
                                               : "scheduling a delayed close and rejecting this query"
            );
            if (!g_bCheckGameGuardAuthCheckOnly) {
                myUser->SetCloseLater();
                return false;
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GameGuard] GM [%s] Timer over! first query error %d",
            //     myUser->GetName(),
            //     error)
            // L2_LOG_ERROR(Log,
            //     L"[GameGuard] GM [%s] Timer over! first query error %d",
            //     myUser->GetName(),
            //     error)
            // L2_LOG_ERROR(Log,
            //     "GameGuard could not create the first query for builder '%s' (error %u). Builder handling continues
            //     without scheduling a close.", WideToUtf8(std::wstring_view(myUser->GetName())).c_str(),
            //     static_cast<unsigned>(error))
            L2_LOG_ERROR(
                Log,
                "GameGuard could not create the first query for builder '%s' (error %u); builder handling continues without "
                "scheduling a close.",
                Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error)
            );
        }
        CheckGameGuardInvalidVersion(myUser, error);
    }

    (*current)->Assemble(
        "dddd", m_csa.m_AuthQuery.dwIndex, m_csa.m_AuthQuery.dwValue1, m_csa.m_AuthQuery.dwValue2, m_csa.m_AuthQuery.dwValue3
    );
    m_lastRequestTick = runtime::GetTickCount();
    return true;
}

bool GameGuardAgent::QuerySecondImpl() {
    // Current L2Server.exe 0x004516c0.
    L2_GUARD_FUNCTION();
    if (!g_bCheckGameGuardAuth) {
        return true;
    }

    CUserSocket* const socket = myUser->GetSocket();
    const std::uint32_t error = m_csa.GetAuthQuery();
    if (error != 0U) {
        const FVector auditPosition = myUser->GetPos();
        CLogFormatter audit;
        const std::u16string auditIpAddress = myUser->GetSocket()->GetIPAddressString();
        const std::u16string auditTargetIp = std::u16string(auditIpAddress.c_str());
        const std::u16string auditTargetName = std::u16string(myUser->GetName());
        const int auditLevel = myUser->Level();
        const std::uint32_t auditAccountId = myUser->GetAccountID();
        const std::uint32_t auditDatabaseId = myUser->GetDBID();
        audit.WriteAuditLog(
            0x585, auditDatabaseId, auditAccountId, 0, 0, runtime::Win64CvttDoubleToInt32(auditPosition.x),
            runtime::Win64CvttDoubleToInt32(auditPosition.y), runtime::Win64CvttDoubleToInt32(auditPosition.z),
            auditTargetIp.c_str(), u"", u"", 6, 0, 0, error, 3, 0, 0, 0, 0, auditLevel, auditTargetName.c_str(),
            myUser->m_sAccountName.data(), u"", u"", 0
        );
        if (!myUser->IsBuilder()) {
            if (!g_bCheckGameGuardAuthCheckOnly) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] force close called in game guard",
                //     L"AgentGameGuard.cpp",
                //     0x80)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] force close called in game guard",
                //     L"AgentGameGuard.cpp",
                //     0x80)
                // L2_LOG_ERROR(Log,
                //     "GameGuard could not create the next query for player '%s' (error %u). Force-closing the client
                //     connection.", WideToUtf8(std::wstring_view(myUser->GetName())).c_str(), static_cast<unsigned>(error))
                L2_LOG_ERROR(
                    Log,
                    "GameGuard could not create the next query for player '%s' (error %u); force-closing the client "
                    "connection.",
                    Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error)
                );
                socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
                return false;
            }
            // Log.Add(
            //     LOG_OPT_ERROR,
            //     L"[GameGuard] [%s] Time over! (timer expired)",
            //     myUser->GetName())
            // L2_LOG(Log,
            //     LOG_OPT_ERROR,
            //     L"[GameGuard] [%s] Time over! (timer expired)",
            //     myUser->GetName())
            L2_LOG(
                Log, LOG_OPT_ERROR,
                "GameGuard could not create the next query for player '%s' (error %u); check-only mode keeps the query path "
                "running.",
                Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error)
            );
        } else {
            myUser->SendSystemMessage(u"%s", u"[GameGuard] fail");
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GameGuard] GM [%s] Timer over! second query error %d",
            //     myUser->GetName(),
            //     error)
            // L2_LOG_ERROR(Log,
            //     L"[GameGuard] GM [%s] Timer over! second query error %d",
            //     myUser->GetName(),
            //     error)
            // L2_LOG_ERROR(Log,
            //     "GameGuard could not create the next query for builder '%s' (error %u). The builder notification was
            //     requested and query handling continues.", WideToUtf8(std::wstring_view(myUser->GetName())).c_str(),
            //     static_cast<unsigned>(error))
            L2_LOG_ERROR(
                Log,
                "GameGuard could not create the next query for builder '%s' (error %u); the builder notification was "
                "requested and query handling continues.",
                Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error)
            );
        }
        CheckGameGuardInvalidVersion(myUser, error);
    }

    m_lastRequestTick = runtime::GetTickCount();
    socket->Send(
        "cdddd", 0x74, m_csa.m_AuthQuery.dwIndex, m_csa.m_AuthQuery.dwValue1, m_csa.m_AuthQuery.dwValue2,
        m_csa.m_AuthQuery.dwValue3
    );
    if (g_bBuilderLogAdd && myUser->IsBuilder()) {
        WriteBuilderQueryLog(*myUser, *socket, m_lastRequestTick, m_lastResponseTick, m_csa.m_AuthQuery);
    }
    return true;
}

bool GameGuardAgent::ResponseFirstImpl(const unsigned char** packet) {
    // Current L2Server.exe 0x00451afc.
    L2_GUARD_FUNCTION();
    m_noResponceCount = 0;
    *packet = Disassemble(
        *packet, "dddd", &m_csa.m_AuthAnswer.dwIndex, &m_csa.m_AuthAnswer.dwValue1, &m_csa.m_AuthAnswer.dwValue2,
        &m_csa.m_AuthAnswer.dwValue3
    );
    if (!g_bCheckGameGuardAuth) {
        return true;
    }

    const _GG_AUTH_DATA query = m_csa.m_AuthQuery;
    const std::uint32_t error = m_csa.CheckAuthAnswer();
    if (error == 0U) {
        m_lastResponseTick = runtime::GetTickCount();
        return true;
    }

    const FVector auditPosition = myUser->GetPos();
    const std::u16string queryText = AuthDataText(query);
    const std::u16string answerText = AuthDataText(m_csa.m_AuthAnswer);
    CLogFormatter audit;
    const std::u16string auditIpAddress = myUser->GetSocket()->GetIPAddressString();
    const std::u16string auditTargetIp = std::u16string(auditIpAddress.c_str());
    const std::u16string auditTargetName = std::u16string(myUser->GetName());
    const int auditLevel = myUser->Level();
    const std::uint32_t auditAccountId = myUser->GetAccountID();
    const std::uint32_t auditDatabaseId = myUser->GetDBID();
    audit.WriteAuditLog(
        0x585, auditDatabaseId, auditAccountId, 0, 0, runtime::Win64CvttDoubleToInt32(auditPosition.x),
        runtime::Win64CvttDoubleToInt32(auditPosition.y), runtime::Win64CvttDoubleToInt32(auditPosition.z),
        auditTargetIp.c_str(), queryText.c_str(), answerText.c_str(), 6, 0, 0, error, 5, 0, 0, 0, 0, auditLevel,
        auditTargetName.c_str(), myUser->m_sAccountName.data(), u"", u"", 0
    );
    if (!myUser->IsBuilder()) {
        // Log.Add(
        //     LOG_OPT_ERROR,
        //     L"[GameGuard] [%s] fail to ResponseFirst error %d",
        //     myUser->GetName(),
        //     error)
        // L2_LOG(Log,
        //     LOG_OPT_ERROR,
        //     L"[GameGuard] [%s] fail to ResponseFirst error %d",
        //     myUser->GetName(),
        //     error)
        L2_LOG(
            Log, LOG_OPT_ERROR, "GameGuard rejected the first answer from player '%s' (error %u); %s.",
            Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error),
            g_bCheckGameGuardAuthCheckOnly ? "check-only mode continues failure handling without scheduling a close"
                                           : "scheduling a delayed close and rejecting this response"
        );
        if (!g_bCheckGameGuardAuthCheckOnly) {
            myUser->SetCloseLater();
            return false;
        }
    } else if (CUserSocket* const socket = myUser->GetSocket()) {
        socket->SendSystemMessage(u"%s", u"Gameguard fail");
    }
    CheckGameGuardInvalidVersion(myUser, error);
    return true;
}

bool GameGuardAgent::TimeCheckImpl() {
    // Current L2Server.exe 0x00451f04.
    L2_GUARD_FUNCTION();
    if (!g_bCheckGameGuardAuth || runtime::GetTickCount() - m_lastRequestTick <= 240000U) {
        return true;
    }

    if (m_lastRequestTick < m_lastResponseTick + 10U) {
        m_noResponceCount = 0;
    } else {
        const FVector auditPosition = myUser->GetPos();
        CLogFormatter audit;
        const std::u16string auditIpAddress = myUser->GetSocket()->GetIPAddressString();
        const std::u16string auditTargetIp = std::u16string(auditIpAddress.c_str());
        const std::u16string auditTargetName = std::u16string(myUser->GetName());
        const int auditLevel = myUser->Level();
        const std::uint32_t auditAccountId = myUser->GetAccountID();
        const std::uint32_t auditDatabaseId = myUser->GetDBID();
        audit.WriteAuditLog(
            0x585, auditDatabaseId, auditAccountId, 0, 0, runtime::Win64CvttDoubleToInt32(auditPosition.x),
            runtime::Win64CvttDoubleToInt32(auditPosition.y), runtime::Win64CvttDoubleToInt32(auditPosition.z),
            auditTargetIp.c_str(), u"", u"", 6, 0, 0, 0x0b, 2, 0, 0, 0, 0, auditLevel, auditTargetName.c_str(),
            myUser->m_sAccountName.data(), u"", u"", 0
        );
        if (!myUser->IsBuilder()) {
            if (!g_bCheckGameGuardAuthCheckOnly) {
                ++m_noResponceCount;
                if (m_noResponceCount > 1U) {
                    myUser->SetCloseLater();
                }
                return false;
            }
        } else {
            myUser->SendSystemMessage(u"%s", u"[GameGuard] fail");
        }
    }
    return QuerySecondImpl();
}

bool GameGuardAgent::ResponseSecond(const unsigned char** packet) {
    // Current L2Server.exe 0x00452228. The current body intentionally
    // returns false after the optional builder diagnostic on every path.
    L2_GUARD_FUNCTION();
    char skipAuthentication = 0;
    *packet = Disassemble(
        *packet, "ddddc", &m_csa.m_AuthAnswer.dwIndex, &m_csa.m_AuthAnswer.dwValue1, &m_csa.m_AuthAnswer.dwValue2,
        &m_csa.m_AuthAnswer.dwValue3, &skipAuthentication
    );

    if (skipAuthentication == 0 && g_bCheckGameGuardAuth) {
        const _GG_AUTH_DATA query = m_csa.m_AuthQuery;
        const std::uint32_t error = m_csa.CheckAuthAnswer();
        if (error == 0U) {
            m_lastResponseTick = runtime::GetTickCount();
        } else {
            const std::u16string queryText = AuthDataText(query);
            const std::u16string answerText = AuthDataText(m_csa.m_AuthAnswer);
            const FVector auditPosition = myUser->GetPos();
            CLogFormatter audit;
            const std::u16string auditIpAddress = myUser->GetSocket()->GetIPAddressString();
            const std::u16string auditTargetIp = std::u16string(auditIpAddress.c_str());
            const std::u16string auditTargetName = std::u16string(myUser->GetName());
            const int auditLevel = myUser->Level();
            const std::uint32_t auditAccountId = myUser->GetAccountID();
            const std::uint32_t auditDatabaseId = myUser->GetDBID();
            audit.WriteAuditLog(
                0x585, auditDatabaseId, auditAccountId, 0, 0, runtime::Win64CvttDoubleToInt32(auditPosition.x),
                runtime::Win64CvttDoubleToInt32(auditPosition.y), runtime::Win64CvttDoubleToInt32(auditPosition.z),
                auditTargetIp.c_str(), queryText.c_str(), answerText.c_str(), 6, 0, 0, error, 4, 0, 0, 0, 0, auditLevel,
                auditTargetName.c_str(), myUser->m_sAccountName.data(), u"", u"", 0
            );
            if (!myUser->IsBuilder()) {
                // Log.Add(
                //     LOG_OPT_ERROR,
                //     L"[GameGuard] [%s] Response Second error %d",
                //     myUser->GetName(),
                //     error)
                // L2_LOG(Log,
                //     LOG_OPT_ERROR,
                //     L"[GameGuard] [%s] Response Second error %d",
                //     myUser->GetName(),
                //     error)
                L2_LOG(
                    Log, LOG_OPT_ERROR, "GameGuard rejected the second answer from player '%s' (error %u); %s.",
                    Utf16ToUtf8(myUser->GetName()).c_str(), static_cast<unsigned>(error),
                    g_bCheckGameGuardAuthCheckOnly ? "check-only mode continues failure handling without scheduling a close"
                                                   : "scheduling a delayed close and rejecting this response"
                );
                if (!g_bCheckGameGuardAuthCheckOnly) {
                    myUser->SetCloseLater();
                    return false;
                }
            } else if (CUserSocket* const socket = myUser->GetSocket()) {
                socket->SendSystemMessage(u"%s", u"Gameguard fail");
            }
            CheckGameGuardInvalidVersion(myUser, error);
        }
    }

    if (g_bBuilderLogAdd && myUser->IsBuilder()) {
        if (CUserSocket* const socket = myUser->GetSocket()) {
            WriteBuilderResponseLog(
                *myUser, *socket, m_lastRequestTick, m_lastResponseTick, m_csa.m_AuthAnswer, skipAuthentication != 0
            );
        }
    }
    return false;
}
