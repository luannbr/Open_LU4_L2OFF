#include "Shared/src/PerfMon.h"

#include "Shared/src/Runtime/PerformanceCounter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/net/Config.h"

#if defined(L2_BUILD_CACHED)
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "User.h"
#elif defined(L2_BUILD_L2SERVER)
#include "Default.h"
#include "L2StatusWnd.h"
#include "Party.h"
#include "PartyRoomRegistry.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "UserSocket.h"

#include "Shared/src/IOC.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

#if defined(L2_BUILD_L2SERVER)
// Current L2Server.exe 0x20021010. TotalChecker's map assigns this signed
// 32-bit hour cache to perfmon.obj.
std::int32_t g_nLastHour = 0;
#endif

namespace l2npc {
namespace {

std::u16string FixedName(const std::array<char16_t, 255>& value) {
    const auto end = std::find(value.begin(), value.end(), u'\0');
    return std::u16string(value.begin(), end);
}

void CopyName(std::array<char16_t, 255>& destination, const std::string& value) {
    destination.fill(u'\0');
    const std::u16string converted = Utf8ToUtf16(value);
    std::copy_n(converted.begin(), std::min(converted.size(), destination.size() - 1), destination.begin());
}

runtime::performance::CounterDefinition CounterDefinition(const _Perf_& source) {
    return {FixedName(source.szObjectName), FixedName(source.szInstanceName), FixedName(source.szCounterName)};
}

void StoreFormattedCounterValue(
    TargetPdhFormattedCounterValue& target, runtime::performance::Status status, double value
) {
    target.CStatus = status;
    std::memcpy(&target.rawValue, &value, sizeof(value));
}

unsigned LogicalProcessorCount() {
    const unsigned count = std::thread::hardware_concurrency();
    return count == 0 ? 1U : count;
}


// Keep one bounded set of examples per diagnostic snapshot. Counter values
// and sampling decisions remain independent of this reporting state.
void AppendUnavailableCounterExample(
    std::string& examples, int unavailableCount, int index, const std::array<char16_t, 255>& instance, std::uint32_t status
) {
    if (unavailableCount > 3) {
        return;
    }
    if (!examples.empty()) {
        examples += "; ";
    }
    char statusText[11]{};
    std::snprintf(statusText, sizeof(statusText), "0x%08x", static_cast<unsigned>(status));
    examples += "#" + std::to_string(index) + " \"" + Utf16ToUtf8(FixedName(instance)) + "\" (" + statusText + ")";
}

int TargetDoubleToInt(double value) {
    if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min()) ||
        value > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value);
}

#if defined(L2_BUILD_CACHED)
double CounterDouble(const CPerfMon& monitor, int index) {
    return monitor.m_pPerf && index >= 0 && index < monitor.m_nPerfCount ? monitor.m_pPerf[index].nPerval : 0.0;
}

int CounterInt(const CPerfMon& monitor, int index) { return TargetDoubleToInt(CounterDouble(monitor, index)); }
#endif

#if defined(L2_BUILD_L2SERVER)
double L2ServerCounterDouble(const CPerfMon& monitor, const int index) {
    return monitor.m_nPerfCount > index ? monitor.m_pPerf[index].nPerval : 0.0;
}

int L2ServerCounterInt(const CPerfMon& monitor, const int index) {
    return TargetDoubleToInt(L2ServerCounterDouble(monitor, index));
}
#endif

} // namespace

#if defined(L2_BUILD_L2SERVER)
// Current pointer 0x15dccb0 -> UTF-16 literal 0x10e2920.
const wchar_t* CPerfMon::s_File = L".\\perfmon.ini";
#endif

// Current Cached.exe 0x004ff04c. Current L2NPC.exe 0x005094f8.
CInstantPerfMon::CInstantPerfMon()
#if defined(L2_BUILD_L2SERVER)
    :
    m_pInfo(nullptr), m_pCounter(nullptr), m_nInstanceCount(0) {
#else
    :
    m_pInfo(nullptr), m_hQuery(nullptr), m_pCounter(nullptr), m_counterValue{}, m_nInstanceCount(0), m_tailPadding{} {
#endif
    Init();
}

// Current Cached.exe 0x004ff070. Current L2NPC.exe 0x0050951c.
CInstantPerfMon::~CInstantPerfMon() {
    PostProcess();
    runtime::performance::CloseQuery(m_hQuery);
    m_hQuery = nullptr;
}

// Current Cached.exe 0x004fedf0. Current L2NPC.exe 0x00509450.
bool CInstantPerfMon::Init() {
    const runtime::performance::Status status = runtime::performance::OpenQuery(m_hQuery);
    if (status != runtime::performance::Success) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"PdhOpenQuery failed %08x",
        //     status)
        // Log.Add(LOG_ERROR,
        //     "[performance] Cannot open the process CPU query (status 0x%08x). Process CPU sampling is unavailable.",
        //     static_cast<unsigned>(status))
        L2_LOG_ERROR(
            Log, "[performance] Cannot open the process CPU query (status 0x%08x); process CPU sampling is unavailable.",
            static_cast<unsigned>(status)
        );
    }
    return status == runtime::performance::Success;
}

