#include "QueueTicket.h"

#include "Shared/src/Log.h"
#include "Shared/src/ScopedLockGuards.h"
#include "UserSocket.h"

#include <cstdint>
#include <ctime>
#include <limits>

namespace {

void ObserveLocalTime32(const std::int32_t* value) noexcept {
#if defined(_WIN32)
    (void)::_localtime32(reinterpret_cast<const __time32_t*>(value));
#else
    const std::time_t hostValue = static_cast<std::time_t>(*value);
    (void)std::localtime(&hostValue);
#endif
}

} // namespace

QueueTicket::QueueTicket() {
    m_QueueTicketSeed = 1;
    m_CountEntranceForce = 0;
}

QueueTicket::~QueueTicket() = default;

bool QueueTicket::AddWaiter(int accountId, CUserSocket* socket) {
    if (m_QueueTicketSeed == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[SHANGHAI_QUEUETICKET],queue ticket seed overflow %I64d, account %d",
        //     static_cast<std::int64_t>(-1),
        //     accountId)
        // L2_LOG_ERROR(Log,
        //     L"[SHANGHAI_QUEUETICKET],queue ticket seed overflow %I64d, account %d",
        //     static_cast<std::int64_t>(-1),
        //     accountId)
        L2_LOG_ERROR(
            Log, "Cannot queue account %d: the 64-bit ticket seed reached its maximum %llu; waiter is not added.", accountId,
            static_cast<unsigned long long>(m_QueueTicketSeed)
        );
        return false;
    }

    {
        L2_WRITE_GUARD(m_LockWaiterQueue);

        auto waiter = m_WaiterQueue.begin();
        while (waiter != m_WaiterQueue.end() && waiter->AccountID != accountId) {
            ++waiter;
        }

        if (waiter == m_WaiterQueue.end()) {
            WAITER_INFO info;
            info.QueueTicketNumber = static_cast<std::int32_t>(m_QueueTicketSeed);
            ++m_QueueTicketSeed;
            info.AccountID = accountId;

            // Current 0x00a14b11 calls _localtime32 on this field before its
            // first store and discards the returned tm pointer.
            ObserveLocalTime32(&info.WaiterRegisterTime);

            info.LastRequestTime = 0;
            info.EntranceTime = 0;
            info.pSocket = socket;
            m_WaiterQueue.push_back(info);
        }
    }
    return true;
}

int QueueTicket::GetWaiterCount(int accountId) {
    const int count = [&]() -> int {
        L2_READ_GUARD(m_LockWaiterQueue);

        if (m_WaiterQueue.empty()) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[SHANGHAI_QUEUETICKET],no waiters. queue is empty %d",
            //     accountId)
            // L2_LOG_INFO(Log,
            //     L"[SHANGHAI_QUEUETICKET],no waiters. queue is empty %d",
            //     accountId)
            // Noise: Polling an empty login queue is routine and already returns zero waiters.
            return 0;
        }

        auto waiter = m_WaiterQueue.begin();
        if (waiter->AccountID == accountId) {
            ObserveLocalTime32(&waiter->LastRequestTime);
            return 1;
        }

        while (waiter != m_WaiterQueue.end() && waiter->AccountID != accountId) {
            ++waiter;
        }

        if (waiter == m_WaiterQueue.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[SHANGHAI_QUEUETICKET],fail to find account %d",
            //     accountId)
            // L2_LOG_ERROR(Log,
            //     L"[SHANGHAI_QUEUETICKET],fail to find account %d",
            //     accountId)
            L2_LOG_ERROR(
                Log,
                "Account %d is absent from the nonempty login queue; position calculation continues past its last waiter.",
                accountId
            );
        } else {
            ObserveLocalTime32(&waiter->LastRequestTime);
        }

        int count = 0;
        for (auto current = m_WaiterQueue.begin(); current != waiter; ++current) {
            ++count;
        }
        ++count;

        if (count < 1) {
            const int queueTicketNumber = waiter != m_WaiterQueue.end() ? waiter->QueueTicketNumber : 0;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[SHANGHAI_QUEUETICKET],waiter count error %d, QueueTicketNumber %d, FirstQueueTicketNumber %d",
            //     count,
            //     queueTicketNumber,
            //     m_WaiterQueue.begin()->QueueTicketNumber)
            // L2_LOG_ERROR(Log,
            //     L"[SHANGHAI_QUEUETICKET],waiter count error %d, QueueTicketNumber %d, FirstQueueTicketNumber %d",
            //     count,
            //     queueTicketNumber,
            //     m_WaiterQueue.begin()->QueueTicketNumber)
            L2_LOG_ERROR(
                Log, "Invalid login queue position %d for account %d (ticket %d, first ticket %d); returning zero.", count,
                accountId, queueTicketNumber, m_WaiterQueue.begin()->QueueTicketNumber
            );
            count = 0;
        }

        return count;
    }();
    return count;
}

bool QueueTicket::AbleEntrance(int accountId) {
    {
        L2_WRITE_GUARD(m_LockWaiterQueue);

        if (m_WaiterQueue.empty()) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[SHANGHAI_QUEUETICKET],already entranced. queue is empty %d",
            //     accountId)
            // L2_LOG_INFO(Log,
            //     L"[SHANGHAI_QUEUETICKET],already entranced. queue is empty %d",
            //     accountId)
            // Noise: An empty queue returns false here. The old message incorrectly implied successful entrance.
        } else {
            const auto first = m_WaiterQueue.begin();
            if (first->AccountID == accountId) {
                m_WaiterQueue.erase(first);
                return true;
            }
        }
    }
    return false;
}

bool QueueTicket::CancleWaiter(int accountId) {
    const bool found = [&]() -> bool {
        L2_WRITE_GUARD(m_LockWaiterQueue);

        auto waiter = m_WaiterQueue.begin();
        while (waiter != m_WaiterQueue.end() && waiter->AccountID != accountId) {
            ++waiter;
        }

        const bool found = waiter != m_WaiterQueue.end();
        if (found) {
            m_WaiterQueue.erase(waiter);
        }

        return found;
    }();
    return found;
}

bool QueueTicket::EntranceForce(int count) {
    {
        L2_WRITE_GUARD(m_LockWaiterQueue);

        int index = 0;
        while (index < count && !m_WaiterQueue.empty()) {
            const auto first = m_WaiterQueue.begin();
            if (first->pSocket != nullptr) {
                first->pSocket->Send("chcd", 0xfe, 0x1ad, 1, 0);
                first->pSocket->SetPassQueueTicket(true);
            }
            m_WaiterQueue.erase(first);
            ++index;
        }
    }
    return true;
}
