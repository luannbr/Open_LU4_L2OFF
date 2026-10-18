#include "Shared/src/Runtime/PerformanceCounter.h"

#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <sstream>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/task_info.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <unistd.h>
#else
#if !defined(__linux__)
#include <sys/resource.h>
#endif
#include <sys/types.h>
#include <unistd.h>
#endif

namespace runtime::performance {
namespace {

constexpr Status CounterUnavailable = 0xc0000bb8U;
constexpr Status CounterNeedsSecondSample = 0x800007d5U;
constexpr Status OutOfMemory = 0xc0000017U;

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const unsigned char leftCharacter = static_cast<unsigned char>(left[index]);
        const unsigned char rightCharacter = static_cast<unsigned char>(right[index]);
        if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) {
            return false;
        }
    }
    return true;
}

#if defined(_WIN32)

std::wstring WindowsWide(const std::u16string& value) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return std::wstring(reinterpret_cast<const wchar_t*>(value.data()), value.size());
}

#else

enum class CounterKind {
    TotalProcessorPercentage,
    ProcessProcessorPercentage,
    SystemPageFaultRate,
    SystemPageRate,
    ProcessPageFaultRate,
    ContextSwitchRate,
    ProcessorQueueLength,
    // The shipping CacheD configuration names SQL Server objects and two
    // Windows drive instances. Those objects cease to exist in the native
    // PostgreSQL build. Retaining a distinct kind keeps their deliberate
    // zero-valued report slots separate from an unknown/misspelled counter.
    NotApplicable,
    Unsupported,
};

struct PortableCounter;

struct PortableQuery {
    std::vector<PortableCounter*> counters;
};

struct PortableCounter {
    PortableQuery* owner = nullptr;
    CounterDefinition definition;
    CounterKind kind = CounterKind::Unsupported;
    std::uint32_t processId = 0;
    bool previousValid = false;
    std::uint64_t previousNumerator = 0;
    std::uint64_t previousDenominator = 0;
    std::chrono::steady_clock::time_point previousTime;
    Status status = CounterNeedsSecondSample;
    double value = 0.0;
};

struct RawCounterSample {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 0;
};

CounterKind ClassifyCounter(const CounterDefinition& definition) {
    const std::string object = LowerAscii(Utf16ToUtf8(definition.objectName));
    const std::string instance = LowerAscii(Utf16ToUtf8(definition.instanceName));
    const std::string counter = LowerAscii(Utf16ToUtf8(definition.counterName));

    // Current Cached.exe's shipped perfmon.ini contains SQL Server PDH
    // objects at indices 8..25 and names sqlservr as the process at indices
    // 3 and 7. PostgreSQL does not expose equivalent PDH objects, and its
    // multi-process model prevents a truthful one-process substitution.
    if (object.rfind("sqlserver:", 0) == 0 ||
        (object == "process" && instance == "sqlservr" && (counter == "% processor time" || counter == "page faults/sec"))) {
        return CounterKind::NotApplicable;
    }

    // Portability improvement: the shipped L2Server _Total disk counters
    // have no native backend, just like CacheD's Windows volumes "1 E:" and
    // "2 F:". Retain explicit NotApplicable slots so supported CPU/memory
    // counters can initialize. Unknown names still fail classification.
    if (object == "physicaldisk" && (instance == "_total" || instance.find(':') != std::string::npos) &&
        (counter == "avg. disk queue length" || counter == "avg. disk sec/read" || counter == "avg. disk sec/write")) {
        return CounterKind::NotApplicable;
    }

    if (object == "processor" && counter == "% processor time") {
        return CounterKind::TotalProcessorPercentage;
    }
    if (object == "process" && counter == "% processor time") {
        return CounterKind::ProcessProcessorPercentage;
    }
    if (object == "memory" && counter == "page faults/sec") {
        return CounterKind::SystemPageFaultRate;
    }
    if (object == "memory" && counter == "pages/sec") {
        return CounterKind::SystemPageRate;
    }
    if (object == "process" && counter == "page faults/sec") {
        return CounterKind::ProcessPageFaultRate;
    }
    if (object == "system" && counter == "context switches/sec") {
#if defined(__linux__)
        return CounterKind::ContextSwitchRate;
#else
        // macOS exposes per-task context-switch counts but no unprivileged
        // system-wide counter equivalent to this Windows PDH object.
        return CounterKind::NotApplicable;
#endif
    }
    if (object == "system" && counter == "processor queue length") {
#if defined(__linux__)
        return CounterKind::ProcessorQueueLength;
#else
        // getloadavg() is a time average, not Windows' instantaneous ready
        // queue. Reporting it under the target counter name was misleading.
        return CounterKind::NotApplicable;
#endif
    }
    return CounterKind::Unsupported;
}

