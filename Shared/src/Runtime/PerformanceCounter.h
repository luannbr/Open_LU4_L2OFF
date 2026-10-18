#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace runtime::performance {

using Status = std::uint32_t;
using Query = void*;
using Counter = void*;

constexpr Status Success = 0;
// PDH_CSTATUS_NO_INSTANCE: the process of a per-process counter is not
// running, for example it ended during a snapshot or has not started yet.
constexpr Status NoInstance = 0xc0000bc6U;

struct CounterDefinition {
    std::u16string objectName;
    std::u16string instanceName;
    std::u16string counterName;
};

struct ProcessInstance {
    std::uint32_t processId = 0;
    std::string name;
};

struct MemoryStatus {
    std::uint32_t memoryLoad = 0;
    std::uint64_t availablePhysicalBytes = 0;
    std::uint64_t totalPhysicalBytes = 0;
    std::uint64_t processWorkingSetBytes = 0;
};

Status OpenQuery(Query& query) noexcept;
void CloseQuery(Query query) noexcept;
Status MakeCounterPath(const CounterDefinition& definition, std::u16string& path) noexcept;
Status AddCounter(Query query, const CounterDefinition& definition, const std::u16string& path, Counter& counter) noexcept;
void RemoveCounter(Counter counter) noexcept;
Status CollectQueryData(Query query) noexcept;
Status GetFormattedCounterValue(Counter counter, double& value) noexcept;
bool IsCounterNotApplicable(Counter counter) noexcept;

std::vector<ProcessInstance> EnumerateProcessInstances();
MemoryStatus ReadMemoryStatus() noexcept;
std::uint32_t CurrentProcessId() noexcept;
std::string CurrentExecutableStem();

} // namespace runtime::performance
