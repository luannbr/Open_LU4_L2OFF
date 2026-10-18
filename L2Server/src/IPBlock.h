#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class IPAddress {
public:
    explicit IPAddress(std::int64_t address);
    IPAddress(unsigned int part1, unsigned int part2, unsigned int part3, unsigned int part4);
    ~IPAddress() = default;

    int GetIPAddressNumber();

private:
    [[maybe_unused]] unsigned int m_ipPart1; // +0x00
    [[maybe_unused]] unsigned int m_ipPart2; // +0x04
    [[maybe_unused]] unsigned int m_ipPart3; // +0x08
    [[maybe_unused]] unsigned int m_ipPart4; // +0x0c
    std::int64_t m_nIPAddress; // +0x10
};


class IPRange {
public:
    IPRange(std::int64_t startIP, std::int64_t endIP);
    ~IPRange();

    bool IsInRange(IPAddress& address);

private:
    friend class IPBlockManager;

    IPAddress* m_firstIP; // +0x00
    IPAddress* m_lastIP; // +0x08
};


class IPBlockManager : public CIOObject {
public:
    IPBlockManager();
    ~IPBlockManager() override;

    static IPBlockManager& inst() {
        static IPBlockManager instance;
        return instance;
    }

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    bool Init();
    bool Load(bool loadAll);
    bool IsBlockedIP(IPAddress& address);
    bool AddBlockedIP(IPRange* range);
    bool AddBlockedIP(std::int64_t startIP, std::int64_t endIP);
    bool DeleteBlockedIP(std::int64_t startIP, std::int64_t endIP);
    bool Clear();

private:
    L2_DECLARE_OBJECT_REGISTRATION(IPBlockManager)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(IPBlockManager)

    std::list<IPRange*> m_IPList; // +0x18
    CYieldLock m_lock; // +0x30
};
