#include "AgentNewInspector.h"

#include "Default.h"
#include "Fishing.h"
#include "NewInspector.h"
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
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

void WriteInspectorAudit(
    User& user,
    const char16_t* expected,
    const char16_t* received,
    std::int64_t errorCount,
    std::int64_t sequenceNo,
    std::int64_t errorCode,
    std::int64_t sendCount,
    std::int64_t receiveCount,
    std::uint32_t lastRequestTick,
    std::uint32_t lastResponseTick
) {
    const FVector position = user.GetPos();
    const std::u16string ipAddress = user.GetSocket()->GetIPAddressString();
    const std::u16string targetIp = std::u16string(ipAddress.c_str());
    const std::u16string targetName = std::u16string(user.GetName());

    CLogFormatter audit;
    audit.WriteAuditLog(
        0x585, user.GetDBID(), user.GetAccountID(), 0, 0, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), targetIp.c_str(),
        expected, received, 7, errorCount, sequenceNo, 0, errorCode, sendCount, receiveCount, lastRequestTick,
        lastResponseTick, user.Level(), targetName.c_str(), user.m_sAccountName.data(), u"", u"", 0
    );
}

bool IsFishing(const User& user) {
    return user.m_pFishingController->m_FishingState != FISHINGS_NONE || user.m_pFishingController->m_bIsAutoFishing;
}

} // namespace

NewInspectorAgent::NewInspectorAgent(User* user) :
    Agent(user, NEW_INSPECTOR),
    m_ErrorCount(0),
    m_SequenceNo(0),
    m_SendCount(0),
    m_RecvCount(0),
    m_lastRequestTick(runtime::GetTickCount()),
    m_lastResponseTick(m_lastRequestTick + 1U) {}

NewInspectorAgent::~NewInspectorAgent() = default;

Query NewInspectorAgent::MakeRandomQuery() {
    // Current L2Server.exe 0x00452740.
    L2_GUARD_FUNCTION();
    Query query;
    query.textureNo = static_cast<unsigned>(std::rand() % 15);
    query.x = static_cast<unsigned>(std::rand()) & 0x7fU;
    query.y = static_cast<unsigned>(std::rand()) & 0x3fU;
    return query;
}

void NewInspectorAgent::QueryCommon(CPacketHelperEx<20480>& packet) {
    // Current L2Server.exe 0x00452810.
    L2_GUARD_FUNCTION();
    const Query first = MakeRandomQuery();
    const Query second = MakeRandomQuery();
    const RGBA firstAnswer = g_NewInspectorDB.GetAnswer(first);
    const RGBA secondAnswer = g_NewInspectorDB.GetAnswer(second);
    const RGBA answer{
        firstAnswer.r ^ secondAnswer.r,
        firstAnswer.g ^ secondAnswer.g,
        firstAnswer.b ^ secondAnswer.b,
        firstAnswer.a ^ secondAnswer.a,
    };
    m_QA.MakeRequestQuery(first, second, answer);
    packet.Assemble("b", 0x40, m_QA.rawResultSendReceiveValues);
}

bool NewInspectorAgent::QueryFirstImpl(PacketList::iterator current) {
    // Current L2Server.exe 0x00452958.
    L2_GUARD_FUNCTION();
    if (!g_bCheckNewInspectorAuth) {
        std::array<char, 64> empty{};
        (*current)->Assemble("b", 0x40, empty.data());
        return true;
    }

    QueryCommon(**current);
    m_lastRequestTick = runtime::GetTickCount();
    runtime::raw_atomic::IncrementI32(&m_SendCount);
    return true;
}

bool NewInspectorAgent::QuerySecondImpl() {
    // Current L2Server.exe 0x00452a50.
    L2_GUARD_FUNCTION();
    if (g_bCheckNewInspectorAuth) {
        CPacketHelperEx<20480> packet;
        QueryCommon(packet);
        m_lastRequestTick = runtime::GetTickCount();
        runtime::raw_atomic::IncrementI32(&m_SendCount);
        myUser->GetSocket()->Send("chb", 0xfe, 0x46, packet.GetLength(), packet.GetPacket());
    }
    return true;
}