// Current Cached.exe 0x004fee2c. Current L2NPC.exe 0x0050948c.
void CInstantPerfMon::PostProcess() {
    if (m_pCounter != nullptr) {
        for (int index = 0; index < m_nInstanceCount; ++index) {
            runtime::performance::RemoveCounter(m_pCounter[index]);
        }
        std::free(m_pCounter);
        m_pCounter = nullptr;
    }
    if (m_pInfo != nullptr) {
        std::free(m_pInfo);
        m_pInfo = nullptr;
    }
}

// Current Cached.exe 0x004ff3a4. Current L2NPC.exe 0x00509540.
CPerfMon::CPerfMon()
#if defined(L2_BUILD_L2SERVER)
{
    // Current L2Server.exe 0x00953310 constructs the trailing instant
    // monitor first, then writes precisely these lanes. The four alignment
    // bytes in PDH_FMT_COUNTERVALUE remain indeterminate.
    m_nCheckPerfMon = 0;
    m_nMemusage = 0;
    m_dwProcessId = 0;
    m_nMemUse = 0;
    m_nFreePhyMem = 0;
    m_nTotPhyMem = 0;
    m_nProcessMem = 0;
    m_pPerf = nullptr;
    m_nInterval = 0;
    m_nPerfCount = 0;
    m_pHCounter = nullptr;
    m_bQuery = false;
    m_nLastCheck = runtime::crt::Time32Now();
    m_dwErrorTick = runtime::GetTickCount();
    m_hQuery = nullptr;
    m_nCPUIsolationLevel = 0;
    m_counterValue.CStatus = 0;
    m_counterValue.rawValue = 0;
}
#else
    :
    m_pPerf(nullptr),
    m_pHCounter(nullptr),
    m_hQuery(nullptr),
    m_counterValue{},
    m_nCheckPerfMon(0),
    m_nMemusage(0),
    m_nInterval(0),
    m_nPerfCount(0),
    m_bQuery(false),
    m_queryPadding{},
    m_nLastCheck(runtime::crt::Time32Now()),
    m_nCPUIsolationLevel(0),
    m_dwProcessId(0),
    m_nMemUse(0),
    m_nFreePhyMem(0),
    m_nTotPhyMem(0),
    m_nProcessMem(0),
    m_dwErrorTick(runtime::GetTickCount()),
    m_instantAlignmentPadding{} {
}
#endif

// Current Cached.exe 0x004ff418. Current L2NPC.exe 0x005095b4.
CPerfMon::~CPerfMon() {
    if (m_bQuery && m_nPerfCount > 0) {
        Delete();
    }
#if defined(L2_BUILD_L2SERVER)
    if (m_pPerf != nullptr) {
        std::free(m_pPerf);
    }
    if (m_pHCounter != nullptr) {
        std::free(m_pHCounter);
    }
#else
    std::free(m_pPerf);
    std::free(m_pHCounter);
#endif
}

// Current Cached.exe 0x004fe7f4. Current L2NPC.exe 0x00509400.
bool CPerfMon::Delete() {
    for (int index = 0; index < m_nPerfCount; ++index) {
        runtime::performance::RemoveCounter(m_pHCounter ? m_pHCounter[index] : nullptr);
#if !defined(L2_BUILD_L2SERVER)
        if (m_pHCounter) {
            m_pHCounter[index] = nullptr;
        }
#endif
    }
    runtime::performance::CloseQuery(m_hQuery);
#if !defined(L2_BUILD_L2SERVER)
    m_hQuery = nullptr;
#endif
    return true;
}

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)

// Current Cached.exe 0x004ff094.
bool CInstantPerfMon::CollectInstanceList() {
    const std::vector<runtime::performance::ProcessInstance> instances =
        runtime::performance::EnumerateProcessInstances();
    m_nInstanceCount =
        static_cast<int>(std::min<std::size_t>(instances.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    if (m_nInstanceCount == 0) {
        return true;
    }

#if defined(L2_BUILD_L2SERVER)
    m_pCounter = static_cast<void**>(std::malloc(static_cast<std::size_t>(m_nInstanceCount) * sizeof(void*)));
#else
    m_pCounter = static_cast<void**>(std::calloc(static_cast<std::size_t>(m_nInstanceCount), sizeof(void*)));
#endif
    if (!m_pCounter) {
        return false;
    }
    m_pInfo =
#if defined(L2_BUILD_L2SERVER)
        static_cast<_Perf_*>(std::malloc(static_cast<std::size_t>(m_nInstanceCount) * sizeof(_Perf_)));
#else
        static_cast<_Perf_*>(std::calloc(static_cast<std::size_t>(m_nInstanceCount), sizeof(_Perf_)));
#endif
    if (!m_pInfo) {
#if !defined(L2_BUILD_L2SERVER)
        PostProcess();
#endif
        return false;
    }

    for (int index = 0; index < m_nInstanceCount; ++index) {
        CopyName(m_pInfo[index].szObjectName, "Process");
        CopyName(m_pInfo[index].szCounterName, "% Processor Time");
        CopyName(m_pInfo[index].szInstanceName, instances[static_cast<std::size_t>(index)].name);

        const auto definition = CounterDefinition(m_pInfo[index]);
        std::u16string path;
        runtime::performance::Status status = runtime::performance::MakeCounterPath(definition, path);
        if (status != runtime::performance::Success) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"PdhMakeCounterPath failed %08x",
            //     status)
            // Log.Add(LOG_ERROR,
            //     "[performance] Cannot build the CPU counter path for process instance \"%s\" (status 0x%08x). Snapshot
            //     setup aborted.", Utf16ToUtf8(definition.instanceName).c_str(), static_cast<unsigned>(status))
            L2_LOG_ERROR(
                Log,
                "[performance] Cannot build the CPU counter path for process instance \"%s\" (status 0x%08x); snapshot "
                "setup aborted.",
                Utf16ToUtf8(definition.instanceName).c_str(), static_cast<unsigned>(status)
            );
            PostProcess();
            return false;
        }
        status = runtime::performance::AddCounter(m_hQuery, definition, path, m_pCounter[index]);
        if (status != runtime::performance::Success) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"PdhAddCounter failed %08x",
            //     status)
            // Log.Add(LOG_ERROR,
            //     "[performance] Cannot register process CPU counter \"%s\" (status 0x%08x). Snapshot setup aborted.",
            //     Utf16ToUtf8(path).c_str(), static_cast<unsigned>(status))
            L2_LOG_ERROR(
                Log, "[performance] Cannot register process CPU counter \"%s\" (status 0x%08x); snapshot setup aborted.",
                Utf16ToUtf8(path).c_str(), static_cast<unsigned>(status)
            );
            PostProcess();
            return false;
        }
    }
    return true;
}