std::uint32_t ResolveProcessId(const std::string& requestedName) {
    if (EqualsAsciiInsensitive(requestedName, CurrentExecutableStem())) {
        return CurrentProcessId();
    }

    std::vector<ProcessInstance> instances = EnumerateProcessInstances();
    std::sort(instances.begin(), instances.end(), [](const ProcessInstance& left, const ProcessInstance& right) {
        return left.processId < right.processId;
    });
    for (const ProcessInstance& instance : instances) {
        // EnumerateProcessInstances already applies PDH's foo, foo#1,
        // foo#2 naming convention after sorting by pid. Match that public
        // instance name directly. Stripping the suffix here selected the
        // first process for every duplicate instance.
        if (EqualsAsciiInsensitive(instance.name, requestedName)) {
            return instance.processId;
        }
    }
    return 0;
}

#if defined(__linux__)

bool ReadLinuxSystemCpu(RawCounterSample& sample) {
    std::ifstream input("/proc/stat");
    std::string label;
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idle = 0;
    std::uint64_t ioWait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softIrq = 0;
    std::uint64_t steal = 0;
    if (!(input >> label >> user >> nice >> system >> idle >> ioWait >> irq >> softIrq >> steal) || label != "cpu") {
        return false;
    }
    sample.numerator = user + nice + system + irq + softIrq + steal;
    sample.denominator = sample.numerator + idle + ioWait;
    return true;
}

