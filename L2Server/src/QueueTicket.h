#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;

class QueueTicket {
public:
    struct WAITER_INFO {
        std::int32_t AccountID;
        std::int32_t QueueTicketNumber;
        std::int32_t LastRequestTime;
        std::int32_t WaiterRegisterTime;
        std::int32_t EntranceTime;
        CUserSocket* pSocket;
    };

    using WAITER_INFO_LIST = std::list<WAITER_INFO>;

    QueueTicket();
    virtual ~QueueTicket();

    static QueueTicket* Inst() {
        static QueueTicket QueueTicketInstance;
        return &QueueTicketInstance;
    }

    int GetTotalWaiterCount() const { return static_cast<int>(m_WaiterQueue.size()); }

    bool AddWaiter(int accountId, CUserSocket* socket);
    int GetWaiterCount(int accountId);
    bool AbleEntrance(int accountId);
    bool CancleWaiter(int accountId);
    bool EntranceForce(int count);

private:
    WAITER_INFO_LIST m_WaiterQueue;
    RWLock m_LockWaiterQueue;
    std::int32_t m_CountEntranceForce;
    std::uint64_t m_QueueTicketSeed;
};