// Current Cached.exe 0x004fee98.
bool CInstantPerfMon::CollectPerformanceCounter() {
    runtime::performance::Status status = runtime::performance::CollectQueryData(m_hQuery);
    if (status != runtime::performance::Success) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"PdhCollectQueryData failed %08x",
        //     status)
        // Log.Add(LOG_WARN,
        //     "[performance] Process CPU snapshot unavailable: initial sampling failed (status 0x%08x).",
        //     static_cast<unsigned>(status))
        L2_LOG_WARN(
            Log, "[performance] Process CPU snapshot unavailable: initial sampling failed (status 0x%08x).",
            static_cast<unsigned>(status)
        );
        PostProcess();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    status = runtime::performance::CollectQueryData(m_hQuery);
    if (status != runtime::performance::Success) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"PdhCollectQueryData failed %08x",
        //     status)
        // Log.Add(LOG_WARN,
        //     "[performance] Process CPU snapshot unavailable: second sampling failed (status 0x%08x).",
        //     static_cast<unsigned>(status))
        L2_LOG_WARN(
            Log, "[performance] Process CPU snapshot unavailable: second sampling failed (status 0x%08x).",
            static_cast<unsigned>(status)
        );
        PostProcess();
        return false;
    }

    // Noise: A single completion summary below replaces the repeated snapshot banners.
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[PERF] All Process CPU Usage List : %d Processes Begin ===========",
    //     m_nInstanceCount)
    int unavailableCount = 0;
    int endedInstanceCount = 0;
    std::string unavailableExamples;
    const double processorCount = static_cast<double>(LogicalProcessorCount());
    for (int index = 0; index < m_nInstanceCount; ++index) {
        double value = 0.0;
        status = runtime::performance::GetFormattedCounterValue(m_pCounter[index], value);
        StoreFormattedCounterValue(m_counterValue, status, value);
        // Noise: Convert names only for printed records or the first three unavailable-counter examples.
        // const std::wstring instanceName = Utf16ToWide(
        //     FixedName(m_pInfo[index].szInstanceName))
        if (status == runtime::performance::Success) {
            const int normalized = TargetDoubleToInt(value / processorCount);
            if (normalized > 0) {
                // Log.AddT(
                //     LOG_ERROR,
                //     L"[PERF] ProcessNo[%02d] Instance[%s] CPU Usage[%d]",
                //     index,
                //     instanceName.c_str(),
                //     normalized)
                // Log.Add(LOG_NORMAL,
                //     "[performance] Process instance #%d \"%s\": %d%% of total CPU capacity.",
                //     index, Utf16ToUtf8(FixedName(m_pInfo[index].szInstanceName)).c_str(), normalized)
                L2_LOG_INFO(
                    Log, "[performance] Process instance #%d \"%s\": %d%% of total CPU capacity.", index,
                    Utf16ToUtf8(FixedName(m_pInfo[index].szInstanceName)).c_str(), normalized
                );
            }
        } else {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[PERF] PdhGetFormattedCounterValue failed %08x InstanceName[%s]",
            //     status,
            //     instanceName.c_str())
            // Noise: Summarize unavailable counters once, with at most three examples.
            ++unavailableCount;
            if (status == runtime::performance::NoInstance) {
                ++endedInstanceCount;
            }
            AppendUnavailableCounterExample(
                unavailableExamples, unavailableCount, index, m_pInfo[index].szInstanceName, status
            );
        }
    }
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[PERF] All Process CPU Usage List : %d Processes End ===========",
    //     m_nInstanceCount)
    // Log.Add(unavailableCount == 0 ? LOG_NORMAL : LOG_WARN,
    //     "[performance] Process CPU snapshot complete: %d instances, %d unavailable.%s%s",
    //     m_nInstanceCount, unavailableCount,
    //     unavailableExamples.empty() ? "" : " Examples (up to 3): ", unavailableExamples.c_str())
    // Noise: a process that ended during the snapshot (NoInstance) is normal
    // and does not make the summary a warning.
    L2_LOG(
        Log, unavailableCount == endedInstanceCount ? LOG_NORMAL : LOG_WARN,
        "[performance] Process CPU snapshot complete: %d instances, %d unavailable (%d ended during the snapshot).%s%s",
        m_nInstanceCount, unavailableCount, endedInstanceCount,
        unavailableExamples.empty() ? "" : " Examples (up to 3): ", unavailableExamples.c_str()
    );
    PostProcess();
    return true;
}

