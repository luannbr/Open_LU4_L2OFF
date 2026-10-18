#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Shared/QsCheck.cpp owner used by the L2CACHED build of CPacketEx. Current
// Cached.exe fixes the packet-table capacity at 235 and places the three
// arrays at +0x84, +0x430 and +0x7dc. The query map at +0xb88 and slow-query
// vector at +0xba8 are the real target containers. Their pointed record
// layouts are fixed by the allocations and field accesses at
// 0x00520d40, 0x0052118c, 0x00521e8c and 0x00522464.
class CQsCheck {
public:
    static constexpr std::size_t PacketCapacity = 235;

    CQsCheck();
    ~CQsCheck();

    void AddPacketReport(std::uint32_t packetType, std::uint32_t packetCallBackTime, std::uint32_t packetProcessTime);
    void ShowPacketReport();
    void ClearPacketReport();

    void SlowReport(LogType logType);
    void Report(LogType logType);
    std::uint32_t AddSlowQs(const wchar_t* statement, std::uint32_t durationMilliseconds);
    void ResetData();
    std::int64_t AddValue(const wchar_t* statement, std::uint32_t durationMilliseconds);

    static bool s_packetReport;

private:
    // The surviving symbols do not keep these two private type names.
    // Semantic names are used while their complete current layouts remain
    // machine-confirmed: 0x10 and 0x410 bytes respectively.
    struct QueryAggregate {
        std::int64_t count = 0;
        std::int64_t totalMilliseconds = 0;
    };

    struct SlowQueryRecord {
        SlowQueryRecord(std::uint32_t durationMilliseconds, const wchar_t* statement);

        std::int32_t creationTime = 0;
        std::uint32_t alignment = 0;
        std::int64_t totalMilliseconds = 0;
        std::array<char16_t, 512> statement{};
    };

    using QueryReportMap = std::map<std::u16string, QueryAggregate*, runtime::CStringLess<std::u16string>>;
    using SlowQueryReport = std::vector<SlowQueryRecord*, std::allocator<SlowQueryRecord*>>;


    CIOCriticalSection m_reportLock;
    CIOCriticalSection m_slowReportLock;
    CIOCriticalSection m_packetLock;
    std::int32_t m_slowRecordCounter = 0;
    std::int32_t m_recordCounter = 0;
    std::int32_t m_firstRecordTime = 0;
    std::array<std::uint32_t, PacketCapacity> m_packetCounters{};
    std::array<std::uint32_t, PacketCapacity> m_packetCallBackTimes{};
    std::array<std::uint32_t, PacketCapacity> m_packetProcessTimes{};
    QueryReportMap m_qsReport;
    SlowQueryReport m_slowQsReport;
};


extern CQsCheck g_qsCheck;

// Current global timer owner at Cached.exe DAT_02a1dce8. Its constructor is
// the ordinary CIOObject constructor followed by {0,-1,false}. The vtable
// overrides only TimerExpired at slot +0x20.
class CQsCheckManager final : public CIOObject {
public:
    CQsCheckManager() = default;
    ~CQsCheckManager() override = default;

    // Current 0x005228d4 has no kept symbol. This semantic name describes
    // its exact operation without claiming an original source identifier.
    void SetReportMinute(int reportMinute);
    bool StartTimer();
    bool EnableTimer();
    bool DisableTimer();
    bool IsReportMin(int reportMinute, int currentMinute);
    void TimerExpired(int id) override;

private:
    std::int32_t m_reportMinute = 0;
    std::int32_t m_nextReportMinute = -1;
    bool m_enabled = false;
};


extern CQsCheckManager g_qsCheckManager;

namespace packet_ex_detail {

// Exact #ifdef L2CACHED member block corroborated by the C1 source and by
// current Cached.exe 0x0045c2a0/0x004814c4. It is a policy only because the
// portable build must instantiate both process variants from one Shared tree.
class CPacketExCacheDInstrumentation {
public:
    static constexpr std::size_t TargetExtent = 0x10;

    CPacketExCacheDInstrumentation() noexcept;
    ~CPacketExCacheDInstrumentation();

    void SetPacketType(std::uint32_t packetType) noexcept;
    void BeginCallback(std::uint8_t* packet) noexcept;

private:
    std::uint32_t m_createTime;
    std::uint32_t m_handleTime;
    std::uint32_t m_deleteTime;
    std::uint32_t m_packetType;
};


} // namespace packet_ex_detail