bool ReadLinuxProcessCounters(std::uint32_t processId, std::uint64_t& cpuNanoseconds, std::uint64_t& pageFaults) {
    std::ifstream input("/proc/" + std::to_string(processId) + "/stat");
    std::string line;
    if (!std::getline(input, line)) {
        return false;
    }
    const std::size_t closeName = line.rfind(')');
    if (closeName == std::string::npos || closeName + 2 >= line.size()) {
        return false;
    }
    std::istringstream fields(line.substr(closeName + 2));
    std::vector<std::string> values;
    for (std::string value; fields >> value;) {
        values.push_back(std::move(value));
    }
    // values[0] is field 3 (state). Minflt/majflt/utime/stime are fields
    // 10/12/14/15 respectively.
    if (values.size() <= 12) {
        return false;
    }
    try {
        const std::uint64_t minorFaults = std::stoull(values[7]);
        const std::uint64_t majorFaults = std::stoull(values[9]);
        const std::uint64_t userTicks = std::stoull(values[11]);
        const std::uint64_t systemTicks = std::stoull(values[12]);
        const long ticksPerSecond = ::sysconf(_SC_CLK_TCK);
        if (ticksPerSecond <= 0) {
            return false;
        }
        cpuNanoseconds = (userTicks + systemTicks) * 1000000000ULL / static_cast<std::uint64_t>(ticksPerSecond);
        pageFaults = minorFaults + majorFaults;
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadLinuxVmCounter(std::string_view firstName, std::string_view secondName, std::uint64_t& result) {
    std::ifstream input("/proc/vmstat");
    result = 0;
    bool foundFirst = false;
    bool foundSecond = secondName.empty();
    std::string name;
    std::uint64_t value = 0;
    while (input >> name >> value) {
        if (name == firstName) {
            result += value;
            foundFirst = true;
        } else if (!secondName.empty() && name == secondName) {
            result += value;
            foundSecond = true;
        }
    }
    return foundFirst && foundSecond;
}

bool ReadLinuxContextSwitches(std::uint64_t& result) {
    std::ifstream input("/proc/stat");
    std::string name;
    std::string line;
    while (input >> name) {
        if (name == "ctxt") {
            return static_cast<bool>(input >> result);
        }
        std::getline(input, line);
    }
    return false;
}

bool ReadLinuxProcessorQueueLength(double& result) {
    std::ifstream input("/proc/stat");
    std::string name;
    std::string line;
    std::uint64_t runnable = 0;
    while (input >> name) {
        if (name == "procs_running") {
            if (!(input >> runnable)) {
                return false;
            }
            const long onlineProcessors = ::sysconf(_SC_NPROCESSORS_ONLN);
            if (onlineProcessors <= 0) {
                return false;
            }
            const std::uint64_t runningCapacity = static_cast<std::uint64_t>(onlineProcessors);
            // Linux includes executing tasks in procs_running. Windows'
            // System\\Processor Queue Length excludes threads already
            // running, so keep only the excess over online processors.
            result = static_cast<double>(runnable > runningCapacity ? runnable - runningCapacity : 0);
            return true;
        }
        std::getline(input, line);
    }
    return false;
}

#elif defined(__APPLE__)

bool ReadDarwinSystemCpu(RawCounterSample& sample) {
    natural_t processorCount = 0;
    processor_info_array_t information = nullptr;
    mach_msg_type_number_t informationCount = 0;
    const kern_return_t result =
        ::host_processor_info(::mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &processorCount, &information, &informationCount);
    if (result != KERN_SUCCESS || !information) {
        return false;
    }
    std::uint64_t busy = 0;
    std::uint64_t total = 0;
    for (natural_t processor = 0; processor < processorCount; ++processor) {
        const auto* ticks = reinterpret_cast<processor_cpu_load_info_t>(information) + processor;
        const std::uint64_t processorBusy =
            ticks->cpu_ticks[CPU_STATE_USER] + ticks->cpu_ticks[CPU_STATE_SYSTEM] + ticks->cpu_ticks[CPU_STATE_NICE];
        busy += processorBusy;
        total += processorBusy + ticks->cpu_ticks[CPU_STATE_IDLE];
    }
    sample.numerator = busy;
    sample.denominator = total;
    ::vm_deallocate(
        ::mach_task_self(), reinterpret_cast<vm_address_t>(information),
        static_cast<vm_size_t>(informationCount * sizeof(integer_t))
    );
    return total != 0;
}

bool ReadDarwinProcessCounters(std::uint32_t processId, std::uint64_t& cpuNanoseconds, std::uint64_t& pageFaults) {
    rusage_info_v2 usage{};
    if (::proc_pid_rusage(static_cast<int>(processId), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&usage)) != 0) {
        return false;
    }
    cpuNanoseconds = usage.ri_user_time + usage.ri_system_time;
    pageFaults = usage.ri_pageins;
    return true;
}

bool ReadDarwinCurrentTaskFaults(std::uint32_t processId, std::uint64_t& pageFaults) {
    if (processId != CurrentProcessId()) {
        return false;
    }
    task_events_info_data_t events{};
    mach_msg_type_number_t count = TASK_EVENTS_INFO_COUNT;
    if (::task_info(::mach_task_self(), TASK_EVENTS_INFO, reinterpret_cast<task_info_t>(&events), &count) != KERN_SUCCESS) {
        return false;
    }
    // TASK_EVENTS_INFO::faults is the total task page-fault counter.
    // proc_pid_rusage::ri_pageins records only actual page-ins and therefore
    // undercounts Windows Process\\Page Faults/sec.
    pageFaults = static_cast<std::uint32_t>(events.faults);
    return true;
}

bool ReadDarwinVmCounters(std::uint64_t& faults, std::uint64_t& pages) {
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (::host_statistics64(::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count) !=
        KERN_SUCCESS) {
        return false;
    }
    faults = statistics.faults;
    pages = statistics.pageins + statistics.pageouts;
    return true;
}

#endif

bool ReadRawCounter(PortableCounter& counter, RawCounterSample& sample) {
    switch (counter.kind) {
        case CounterKind::TotalProcessorPercentage:
#if defined(__linux__)
            return ReadLinuxSystemCpu(sample);
#elif defined(__APPLE__)
            return ReadDarwinSystemCpu(sample);
#else
            return false;
#endif

        case CounterKind::ProcessProcessorPercentage:
        case CounterKind::ProcessPageFaultRate: {
            if (counter.processId == 0) {
                counter.processId = ResolveProcessId(Utf16ToUtf8(counter.definition.instanceName));
            }
            if (counter.processId == 0) {
                return false;
            }
            std::uint64_t cpu = 0;
            std::uint64_t faults = 0;
#if defined(__linux__)
            if (!ReadLinuxProcessCounters(counter.processId, cpu, faults)) {
                return false;
            }
#elif defined(__APPLE__)
            if (!ReadDarwinProcessCounters(counter.processId, cpu, faults)) {
                return false;
            }
            if (counter.kind == CounterKind::ProcessPageFaultRate &&
                !ReadDarwinCurrentTaskFaults(counter.processId, faults)) {
                return false;
            }
#else
            return false;
#endif
            sample.numerator = counter.kind == CounterKind::ProcessProcessorPercentage ? cpu : faults;
            return true;
        }

        case CounterKind::SystemPageFaultRate:
#if defined(__linux__)
            return ReadLinuxVmCounter("pgfault", std::string_view{}, sample.numerator);
#elif defined(__APPLE__)
        {
            std::uint64_t pages = 0;
            return ReadDarwinVmCounters(sample.numerator, pages);
        }
#else
            return false;
#endif

        case CounterKind::SystemPageRate:
#if defined(__linux__)
            return ReadLinuxVmCounter("pgpgin", "pgpgout", sample.numerator);
#elif defined(__APPLE__)
        {
            std::uint64_t faults = 0;
            return ReadDarwinVmCounters(faults, sample.numerator);
        }
#else
            return false;
#endif

        case CounterKind::ContextSwitchRate:
#if defined(__linux__)
            return ReadLinuxContextSwitches(sample.numerator);
#else
            return false;
#endif

        case CounterKind::ProcessorQueueLength: {
#if defined(__linux__)
            double queueLength = 0.0;
            if (!ReadLinuxProcessorQueueLength(queueLength)) {
                return false;
            }
            counter.value = queueLength;
            counter.status = Success;
            return true;
#else
            return false;
#endif
        }

        case CounterKind::NotApplicable:
        case CounterKind::Unsupported:
            return false;
    }
    return false;
}

void UpdateCounter(PortableCounter& counter, std::chrono::steady_clock::time_point now) {
    if (counter.kind == CounterKind::NotApplicable) {
        counter.value = 0.0;
        counter.status = Success;
        return;
    }
    if (counter.kind == CounterKind::Unsupported) {
        counter.status = CounterUnavailable;
        return;
    }
    if (counter.kind == CounterKind::ProcessorQueueLength) {
        RawCounterSample ignored;
        if (!ReadRawCounter(counter, ignored)) {
            counter.status = CounterUnavailable;
        }
        return;
    }

    RawCounterSample sample;
    // Reset the sample after a failed read. The next read must find the process again.
    // Do not compare samples from different process IDs.
    if (!ReadRawCounter(counter, sample)) {
        if (counter.kind == CounterKind::ProcessProcessorPercentage || counter.kind == CounterKind::ProcessPageFaultRate) {
            counter.processId = 0;
            counter.previousValid = false;
        }
        counter.status = CounterUnavailable;
        return;
    }
    if (!counter.previousValid) {
        counter.previousValid = true;
        counter.previousNumerator = sample.numerator;
        counter.previousDenominator = sample.denominator;
        counter.previousTime = now;
        counter.status = CounterNeedsSecondSample;
        return;
    }

    const double elapsedSeconds = std::chrono::duration<double>(now - counter.previousTime).count();
    const std::uint64_t numeratorDelta = sample.numerator - counter.previousNumerator;
    const std::uint64_t denominatorDelta = sample.denominator - counter.previousDenominator;
    counter.previousNumerator = sample.numerator;
    counter.previousDenominator = sample.denominator;
    counter.previousTime = now;
    if (!(elapsedSeconds > 0.0)) {
        counter.status = CounterNeedsSecondSample;
        return;
    }

    switch (counter.kind) {
        case CounterKind::TotalProcessorPercentage:
            if (denominatorDelta == 0) {
                counter.status = CounterNeedsSecondSample;
                return;
            }
            counter.value = 100.0 * static_cast<double>(numeratorDelta) / static_cast<double>(denominatorDelta);
            break;
        case CounterKind::ProcessProcessorPercentage:
            counter.value = static_cast<double>(numeratorDelta) / (elapsedSeconds * 10000000.0);
            break;
        case CounterKind::SystemPageFaultRate:
        case CounterKind::SystemPageRate:
        case CounterKind::ProcessPageFaultRate:
        case CounterKind::ContextSwitchRate:
            counter.value = static_cast<double>(numeratorDelta) / elapsedSeconds;
            break;
        case CounterKind::ProcessorQueueLength:
        case CounterKind::NotApplicable:
        case CounterKind::Unsupported:
            break;
    }
    counter.status = Success;
}

#endif // !_WIN32

} // namespace