// Current Cached.exe 0x004fe844.
void CPerfMon::CollectMemoryStatus() {
    const runtime::performance::MemoryStatus status = runtime::performance::ReadMemoryStatus();
    m_nMemUse = runtime::Int32FromBits(status.memoryLoad);
    m_nFreePhyMem = runtime::Int32FromBits(status.availablePhysicalBytes >> 10U);
    m_nTotPhyMem = runtime::Int32FromBits(status.totalPhysicalBytes >> 10U);
    m_nProcessMem = runtime::Int32FromBits(status.processWorkingSetBytes >> 10U);
}

// Current Cached.exe 0x004fe900.
bool CPerfMon::CollectPerformanceCounter() {
    const runtime::performance::Status collectStatus = runtime::performance::CollectQueryData(m_hQuery);
    if (collectStatus != runtime::performance::Success) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"PdhCollectQueryData failed %08x",
        //     collectStatus)
        // Log.Add(LOG_WARN,
        //     "[performance] Counter sampling failed (status 0x%08x). This report cycle is skipped.",
        //     static_cast<unsigned>(collectStatus))
        L2_LOG_WARN(
            Log, "[performance] Counter sampling failed (status 0x%08x); this report cycle is skipped.",
            static_cast<unsigned>(collectStatus)
        );
        return false;
    }

    for (int index = 0; index < m_nPerfCount; ++index) {
        double value = 0.0;
        const runtime::performance::Status status =
            runtime::performance::GetFormattedCounterValue(m_pHCounter[index], value);
        StoreFormattedCounterValue(m_counterValue, status, value);
        if (status == runtime::performance::Success) {
            m_pPerf[index].nPerval = value;
            continue;
        }

        const std::uint32_t now = runtime::GetTickCount();
        if (static_cast<std::uint32_t>(now - m_dwErrorTick) > 60000U && m_pPerf != nullptr) {
            const std::string objectName = Utf16ToUtf8(FixedName(m_pPerf[index].szObjectName));
            const std::string instanceName = Utf16ToUtf8(FixedName(m_pPerf[index].szInstanceName));
            const std::string counterName = Utf16ToUtf8(FixedName(m_pPerf[index].szCounterName));
            // Log.AddT(
            //     LOG_ERROR,
            //     L"PdhGetFormattedCounterValue failed %08x ObjectName[%s] InstanceName[%s] CounterName[%s]",
            //     status,
            //     objectName.c_str(),
            //     instanceName.c_str(),
            //     counterName.c_str())
            // Log.Add(LOG_WARN,
            //     "[performance] Counter \"%s / %s / %s\" is unavailable (status 0x%08x). Retaining its last stored value.",
            //     WideToUtf8(objectName).c_str(), WideToUtf8(instanceName).c_str(), WideToUtf8(counterName).c_str(),
            //     static_cast<unsigned>(status))
            // Noise: NoInstance means the process is not running, for example a
            // peer server that starts later. It is not a counter failure.
            L2_LOG(
                Log, status == runtime::performance::NoInstance ? LOG_NORMAL : LOG_WARN,
                "[performance] Counter \"%s / %s / %s\" is unavailable (status 0x%08x%s); retaining its last stored value.",
                objectName.c_str(), instanceName.c_str(), counterName.c_str(), static_cast<unsigned>(status),
                status == runtime::performance::NoInstance ? ", process not running" : ""
            );
            m_dwErrorTick = runtime::GetTickCount();
        }
    }
    return true;
}

#if defined(L2_BUILD_CACHED)

