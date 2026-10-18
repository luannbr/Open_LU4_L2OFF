#include "IPBlock.h"
#include "Shared/src/MemorySub.h"

#include "CConfig.h"
#include "DB.h"

IPAddress::IPAddress(std::int64_t address) : m_ipPart1(0), m_ipPart2(0), m_ipPart3(0), m_ipPart4(0), m_nIPAddress(address) {
    L2_GUARD_FUNCTION();
}

IPAddress::IPAddress(unsigned int part1, unsigned int part2, unsigned int part3, unsigned int part4) :
    m_ipPart1(part1),
    m_ipPart2(part2),
    m_ipPart3(part3),
    m_ipPart4(part4),
    m_nIPAddress(
        static_cast<std::uint64_t>(part1 << 24U) + static_cast<std::uint64_t>(part3 << 8U) +
        static_cast<std::uint64_t>(part2 << 16U) + static_cast<std::uint64_t>(part4)
    ) {
    L2_GUARD_FUNCTION();
}

int IPAddress::GetIPAddressNumber() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_nIPAddress);
}

IPRange::IPRange(std::int64_t startIP, std::int64_t endIP) : m_firstIP(nullptr), m_lastIP(nullptr) {
    L2_GUARD_FUNCTION();
    m_firstIP = new IPAddress(startIP);
    m_lastIP = new IPAddress(endIP);
}

IPRange::~IPRange() {
    L2_GUARD_FUNCTION();
    delete m_firstIP;
    delete m_lastIP;
}

bool IPRange::IsInRange(IPAddress& address) {
    L2_GUARD_FUNCTION();
    return m_firstIP->GetIPAddressNumber() <= address.GetIPAddressNumber() &&
        address.GetIPAddressNumber() <= m_lastIP->GetIPAddressNumber();
}

IPBlockManager::IPBlockManager() {}

IPBlockManager::~IPBlockManager() {
    L2_GUARD_FUNCTION();
    Clear();
}

L2_DEFINE_OBJECT_CLASS_ID(IPBlockManager)

L2_DEFINE_OBJECT_CLASS_NAME(IPBlockManager, m_pClassName_IPBlockManager)

L2_DEFINE_OBJECT_ALLOCATOR(IPBlockManager, m_pClassName_IPBlockManager)

L2_DEFINE_OBJECT_NEW(IPBlockManager)

L2_DEFINE_OBJECT_RELEASE(IPBlockManager)

bool IPBlockManager::Load(bool loadAll) {
    L2_GUARD_FUNCTION();
    const bool enabled = g_nIPBlock != 0;
    if (enabled) {
        db.RequestLoadBlockedIP(loadAll);
    }
    return enabled;
}

void IPBlockManager::TimerExpired(int id [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    if (g_nIPBlock != 0) {
        Load(false);
        AddTimer(600000, 0);
    }
}

bool IPBlockManager::Init() {
    L2_GUARD_FUNCTION();
    const bool enabled = g_nIPBlock != 0;
    if (enabled) {
        AddTimer(600000, 0);
        Load(true);
    }
    return enabled;
}

bool IPBlockManager::IsBlockedIP(IPAddress& address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (IPRange* range : m_IPList) {
        if (range->IsInRange(address)) {
            return true;
        }
    }
    return false;
}

bool IPBlockManager::AddBlockedIP(IPRange* range) {
    L2_GUARD_FUNCTION();
    if (range != nullptr) {
        L2_GUARD(m_lock);
        m_IPList.push_back(range);
    }
    return true;
}

bool IPBlockManager::AddBlockedIP(std::int64_t startIP, std::int64_t endIP) {
    L2_GUARD_FUNCTION();
    IPRange* const range = new IPRange(startIP, endIP);
    return AddBlockedIP(range);
}

bool IPBlockManager::DeleteBlockedIP(std::int64_t startIP, std::int64_t endIP) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    bool deleted = false;
    for (auto it = m_IPList.begin(); it != m_IPList.end();) {
        IPRange* const range = *it;
        if (range->m_firstIP->GetIPAddressNumber() == startIP && range->m_lastIP->GetIPAddressNumber() == endIP) {
            it = m_IPList.erase(it);
            deleted = true;
        } else {
            ++it;
        }
    }
    return deleted;
}

bool IPBlockManager::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (IPRange* range : m_IPList) {
        delete range;
    }
    m_IPList.clear();
    return true;
}
