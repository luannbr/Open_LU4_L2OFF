#include "UserBroadcaster.h"

#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOC.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/YieldLock.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>

class BroadcastSerializer final : public CIOObject {
public:
    explicit BroadcastSerializer(std::int32_t tick) noexcept;
    ~BroadcastSerializer() override = default;

    void Push(DefaultIOBuffer* buffer);
    void TimerExpired(int id) override;

private:
    std::deque<DefaultIOBuffer*> m_queue;
    CYieldLock m_lock;
    std::atomic<std::int32_t> m_started;
    std::int32_t m_tick;
};


BroadcastSerializer gBroadcastSerializer{0x2134};

namespace {


DefaultIOBuffer* MakeBroadcastBuffer(const char* format, va_list arguments) {
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const std::uint16_t bodySize = static_cast<std::uint16_t>(
        VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments)
    );
    if (bodySize == 0) {
        buffer->Release();
        return nullptr;
    }

    const std::uint16_t packetSize = static_cast<std::uint16_t>(bodySize + 2);
    std::memcpy(buffer->m_Buffer.data(), &packetSize, sizeof(packetSize));
    buffer->m_dwSize = packetSize;
    return buffer;
}

class BroadcastVisitor final {
public:
    explicit BroadcastVisitor(DefaultIOBuffer* buffer) noexcept : m_buffer(buffer) {}

    bool Do(User* user) {
        m_buffer->AddRef();
        user->GetSocket()->AsyncSendConstBuffer(m_buffer);
        return true;
    }

private:
    DefaultIOBuffer* m_buffer;
};

} // namespace

BroadcastSerializer::BroadcastSerializer(const std::int32_t tick) noexcept :
    m_started(0), m_tick(tick) {}

void BroadcastSerializer::Push(DefaultIOBuffer* buffer) {
    // Current L2Server 0x00bdf9c8.
    {
        L2_GUARD(m_lock);
        buffer->AddRef();
        m_queue.push_back(buffer);
    }

    if (m_started.exchange(1, std::memory_order_seq_cst) == 0) {
        AddTimer(m_tick, 0);
    }
}

void BroadcastSerializer::TimerExpired(int id) {
    // Current L2Server 0x00bdfda4. The shipped owner holds
    // its queue lock while enqueuing the broadcast to every user.
    L2_GUARD_FUNCTION();
    static_cast<void>(id);

    {
        L2_GUARD(m_lock);
        if (!m_queue.empty()) {
            DefaultIOBuffer* const buffer = m_queue.front();
            FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                buffer->AddRef();
                user->GetSocket()->AsyncSendConstBuffer(buffer);
            });
            buffer->Release();
            m_queue.pop_front();
        }
    }
    AddTimer(m_tick, 0);
}

CUserBroadcaster::CUserBroadcaster() : m_nIndex(0) {}

CUserBroadcaster::~CUserBroadcaster() = default;

void CUserBroadcaster::BroadcastToAllUser(const char* format, ...) {
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = MakeBroadcastBuffer(format, arguments);
    va_end(arguments);
    if (!buffer) {
        return;
    }

    g_Timer.BeginJob(JOB_AsyncSendConstBuffer);
    BroadcastVisitor broadcast(buffer);
    g_UserMap.FOR_ALL(&broadcast, L"UserBroadcaster.cpp", 0x23);
    buffer->Release();
    ++m_nIndex;
    g_Timer.EndJob(JOB_AsyncSendConstBuffer);
}

void CUserBroadcaster::BroadcastToAllUser(const S_EX_PROTOCOL protocol, const network::IPacket& packet) {
    // Current L2Server.exe 0x00bdefa0.
    L2_GUARD_FUNCTION();

    DefaultIOBuffer* const buffer =
        MakeConstBufferFromPacketEx(static_cast<char>(0xfe), static_cast<std::int16_t>(protocol), packet);
    if (buffer == nullptr) {
        return;
    }

    g_Timer.BeginJob(JOB_AsyncSendConstBuffer);
    g_UserMap.FOR_ALL_EX([buffer](User* const user) {
        static constexpr wchar_t functionName[] = L"UserBroadcaster.cpp(228)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        buffer->AddRef();
        user->GetSocket()->AsyncSendConstBuffer(buffer);
        return true;
    });
    buffer->Release();
    g_Timer.EndJob(JOB_AsyncSendConstBuffer);
}

void CUserBroadcaster::BroadcastToAllUserEx(const char* format, ...) {
    // Current L2Server 0x00bdfa5c.
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = MakeBroadcastBuffer(format, arguments);
    va_end(arguments);
    if (!buffer) {
        return;
    }

    gBroadcastSerializer.Push(buffer);
    buffer->Release();
}

void CUserBroadcaster::BroadcastToAllUserAnnounce(const char16_t* message) {
    L2_GUARD_FUNCTION();
    BroadcastToAllUser("cddSdS", 0x4a, 0, 10, u"", -1, message);
}

void CUserBroadcaster::BroadcastToAllUserScreenAnnounce(const wchar_t* message) {
    L2_GUARD_FUNCTION();
    const std::u16string targetMessage = WideToUtf16(message);
    BroadcastToAllUser("cddSdS", 0x4a, 0, 0x13, u"", -1, message ? targetMessage.c_str() : nullptr);
}

void CUserBroadcaster::BroadcastToAllUserScreenAnnounceEX(const wchar_t* message) {
    // Current L2Server 0x00bdfc34. The EX variant queues through the
    // serialized broadcast path while retaining the same wire payload.
    L2_GUARD_FUNCTION();
    const std::u16string targetMessage = WideToUtf16(message);
    BroadcastToAllUserEx("cddSdS", 0x4a, 0, 0x13, u"", -1, message ? targetMessage.c_str() : nullptr);
}

void CUserBroadcaster::BroadcastToCanSunsetUser(const char* format, ...) {
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = MakeBroadcastBuffer(format, arguments);
    va_end(arguments);
    if (!buffer) {
        return;
    }

    g_Timer.BeginJob(JOB_AsyncSendConstBuffer);
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        buffer->AddRef();
        if (user->m_nSunRiseForce != 1) {
            user->GetSocket()->AsyncSendConstBuffer(buffer);
        }
    });
    buffer->Release();
    ++m_nIndex;
    g_Timer.EndJob(JOB_AsyncSendConstBuffer);
}