// Current Cached.exe 0x004ff66c.
bool CPerfMon::Init() {
    runtime::Config configuration;
    configuration.Load(runtime::ConfigFile("./perfmon.ini").string(), false);
    m_nCheckPerfMon = configuration.GetInt("PerfMon", "CheckPerfMon", 0);
    m_nPerfCount = configuration.GetInt("PerfMon", "Count", 0);
    m_nInterval = configuration.GetInt("PerfMon", "Interval", 6000);
    m_nMemusage = configuration.GetInt("PerfMon", "Memusage", 0);
    m_dwProcessId = runtime::performance::CurrentProcessId();
    m_nCPUIsolationLevel = configuration.GetInt("PerfMon", "CPUIsolationLevel", 10);

    if (m_nCheckPerfMon == 0) {
        return false;
    }
    if (m_nPerfCount < 1) {
        return true;
    }

    m_pHCounter = static_cast<void**>(std::calloc(static_cast<std::size_t>(m_nPerfCount), sizeof(void*)));
    if (!m_pHCounter) {
        return false;
    }

    runtime::performance::Status status = runtime::performance::OpenQuery(m_hQuery);
    if (status != runtime::performance::Success) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"POQ failed %08x",
        //     status)
        // Log.Add(LOG_ERROR,
        //     "[performance] Cannot open the performance query (status 0x%08x). Counter setup aborted.",
        //     static_cast<unsigned>(status))
        L2_LOG_ERROR(
            Log, "[performance] Cannot open the performance query (status 0x%08x); counter setup aborted.",
            static_cast<unsigned>(status)
        );
        std::free(m_pHCounter);
        m_pHCounter = nullptr;
        return false;
    }

    m_pPerf = static_cast<_Perf_*>(std::calloc(static_cast<std::size_t>(m_nPerfCount), sizeof(_Perf_)));
    if (!m_pPerf) {
        runtime::performance::CloseQuery(m_hQuery);
        m_hQuery = nullptr;
        std::free(m_pHCounter);
        m_pHCounter = nullptr;
        return false;
    }

    const auto discardInitialization = [this] {
        if (m_pHCounter) {
            for (int index = 0; index < m_nPerfCount; ++index) {
                runtime::performance::RemoveCounter(m_pHCounter[index]);
            }
        }
        runtime::performance::CloseQuery(m_hQuery);
        m_hQuery = nullptr;
        std::free(m_pPerf);
        m_pPerf = nullptr;
        std::free(m_pHCounter);
        m_pHCounter = nullptr;
        m_bQuery = false;
    };

    int notApplicableCounterCount = 0;

    for (int index = 0; index < m_nPerfCount; ++index) {
        const std::string number = std::to_string(index + 1);
        CopyName(m_pPerf[index].szObjectName, configuration.Get("PerfMon", "Perf" + number + "ObjectName", "Processor"));
        CopyName(m_pPerf[index].szInstanceName, configuration.Get("PerfMon", "Perf" + number + "InstanceName", "0"));
        CopyName(
            m_pPerf[index].szCounterName, configuration.Get("PerfMon", "Perf" + number + "CounterName", "% Processor Time")
        );
        m_pPerf[index].nPerval = 0.0;

        // The target replaces Perf2's configured process instance with the
        // basename returned by GetProcessImageFileNameW.
        if (index == 1) {
            CopyName(m_pPerf[index].szInstanceName, runtime::performance::CurrentExecutableStem());
        }

        const auto definition = CounterDefinition(m_pPerf[index]);
        std::u16string path;
        status = runtime::performance::MakeCounterPath(definition, path);
        if (status != runtime::performance::Success) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"PdhMakeCounterPath failed %08x",
            //     status)
            // Log.Add(LOG_ERROR,
            //     "[performance] Cannot build Perf%s counter path for \"%s / %s / %s\" (status 0x%08x). Counter setup
            //     aborted.", number.c_str(), Utf16ToUtf8(definition.objectName).c_str(),
            //     Utf16ToUtf8(definition.instanceName).c_str(), Utf16ToUtf8(definition.counterName).c_str(),
            //     static_cast<unsigned>(status))
            L2_LOG_ERROR(
                Log,
                "[performance] Cannot build Perf%s counter path for \"%s / %s / %s\" (status 0x%08x); counter setup "
                "aborted.",
                number.c_str(), Utf16ToUtf8(definition.objectName).c_str(), Utf16ToUtf8(definition.instanceName).c_str(),
                Utf16ToUtf8(definition.counterName).c_str(), static_cast<unsigned>(status)
            );
            discardInitialization();
            return false;
        }
        status = runtime::performance::AddCounter(m_hQuery, definition, path, m_pHCounter[index]);
        if (status != runtime::performance::Success) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"PdhAddCounter failed %08x",
            //     status)
            // Log.Add(LOG_ERROR,
            //     "[performance] Cannot register Perf%s counter \"%s\" (status 0x%08x). Counter setup aborted.",
            //     number.c_str(), Utf16ToUtf8(path).c_str(), static_cast<unsigned>(status))
            L2_LOG_ERROR(
                Log, "[performance] Cannot register Perf%s counter \"%s\" (status 0x%08x); counter setup aborted.",
                number.c_str(), Utf16ToUtf8(path).c_str(), static_cast<unsigned>(status)
            );
            discardInitialization();
            return false;
        }
        if (runtime::performance::IsCounterNotApplicable(m_pHCounter[index])) {
            ++notApplicableCounterCount;
        }
        const std::string hostPath = Utf16ToUtf8(path);
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"PdhMakeCounterPath %s",
        //     hostPath.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[performance] Registered counter \"%s\".",
        //     WideToUtf8(hostPath).c_str())
        L2_LOG_INFO(Log, "[performance] Registered counter \"%s\".", hostPath.c_str());
    }
    if (notApplicableCounterCount > 0) {
        // Portable-only deployment diagnostic: the current Windows path has
        // no NotApplicable counters and therefore emits no additional line.
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"Portable PerfMon: %d Windows/MSSQL counters are not applicable and remain zero",
        //     notApplicableCounterCount)
        // Log.Add(LOG_NORMAL,
        //     "[performance] %d counters do not apply to this platform and will report zero.",
        //     notApplicableCounterCount)
        L2_LOG_INFO(
            Log, "[performance] %d counters do not apply to this platform and will report zero.", notApplicableCounterCount
        );
    }
    m_bQuery = true;
    return true;
}