bool NewInspectorAgent::ExecutePolicy(std::uint32_t errorCode) {
    // Current L2Server.exe 0x00452b98.
    L2_GUARD_FUNCTION();
    if (myUser->IsBuilder()) {
        std::array<char16_t, 1024> message{};
        FormatUtf16To(
            message.data(), message.size(), u"[NewInspector] [%s] GM ErrorCode [%d]", myUser->GetName(),
            static_cast<int>(errorCode)
        );
        // Log.Add(LOG_ERROR, message.data())
        // L2_LOG_ERROR(Log, message.data())
        L2_LOG_ERROR(Log, u"%ls", message.data());
        return true;
    }

    if (errorCode == 3U) {
        {
            const FVector position = myUser->GetPos();
            const std::u16string ipAddress = myUser->GetSocket()->GetIPAddressString();
            const std::int32_t x = runtime::Win64CvttDoubleToInt32(position.x);
            const std::int32_t y = runtime::Win64CvttDoubleToInt32(position.y);
            const std::int32_t z = runtime::Win64CvttDoubleToInt32(position.z);
            const char16_t* const userName = myUser->GetName();
            const std::int32_t level = myUser->Level();
            const std::uint32_t lastResponseTick = m_lastResponseTick;
            const std::uint32_t lastRequestTick = m_lastRequestTick;
            const std::int32_t receiveCount = runtime::raw_atomic::LoadI32Relaxed(&m_RecvCount);
            const std::int32_t sendCount = runtime::raw_atomic::LoadI32Relaxed(&m_SendCount);
            const std::int32_t sequenceNo = runtime::raw_atomic::IncrementI32(&m_SequenceNo);
            const std::int32_t errorCount = runtime::raw_atomic::IncrementI32(&m_ErrorCount);
            const std::uint32_t accountId = myUser->GetAccountID();
            const std::uint32_t dbId = myUser->GetDBID();
            const std::u16string targetIp = std::u16string(ipAddress.c_str());
            const std::u16string targetName = std::u16string(userName);

            CLogFormatter audit;
            audit.WriteAuditLog(
                0x585, dbId, accountId, 0, 0, x, y, z, targetIp.c_str(), u"", u"", 7, errorCount, sequenceNo, 0, 3,
                sendCount, receiveCount, lastRequestTick, lastResponseTick, level, targetName.c_str(),
                myUser->m_sAccountName.data(), u"", u"", 0
            );
        }
        static_cast<void>(runtime::raw_atomic::CompareExchangeI32(&m_ErrorCount, 0, 10));
    }

    if (!g_bCheckNewInspectorAuthCheckOnly) {
        myUser->LogAbnormalAction(AUA_INSPECTOR_FORCE_CLOSE, true, 0, 0, 0, 0);
        return false;
    }
    return true;
}

bool NewInspectorAgent::ResponseCommonImpl(const unsigned char** packet, bool firstResponse) {
    // Current L2Server.exe 0x00452ed0.
    L2_GUARD_FUNCTION();
    if (!g_bCheckNewInspectorAuth) {
        return true;
    }

    std::memset(m_QA.SendReceiveValues, 0, sizeof(m_QA.SendReceiveValues));
    std::memset(m_QA.rawSendReceiveValues, 0, sizeof(m_QA.rawSendReceiveValues));
    std::memset(m_QA.rawResultSendReceiveValues, 0, sizeof(m_QA.rawResultSendReceiveValues));
    *packet = Disassemble(*packet, "b", 0x40, m_QA.rawSendReceiveValues);
    runtime::raw_atomic::IncrementI32(&m_RecvCount);
    m_QA.ResponseResultQuery();
    if (m_QA.ValidateResult()) {
        m_lastResponseTick = runtime::GetTickCount();
        runtime::raw_atomic::StoreI32(&m_ErrorCount, 0);
        return true;
    }

    const std::uint32_t errorCode = firstResponse ? 1U : (IsFishing(*myUser) ? 4U : 2U);
    std::array<char16_t, 50> expected{};
    std::array<char16_t, 50> received{};
    FormatUtf16To(
        expected.data(), expected.size(), u"%d %d %d %d", static_cast<int>(m_QA.m_Answer.r),
        static_cast<int>(m_QA.m_Answer.g), static_cast<int>(m_QA.m_Answer.b), static_cast<int>(m_QA.m_Answer.a)
    );
    FormatUtf16To(
        received.data(), received.size(), u"%d %d %d %d", static_cast<int>(m_QA.SendReceiveValues[0]),
        static_cast<int>(m_QA.SendReceiveValues[1]), static_cast<int>(m_QA.SendReceiveValues[2]),
        static_cast<int>(m_QA.SendReceiveValues[3])
    );

    const std::int32_t sequenceNo = runtime::raw_atomic::IncrementI32(&m_SequenceNo);
    const std::u16string expected16 = std::u16string(expected.data());
    const std::u16string received16 = std::u16string(received.data());
    WriteInspectorAudit(
        *myUser, expected16.c_str(), received16.c_str(), 0, sequenceNo, errorCode,
        runtime::raw_atomic::LoadI32Relaxed(&m_SendCount), runtime::raw_atomic::LoadI32Relaxed(&m_RecvCount),
        m_lastRequestTick, m_lastResponseTick
    );
    return ExecutePolicy(errorCode);
}

bool NewInspectorAgent::TimeCheckImpl() {
    // Current L2Server.exe 0x00453360.
    L2_GUARD_FUNCTION();
    if (g_bCheckNewInspectorAuth && runtime::GetTickCount() - m_lastRequestTick > 180000U) {
        if (m_lastResponseTick + 10U <= m_lastRequestTick) {
            const std::uint32_t errorCode = IsFishing(*myUser) ? 5U : 3U;
            if (!ExecutePolicy(errorCode)) {
                return false;
            }
        }
        if (!QuerySecondImpl()) {
            return false;
        }
    }
    return true;
}

bool NewInspectorAgent::ResponseSecond(const unsigned char** packet) {
    // Current L2Server.exe 0x00453470.
    L2_GUARD_FUNCTION();
    return ResponseCommonImpl(packet, false);
}

bool NewInspectorAgent::ResponseFirstImpl(const unsigned char** packet) {
    // Current L2Server.exe 0x0045351c.
    L2_GUARD_FUNCTION();
    return ResponseCommonImpl(packet, true);
}