Status OpenQuery(Query& query) noexcept {
#if defined(_WIN32)
    PDH_HQUERY nativeQuery = nullptr;
    const PDH_STATUS status = ::PdhOpenQueryW(nullptr, 0, &nativeQuery);
    query = nativeQuery;
    return static_cast<Status>(status);
#else
    auto* portableQuery = new (std::nothrow) PortableQuery;
    if (!portableQuery) {
        return OutOfMemory;
    }
    query = portableQuery;
    return Success;
#endif
}

void CloseQuery(Query query) noexcept {
    if (!query) {
        return;
    }
#if defined(_WIN32)
    ::PdhCloseQuery(static_cast<PDH_HQUERY>(query));
#else
    auto* portableQuery = static_cast<PortableQuery*>(query);
    for (PortableCounter* counter : portableQuery->counters) {
        delete counter;
    }
    delete portableQuery;
#endif
}

Status MakeCounterPath(const CounterDefinition& definition, std::u16string& path) noexcept {
    try {
#if defined(_WIN32)
        const std::wstring objectName = WindowsWide(definition.objectName);
        const std::wstring instanceName = WindowsWide(definition.instanceName);
        const std::wstring counterName = WindowsWide(definition.counterName);
        PDH_COUNTER_PATH_ELEMENTS_W elements{};
        elements.szObjectName = const_cast<wchar_t*>(objectName.c_str());
        elements.szInstanceName = instanceName.empty() ? nullptr : const_cast<wchar_t*>(instanceName.c_str());
        elements.szCounterName = const_cast<wchar_t*>(counterName.c_str());
        elements.dwInstanceIndex = 0;

        DWORD characterCount = 0;
        PDH_STATUS status = ::PdhMakeCounterPathW(&elements, nullptr, &characterCount, 0);
        if (status != PDH_MORE_DATA || characterCount == 0) {
            return static_cast<Status>(status);
        }
        std::vector<wchar_t> buffer(characterCount);
        status = ::PdhMakeCounterPathW(&elements, buffer.data(), &characterCount, 0);
        if (status != ERROR_SUCCESS) {
            return static_cast<Status>(status);
        }
        path.assign(reinterpret_cast<const char16_t*>(buffer.data()));
#else
        path.clear();
        path.push_back(u'\\');
        path.append(definition.objectName);
        if (!definition.instanceName.empty()) {
            path.push_back(u'(');
            path.append(definition.instanceName);
            path.push_back(u')');
        }
        path.push_back(u'\\');
        path.append(definition.counterName);
#endif
        return Success;
    } catch (...) {
        path.clear();
        return OutOfMemory;
    }
}