// Current Cached.exe 0x004ff45c.
void CPerfMon::ReportCommon() {
    const double processorCount = static_cast<double>(LogicalProcessorCount());
    const int totalCpu = CounterInt(*this, 0);
    const int processCpu = TargetDoubleToInt(CounterDouble(*this, 1) / processorCount);
    const int sqlServerCpu = TargetDoubleToInt(CounterDouble(*this, 2) / processorCount);
    const int memoryPageFaults = CounterInt(*this, 3);
    const int memoryPages = CounterInt(*this, 4);
    const int processPageFaults = CounterInt(*this, 5);
    const int sqlServerPageFaults = CounterInt(*this, 6);

    int availablePhysicalPercent = 0;
    if (m_nTotPhyMem >= 1) {
        const std::uint32_t numeratorBits = static_cast<std::uint32_t>(m_nFreePhyMem) * 100U;
        std::int32_t numerator = 0;
        std::memcpy(&numerator, &numeratorBits, sizeof(numerator));
        availablePhysicalPercent = numerator / m_nTotPhyMem;
    }

    Log.AddT(
        LOG_DB, u"%d,,,,,,,,,,,%d,%d,%d,%d,%d,%d,,,,,,,,,", 0x642, totalCpu, availablePhysicalPercent, m_nProcessMem,
        CUser::s_loggedUsers.load(std::memory_order_relaxed), processCpu, sqlServerCpu
    );
    Log.AddT(
        LOG_DB,
        u"%d,,,,,,,,,,,%d,%d,%d,%d"
        u",,,,,,,,,,,",
        0x647, memoryPageFaults, memoryPages, processPageFaults, sqlServerPageFaults
    );

    if (m_nCPUIsolationLevel < (totalCpu - sqlServerCpu) - processCpu) {
        if (m_instantPerfMon.CollectInstanceList()) {
            m_instantPerfMon.CollectPerformanceCounter();
        }
    }
}

// Current Cached.exe 0x004fea24. The damaged current PDB does not keep the
// original method identifier. The body reports SQL Server counters 8..27.
void CPerfMon::ReportDatabaseCountersAt004fea24() {
    Log.AddT(
        LOG_DB, u"%d,,,,,,,,,,,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d,,,,,", 0x649, CounterDouble(*this, 10),
        CounterDouble(*this, 13), CounterInt(*this, 7), CounterInt(*this, 8), CounterInt(*this, 9), CounterInt(*this, 11),
        CounterInt(*this, 12), CounterInt(*this, 14), CounterInt(*this, 25), CounterInt(*this, 26)
    );
    Log.AddT(
        LOG_DB, u"%d,,,,,,,,,,,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d,,,,,", 0x64a, CounterDouble(*this, 15),
        CounterDouble(*this, 19), CounterInt(*this, 16), CounterInt(*this, 17), CounterInt(*this, 18), CounterInt(*this, 20),
        CounterInt(*this, 21), CounterInt(*this, 22), CounterInt(*this, 23), CounterInt(*this, 24)
    );
}

// Current Cached.exe 0x004fecfc. The damaged current PDB does not keep the
// original method identifier. The body reports disk counters 28..33.
void CPerfMon::ReportDiskCountersAt004fecfc() {
    Log.AddT(
        LOG_DB, u"%d,,,,,,,,,,,%f,%f,%d,%d,%d,%d,,,,,,,,,", 0x64b, CounterDouble(*this, 27), CounterDouble(*this, 30),
        TargetDoubleToInt(CounterDouble(*this, 28) * 1000.0), TargetDoubleToInt(CounterDouble(*this, 29) * 1000.0),
        TargetDoubleToInt(CounterDouble(*this, 31) * 1000.0), TargetDoubleToInt(CounterDouble(*this, 32) * 1000.0)
    );
}

// Current Cached.exe 0x004ff614.
bool CPerfMon::GetPerfMon() {
    if (m_nCheckPerfMon == 0) {
        return false;
    }
    if (m_nMemusage == 1) {
        CollectMemoryStatus();
    }
    if (m_nPerfCount > 0 && m_bQuery && !CollectPerformanceCounter()) {
        return false;
    }
    ReportCommon();
    ReportDatabaseCountersAt004fea24();
    ReportDiskCountersAt004fecfc();
    return true;
}

#endif // L2_BUILD_CACHED

#if defined(L2_BUILD_L2SERVER)

