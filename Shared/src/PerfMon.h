#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace l2npc {

// Shared/perfmon.h calls this record _Perf_. WCHAR is fixed at 16 bits in the
// target, so char16_t keeps its three 255-character buffers and 0x608 size
// unchanged on Unix hosts where wchar_t is 32 bits.
struct _Perf_ {
    std::array<char16_t, 255> szObjectName;
    std::array<char16_t, 255> szInstanceName;
    std::array<char16_t, 255> szCounterName;
    std::array<std::byte, 6> alignmentPadding;
    double nPerval;
};


// Win64 PDH_FMT_COUNTERVALUE consists of a 32-bit status, four bytes of
// alignment and an eight-byte value union.
struct TargetPdhFormattedCounterValue {
    std::uint32_t CStatus;
    std::array<std::byte, 4> alignmentPadding;
    std::uint64_t rawValue;
};


class CInstantPerfMon {
public:
    CInstantPerfMon();
    ~CInstantPerfMon();

    bool CollectInstanceList();
    bool CollectPerformanceCounter();

private:
    bool Init();
    void PostProcess();

    _Perf_* m_pInfo;
    void* m_hQuery;
    void** m_pCounter;
    TargetPdhFormattedCounterValue m_counterValue;
    std::int32_t m_nInstanceCount;
    std::array<std::byte, 4> m_tailPadding;
};

class CPerfMon {
public:
    CPerfMon();
    ~CPerfMon();

    bool Init();
    bool Delete();
    bool ReLoad();
    bool GetPerfMon();
    int GetInterval() { return m_nInterval; }

    static const wchar_t* s_File;

    // These fields are public in the recovered perfmon.h type record.
    _Perf_* m_pPerf;
    void** m_pHCounter;
    void* m_hQuery;
    TargetPdhFormattedCounterValue m_counterValue;
    std::int32_t m_nCheckPerfMon;
    std::int32_t m_nMemusage;
    std::int32_t m_nInterval;
    std::int32_t m_nPerfCount;
    bool m_bQuery;
    std::array<std::byte, 3> m_queryPadding;
    std::int32_t m_nLastCheck;
    std::int32_t m_nCPUIsolationLevel;
    std::uint32_t m_dwProcessId;
    std::int32_t m_nMemUse;
    std::int32_t m_nFreePhyMem;
    std::int32_t m_nTotPhyMem;
    std::int32_t m_nProcessMem;
    std::uint32_t m_dwErrorTick;
    std::array<std::byte, 4> m_instantAlignmentPadding;
    CInstantPerfMon m_instantPerfMon;

private:
    void CollectMemoryStatus();
    bool CollectPerformanceCounter();
    void ReportCommon();
    void ReportSayCount();
    void ReportUserCount();
    void ReportDisk();

    // Current Cached.exe contains these two CacheD-only report bodies in
    // Shared/perfmon.cpp. Their original identifiers are absent from the
    // damaged PDB. The semantic names keep their exact entry addresses.
    void ReportDatabaseCountersAt004fea24();
    void ReportDiskCountersAt004fecfc();
};


// Process-specific Shared/perfmon.cpp global: current L2NPC.exe
// DAT_0278fe30 and current Cached.exe DAT_02a04290.
extern CPerfMon g_PerfMon;

} // namespace l2npc