Status AddCounter(Query query, const CounterDefinition& definition, const std::u16string& path, Counter& counter) noexcept {
    counter = nullptr;
    if (!query) {
        return CounterUnavailable;
    }
#if defined(_WIN32)
    try {
        const std::wstring nativePath = WindowsWide(path);
        PDH_HCOUNTER nativeCounter = nullptr;
        const PDH_STATUS status = ::PdhAddCounterW(static_cast<PDH_HQUERY>(query), nativePath.c_str(), 0, &nativeCounter);
        counter = nativeCounter;
        return static_cast<Status>(status);
    } catch (...) {
        counter = nullptr;
        return OutOfMemory;
    }
#else
    try {
        std::unique_ptr<PortableCounter> portableCounter(new (std::nothrow) PortableCounter);
        if (!portableCounter) {
            return OutOfMemory;
        }
        portableCounter->owner = static_cast<PortableQuery*>(query);
        portableCounter->definition = definition;
        portableCounter->kind = ClassifyCounter(definition);
        if (portableCounter->kind == CounterKind::Unsupported) {
            // PdhAddCounterW rejects a path which does not identify a real
            // counter. Keep that distinction for unknown portable config.
            // only classified Windows/MSSQL slots are admitted
            // as deliberate zero-valued platform replacements.
            return CounterUnavailable;
        }
        if (portableCounter->kind == CounterKind::NotApplicable) {
            portableCounter->status = Success;
            portableCounter->value = 0.0;
        }
        if (portableCounter->kind == CounterKind::ProcessProcessorPercentage ||
            portableCounter->kind == CounterKind::ProcessPageFaultRate) {
            portableCounter->processId = ResolveProcessId(Utf16ToUtf8(definition.instanceName));
        }
        portableCounter->owner->counters.push_back(portableCounter.get());
        counter = portableCounter.release();
        return Success;
    } catch (...) {
        counter = nullptr;
        return OutOfMemory;
    }
#endif
}