// Current L2Server.exe 0x00953598.
bool CPerfMon::Init() {
    runtime::Config configuration;
    // Opt-in portable deployment path. The original cwd-relative filename
    // remains the default when --config-dir is absent.
    configuration.Load(runtime::ConfigFile("./perfmon.ini").string(), false);
    m_nCheckPerfMon = configuration.GetInt("PerfMon", "CheckPerfMon", 0);
    m_nPerfCount = configuration.GetInt("PerfMon", "Count", 0);
    m_nInterval = configuration.GetInt("PerfMon", "Interval", 6000);
    m_nMemusage = configuration.GetInt("PerfMon", "Memusage", 0);
    m_dwProcessId = runtime::performance::CurrentProcessId();
    m_nCPUIsolationLevel = configuration.GetInt("PerfMon", "CPUIsolationLevel", 10);

    if (m_nCheckPerfMon == 0) {
        return false;
    }
    if (m_nPerfCount < 1) {
        return true;
    }

    m_pHCounter = static_cast<void**>(std::malloc(static_cast<std::size_t>(m_nPerfCount) * sizeof(void*)));
    if (m_pHCounter == nullptr) {
        return false;
    }

    runtime::performance::Status status = runtime::performance::OpenQuery(m_hQuery);
    if (status != runtime::performance::Success) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"POQ failed %08x",
        //     status)
        // Log.Add(LOG_ERROR,
        //     "[performance] Cannot open the performance query (status 0x%08x). Counter setup aborted.",
        //     static_cast<unsigned>(status))
        L2_LOG_ERROR(
            Log, "[performance] Cannot open the performance query (status 0x%08x); counter setup aborted.",
            static_cast<unsigned>(status)
        );
        std::free(m_pHCounter);
        m_pHCounter = nullptr;
        return false;
    }

    m_pPerf = static_cast<_Perf_*>(std::malloc(static_cast<std::size_t>(m_nPerfCount) * sizeof(_Perf_)));
    if (m_pPerf == nullptr) {
        // The current failure edge leaks the already-open PDH
        // query. It releases only the handle array.
        std::free(m_pHCounter);
        m_pHCounter = nullptr;
        return false;
    }

    const auto discardArrays = [this] {
        // Current 0x00953b38 does not remove preceding counters or close the
        // query on a path/add failure.
        std::free(m_pPerf);
        m_pPerf = nullptr;
        std::free(m_pHCounter);
        m_pHCounter = nullptr;
    };

    for (int index = 0; index < m_nPerfCount; ++index) {
        _Perf_& perf = m_pPerf[index];
        perf.nPerval = 0.0;
        const std::string number = std::to_string(index + 1);
        CopyName(perf.szObjectName, configuration.Get("PerfMon", "Perf" + number + "ObjectName", "Processor"));
        CopyName(perf.szInstanceName, configuration.Get("PerfMon", "Perf" + number + "InstanceName", "0"));
        CopyName(perf.szCounterName, configuration.Get("PerfMon", "Perf" + number + "CounterName", "% Processor Time"));

        // Current Perf2 replaces the configured instance with the basename
        // obtained from GetProcessImageFileNameW. The platform adapter
        // publishes the same executable-stem contract.
        if (index == 1) {
            CopyName(perf.szInstanceName, runtime::performance::CurrentExecutableStem());
        }

        const auto definition = CounterDefinition(perf);
        std::u16string path;
        status = runtime::performance::MakeCounterPath(definition, path);
        if (status != runtime::performance::Success) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"PdhMakeCounterPath failed %08x",
            //     status)
            // Log.Add(LOG_ERROR,
            //     "[performance] Cannot build Perf%s counter path for \"%s / %s / %s\" (status 0x%08x). Counter setup
            //     aborted.", number.c_str(), Utf16ToUtf8(definition.objectName).c_str(),
            //     Utf16ToUtf8(definition.instanceName).c_str(), Utf16ToUtf8(definition.counterName).c_str(),
            //     static_cast<unsigned>(status))
            L2_LOG_ERROR(
                Log,
                "[performance] Cannot build Perf%s counter path for \"%s / %s / %s\" (status 0x%08x); counter setup "
                "aborted.",
                number.c_str(), Utf16ToUtf8(definition.objectName).c_str(), Utf16ToUtf8(definition.instanceName).c_str(),
                Utf16ToUtf8(definition.counterName).c_str(), static_cast<unsigned>(status)
            );
            discardArrays();
            return false;
        }
        status = runtime::performance::AddCounter(m_hQuery, definition, path, m_pHCounter[index]);
        if (status != runtime::performance::Success) {
            // Log.Add(
            //     LOG_ERROR,
            //     "PdhAddCounter failed %08x",
            //     status)
            // Log.Add(LOG_ERROR,
            //     "[performance] Cannot register Perf%s counter \"%s\" (status 0x%08x). Counter setup aborted.",
            //     number.c_str(), Utf16ToUtf8(path).c_str(), static_cast<unsigned>(status))
            L2_LOG_ERROR(
                Log, "[performance] Cannot register Perf%s counter \"%s\" (status 0x%08x); counter setup aborted.",
                number.c_str(), Utf16ToUtf8(path).c_str(), static_cast<unsigned>(status)
            );
            discardArrays();
            return false;
        }
        // Portability improvement: unavailable platform slots keep their
        // positions, but zero must not be presented as a measured value.
        if (runtime::performance::IsCounterNotApplicable(m_pHCounter[index])) {
            L2_LOG_INFO(
                Log,
                "[performance] Perf%s counter \"%s\" is unavailable on this platform; its slot remains zero (not a "
                "measurement). Other counters remain enabled.",
                number.c_str(), Utf16ToUtf8(path).c_str()
            );
            continue;
        }
        const std::string hostPath = Utf16ToUtf8(path);
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"PdhMakeCounterPath %s",
        //     hostPath.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[performance] Registered counter \"%s\".",
        //     WideToUtf8(hostPath).c_str())
        L2_LOG_INFO(Log, "[performance] Registered counter \"%s\".", hostPath.c_str());
    }

    m_bQuery = true;
    return true;
}

