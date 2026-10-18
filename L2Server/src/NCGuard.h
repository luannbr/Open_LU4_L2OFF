#pragma once

#include "NCGuard/include/NCGuard.h"

#include "Shared/src/CriticalSectionStorage.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct NCGuardQueueEntry {
    std::uint32_t uniqueId;
    std::array<std::byte, 4> eventTypeLane;
    void* data;
    std::uint32_t size;
    std::array<std::byte, 4> sizeHighLane;
};

struct NCGuardPenaltyPattern {
    std::uint32_t ncgId;
    std::int32_t mode;
    std::array<std::uint32_t, 4> fileHash;
};


// Exact current NCGuard.cpp physical owner. The raw critical-section members
// are material: the target initializes both directly and does
// not delete either one from the process-lifetime singleton destructor.
class CNCGuardManager final : public CIOObject, public Singleton<CNCGuardManager> {
public:

    CNCGuardManager();
    ~CNCGuardManager() override;

    void Init();
    void TimerExpired(int id) override;
    bool LoadPenaltyPattern();

private:
    friend int NCGuardSendHandler(std::uint64_t uniqueId, void* data, std::uint32_t size);
    friend int NCGuardEventHandler(
        std::uint64_t uniqueId, NCG_EVENT_TYPE eventType, void* data, std::uint32_t size, std::uint32_t flags
    );

    void PushLogQueue(const NCGuardQueueEntry& entry);
    void PushMessageQueue(const NCGuardQueueEntry& entry);
    void ProcessMessageQueue();
    void ProcessLogQueue();
    void WriteNCGuardLog(std::uint32_t uniqueId, NCG_EVENT_TYPE eventType, void* data);

    l2portable_detail::CriticalSectionStorage m_logCriticalSection; // +0x020
    std::deque<NCGuardQueueEntry> m_logQueue; // +0x048
    l2portable_detail::CriticalSectionStorage m_messageCriticalSection; // +0x078
    std::deque<NCGuardQueueEntry> m_messageQueue; // +0x0a0
    bool m_loaded; // +0x0d0
    bool m_use; // +0x0d1
    std::array<char16_t, 128> m_version; // +0x0d2
    [[maybe_unused]] std::array<std::byte, 6> m_alignment01d2To01d8;
    std::vector<NCGuardPenaltyPattern> m_penaltyPatterns; // +0x1d8
    [[maybe_unused]] std::array<std::byte, 8> m_allocationTail; // +0x1f0
};

int NCGuardSendHandler(std::uint64_t uniqueId, void* data, std::uint32_t size);
int NCGuardEventHandler(
    std::uint64_t uniqueId, NCG_EVENT_TYPE eventType, void* data, std::uint32_t size, std::uint32_t flags
);

extern bool g_NCGuardServerStarted;