void RemoveCounter(Counter counter) noexcept {
    if (!counter) {
        return;
    }
#if defined(_WIN32)
    ::PdhRemoveCounter(static_cast<PDH_HCOUNTER>(counter));
#else
    auto* portableCounter = static_cast<PortableCounter*>(counter);
    if (portableCounter->owner) {
        auto& counters = portableCounter->owner->counters;
        counters.erase(std::remove(counters.begin(), counters.end(), portableCounter), counters.end());
    }
    delete portableCounter;
#endif
}

Status CollectQueryData(Query query) noexcept {
    if (!query) {
        return CounterUnavailable;
    }
#if defined(_WIN32)
    return static_cast<Status>(::PdhCollectQueryData(static_cast<PDH_HQUERY>(query)));
#else
    const auto now = std::chrono::steady_clock::now();
    for (PortableCounter* counter : static_cast<PortableQuery*>(query)->counters) {
        if (counter) {
            UpdateCounter(*counter, now);
        }
    }
    return Success;
#endif
}

Status GetFormattedCounterValue(Counter counter, double& value) noexcept {
    if (!counter) {
        return CounterUnavailable;
    }
#if defined(_WIN32)
    PDH_FMT_COUNTERVALUE formatted{};
    const PDH_STATUS status = ::PdhGetFormattedCounterValue(
        static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &formatted
    );
    if (status == ERROR_SUCCESS) {
        value = formatted.doubleValue;
    }
    return static_cast<Status>(status);
#else
    const auto* portableCounter = static_cast<const PortableCounter*>(counter);
    if (portableCounter->status == Success) {
        value = portableCounter->value;
    }
    return portableCounter->status;
#endif
}