// Current L2Server.exe 0x009533c8.
void CPerfMon::ReportCommon() {
    const double processorCount = static_cast<double>(LogicalProcessorCount());
    const int totalCpu = L2ServerCounterInt(*this, 0);

    g_L2ServerUsage = m_nPerfCount > 1 ? TargetDoubleToInt(m_pPerf[1].nPerval / processorCount) : 0;
    g_L2NPCUsage = m_nPerfCount > 2 ? TargetDoubleToInt(m_pPerf[2].nPerval / processorCount) : 0;

    const int memoryPageFaults = L2ServerCounterInt(*this, 3);
    const int memoryPages = L2ServerCounterInt(*this, 4);
    const int processPageFaults = L2ServerCounterInt(*this, 5);

    int availablePhysicalPercent = 0;
    if (m_nTotPhyMem > 0) {
        const std::int32_t numerator = runtime::Int32FromBits(static_cast<std::uint32_t>(m_nFreePhyMem) * 100U);
        availablePhysicalPercent = numerator / m_nTotPhyMem;
    }

    // Current publication order is L2Server, L2NPC, then aggregate CPU.
    g_CPUUsage = totalCpu;
    Log.AddT(
        LOG_DB, u"%d,,,,,,,,,,,%d,%d,%d,%d,%d,%d,%d,%d,%d,,,,,,", 0x643, totalCpu, availablePhysicalPercent, m_nProcessMem,
        g_nServerUserCount, g_nPlayingUserCount, g_nStoreUserCount, g_nSocketPendingWrite.load(std::memory_order_relaxed),
        g_L2ServerUsage, g_L2NPCUsage
    );
    Log.AddT(LOG_DB, u"%d,,,,,,,,,,,%d,%d,%d,,,,,,,,,,,,", 0x648, memoryPageFaults, memoryPages, processPageFaults);

    const std::int32_t otherCpu =
        runtime::SubtractInt32Wrapping(runtime::SubtractInt32Wrapping(totalCpu, g_L2NPCUsage), g_L2ServerUsage);
    if (m_nCPUIsolationLevel < otherCpu && m_instantPerfMon.CollectInstanceList()) {
        m_instantPerfMon.CollectPerformanceCounter();
    }
}

// Current L2Server.exe 0x00952f58.
void CPerfMon::ReportSayCount() {
    const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
    if (static_cast<std::int32_t>(localTime.wHour) == ::g_nLastHour) {
        return;
    }

    Log.AddT(
        LOG_BOTH, u"%I64d,,,,,,,,,,,%I64d,%I64d,,,,,,,,,,,,,", std::int64_t{0x340}, static_cast<std::int64_t>(g_nSayCount),
        static_cast<std::int64_t>(g_nSaySize)
    );
    ::g_nLastHour = static_cast<std::int32_t>(localTime.wHour);
    runtime::raw_atomic::ExchangeI32(&g_nSayCount, 0);
    runtime::raw_atomic::ExchangeI32(&g_nSaySize, 0);
}

// Current L2Server.exe 0x00953b64.
void CPerfMon::ReportUserCount() {
    const std::int32_t now = runtime::crt::Time32Now();
    if (runtime::SubtractInt32Wrapping(now, m_nLastCheck) <= 900) {
        return;
    }
    m_nLastCheck = runtime::crt::Time32Now();

    std::int32_t partiesBySize[8]{};
    std::int32_t levelsBySize[8]{};
    std::int32_t partyCount = 0;
    std::int32_t totalMemberCount = 0;

    FOR_ALL_OBJECT_IN_OBJECT_MAP(::g_PartyMap, party, {
        partyCount = runtime::AddInt32Wrapping(partyCount, 1);
        const std::int32_t memberCount = party->GetMemberCount();
        totalMemberCount = runtime::AddInt32Wrapping(totalMemberCount, memberCount);
        if (static_cast<std::uint32_t>(memberCount) - 1U < 7U) {
            partiesBySize[memberCount] = runtime::AddInt32Wrapping(partiesBySize[memberCount], 1);
            levelsBySize[memberCount] = runtime::AddInt32Wrapping(levelsBySize[memberCount], party->GetLevSum());
        }
    });

    for (int memberCount = 1; memberCount <= 7; ++memberCount) {
        Log.AddT(
            LOG_BOTH, u"%I64d,,,,,,,,,,,%I64d,%I64d,%I64d,,,,,,,,,,,,", std::int64_t{0x344},
            static_cast<std::int64_t>(memberCount), static_cast<std::int64_t>(partiesBySize[memberCount]),
            static_cast<std::int64_t>(levelsBySize[memberCount])
        );
    }

    Log.AddT(
        LOG_BOTH, u"%I64d,,,,,,,,,,,%I64d,,,,,,,,,,,,,,", std::int64_t{0x357},
        static_cast<std::int64_t>(CPartyRoomRegistry::inst().CountMember())
    );
    Log.AddT(
        LOG_DB, u"%d,,,,,,,,,,,%d,%d,%d,%d,%d,,,,,,,,,,", 0x646, g_nServerUserCount, g_nPlayingUserCount, g_nStoreUserCount,
        partyCount, totalMemberCount
    );
}

// Current L2Server.exe 0x00953e70.
bool CPerfMon::GetPerfMon() {
    if (m_nCheckPerfMon == 0) {
        return false;
    }
    if (m_nMemusage == 1) {
        CollectMemoryStatus();
    }
    if (m_nPerfCount > 0 && m_bQuery && !CollectPerformanceCounter()) {
        return false;
    }
    ReportCommon();
    ReportSayCount();
    ReportUserCount();
    return true;
}

#endif // L2_BUILD_L2SERVER

#endif // L2_BUILD_CACHED || L2_BUILD_L2SERVER

CPerfMon g_PerfMon;

} // namespace l2npc