bool IsCounterNotApplicable(Counter counter) noexcept {
#if defined(_WIN32)
    return false;
#else
    return counter != nullptr && static_cast<const PortableCounter*>(counter)->kind == CounterKind::NotApplicable;
#endif
}

std::vector<ProcessInstance> EnumerateProcessInstances() {
    std::vector<ProcessInstance> instances;
#if defined(_WIN32)
    DWORD counterCharacters = 0;
    DWORD instanceCharacters = 0;
    PDH_STATUS status = ::PdhEnumObjectItemsW(
        nullptr, nullptr, L"Process", nullptr, &counterCharacters, nullptr, &instanceCharacters, PERF_DETAIL_WIZARD, 0
    );
    if (status != PDH_MORE_DATA || instanceCharacters == 0) {
        return instances;
    }
    std::vector<wchar_t> counterNames(counterCharacters);
    std::vector<wchar_t> instanceNames(instanceCharacters);
    status = ::PdhEnumObjectItemsW(
        nullptr, nullptr, L"Process", counterNames.data(), &counterCharacters, instanceNames.data(), &instanceCharacters,
        PERF_DETAIL_WIZARD, 0
    );
    if (status != ERROR_SUCCESS) {
        return instances;
    }
    for (const wchar_t* name = instanceNames.data(); *name != L'\0'; name += std::wcslen(name) + 1) {
        const std::wstring_view wide(name);
        instances.push_back(
            ProcessInstance{0, Utf16ToUtf8(std::u16string_view(reinterpret_cast<const char16_t*>(wide.data()), wide.size()))}
        );
    }
#elif defined(__APPLE__)
    const int bytes = ::proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0) {
        return instances;
    }
    std::vector<pid_t> processIds(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 1);
    const int filled =
        ::proc_listpids(PROC_ALL_PIDS, 0, processIds.data(), static_cast<int>(processIds.size() * sizeof(pid_t)));
    if (filled <= 0) {
        return instances;
    }
    processIds.resize(static_cast<std::size_t>(filled) / sizeof(pid_t));
    for (const pid_t processId : processIds) {
        if (processId <= 0) {
            continue;
        }
        char name[PROC_PIDPATHINFO_MAXSIZE]{};
        if (::proc_name(processId, name, sizeof(name)) <= 0) {
            continue;
        }
        instances.push_back(ProcessInstance{static_cast<std::uint32_t>(processId), name});
    }
#elif defined(__linux__)
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", error)) {
        if (error) {
            break;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.empty() || !std::all_of(filename.begin(), filename.end(), [](unsigned char character) {
                return std::isdigit(character) != 0;
            })) {
            continue;
        }
        std::ifstream nameFile(entry.path() / "comm");
        std::string name;
        if (!std::getline(nameFile, name) || name.empty()) {
            continue;
        }
        try {
            instances.push_back(ProcessInstance{static_cast<std::uint32_t>(std::stoul(filename)), std::move(name)});
        } catch (...) {
        }
    }
#else
    instances.push_back(ProcessInstance{CurrentProcessId(), CurrentExecutableStem()});
#endif

#if !defined(_WIN32)
    std::sort(instances.begin(), instances.end(), [](const ProcessInstance& left, const ProcessInstance& right) {
        return left.processId < right.processId;
    });
    std::unordered_map<std::string, std::size_t> occurrences;
    for (ProcessInstance& instance : instances) {
        const std::string key = LowerAscii(instance.name);
        const std::size_t occurrence = occurrences[key]++;
        if (occurrence != 0) {
            instance.name.append("#");
            instance.name.append(std::to_string(occurrence));
        }
    }
#endif
    return instances;
}

MemoryStatus ReadMemoryStatus() noexcept {
    MemoryStatus result;
#if defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory)) {
        result.memoryLoad = memory.dwMemoryLoad;
        result.availablePhysicalBytes = memory.ullAvailPhys;
        result.totalPhysicalBytes = memory.ullTotalPhys;
    }
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    HANDLE process = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ::GetCurrentProcessId());
    if (process) {
        if (::GetProcessMemoryInfo(process, &counters, sizeof(counters))) {
            result.processWorkingSetBytes = counters.WorkingSetSize;
        }
        ::CloseHandle(process);
    }
#elif defined(__APPLE__)
    std::uint64_t total = 0;
    std::size_t totalSize = sizeof(total);
    if (::sysctlbyname("hw.memsize", &total, &totalSize, nullptr, 0) == 0) {
        result.totalPhysicalBytes = total;
    }
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (::host_statistics64(::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count) ==
        KERN_SUCCESS) {
        const std::uint64_t availablePages =
            statistics.free_count + statistics.inactive_count + statistics.speculative_count;
        result.availablePhysicalBytes = availablePages * static_cast<std::uint64_t>(vm_kernel_page_size);
    }
    mach_task_basic_info_data_t taskInformation{};
    mach_msg_type_number_t taskInformationCount = MACH_TASK_BASIC_INFO_COUNT;
    if (::task_info(
            ::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&taskInformation), &taskInformationCount
        ) == KERN_SUCCESS) {
        result.processWorkingSetBytes = taskInformation.resident_size;
    }
#elif defined(__linux__)
    std::ifstream memory("/proc/meminfo");
    std::string name;
    std::uint64_t value = 0;
    bool foundTotal = false;
    bool foundAvailable = false;
    while (memory >> name >> value) {
        std::string remainder;
        std::getline(memory, remainder);
        if (name == "MemTotal:") {
            result.totalPhysicalBytes = value * 1024ULL;
            foundTotal = true;
        } else if (name == "MemAvailable:") {
            result.availablePhysicalBytes = value * 1024ULL;
            foundAvailable = true;
        }
        if (foundTotal && foundAvailable) {
            break;
        }
    }
    std::ifstream statm("/proc/self/statm");
    std::uint64_t pages = 0;
    std::uint64_t residentPages = 0;
    if (statm >> pages >> residentPages) {
        const long pageSize = ::sysconf(_SC_PAGESIZE);
        if (pageSize > 0) {
            result.processWorkingSetBytes = residentPages * static_cast<std::uint64_t>(pageSize);
        }
    }
#else
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long availablePages = ::sysconf(_SC_AVPHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) {
        result.totalPhysicalBytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
    }
    if (availablePages > 0 && pageSize > 0) {
        result.availablePhysicalBytes = static_cast<std::uint64_t>(availablePages) * static_cast<std::uint64_t>(pageSize);
    }
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) == 0) {
        result.processWorkingSetBytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
    }
#endif
    if (result.totalPhysicalBytes != 0) {
        const std::uint64_t used = result.totalPhysicalBytes > result.availablePhysicalBytes
            ? result.totalPhysicalBytes - result.availablePhysicalBytes
            : 0;
        result.memoryLoad = static_cast<std::uint32_t>(std::min<std::uint64_t>(100, used * 100 / result.totalPhysicalBytes));
    }
    return result;
}

std::uint32_t CurrentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::string CurrentExecutableStem() {
    std::filesystem::path executable;
#if defined(_WIN32)
    std::vector<wchar_t> path(1024);
    DWORD length = ::GetProcessImageFileNameW(::GetCurrentProcess(), path.data(), static_cast<DWORD>(path.size()));
    if (length != 0) {
        executable = std::wstring(path.data(), length);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size + 1, '\0');
    if (::_NSGetExecutablePath(path.data(), &size) == 0) {
        executable = path.data();
    }
#elif defined(__linux__)
    std::vector<char> path(4096, '\0');
    const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length > 0) {
        path[static_cast<std::size_t>(length)] = '\0';
        executable = path.data();
    }
#endif
    return executable.empty() ? std::string("Cached") : executable.stem().string();
}

} // namespace runtime::performance
