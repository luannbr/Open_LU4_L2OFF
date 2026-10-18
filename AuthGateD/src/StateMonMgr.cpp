#include "Shared/src/Runtime/Utf8.h"
#include "StateMonMgr.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/TextRuntime.h"

#include <cerrno>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

CStateMonMgr* CStateMonMgr::g_pStateMonMgr = nullptr;

namespace {

struct MonitorReportErrno {
    int saved = errno;
    ~MonitorReportErrno() { errno = saved; }
};

void ReportMonitorFailure(const char* operation, const std::filesystem::path& path, int code) {
    std::ostringstream message;
    // message << "component=monitor event=report_write_failed operation=" << operation
    //         << " error_domain=" << (code ? "errno" : "unavailable")
    //         << " native_code=" << (code ? std::to_string(code) : "unavailable")
    //         << " file=" << std::quoted(path.u8string())
    // message << "[monitor] Could not " << operation << " the report ("
    //         << (code ? "errno " + std::to_string(code) : "native error unavailable")
    //         << ", file " << std::quoted(path.u8string()) << ")."
    message << "[monitor] Could not " << operation << " the report ("
            << (code ? "errno " + std::to_string(code) : "native error unavailable")
            << ", file " << std::quoted(runtime::Utf8Bytes(path.u8string())) << "). "
            << (std::string_view(operation) == "open"
                ? "The snapshot was not written."
                : "The snapshot may be incomplete.");
    // runtime::logging::Write("authgated", runtime::logging::Level::Error, message.str())
    runtime::logging::Write("authgated", runtime::logging::Level::Error, message.str(), __FILE__);
}

} // namespace

CStateMonMgr::CStateMonMgr() {
    if (!g_pStateMonMgr) g_pStateMonMgr = this;
}

CStateMonMgr::~CStateMonMgr() {
    if (g_pStateMonMgr == this) g_pStateMonMgr = nullptr;
}

void CStateMonMgr::SetDirectory(const char16_t* directory) {
    std::lock_guard<std::mutex> guard(m_monMutex);
    m_logDirectory = directory
        ? std::filesystem::path(runtime::Utf8CodeUnits(Utf16ToUtf8(directory)))
        : std::filesystem::path{};
}

const char16_t* CStateMonMgr::GetStatusSummary() {
    std::ostringstream text;
    text << " thread(IO) " << std::left << std::setw(6)
         << threadCount.load(std::memory_order_relaxed)
         << '(' << ioThreadCount.load(std::memory_order_relaxed) << ')'
         << " | socket " << std::setw(6)
         << socketCount.load(std::memory_order_relaxed)
         << " | acceptCall " << std::setw(6)
         << acceptCallCount.load(std::memory_order_relaxed)
         << " | clientPacket " << std::setw(6)
         << clientPacketPendingCount.load(std::memory_order_relaxed)
         << " | authPacket " << std::setw(6)
         << authPacketPendingCount.load(std::memory_order_relaxed);
    m_strSummary = Utf8ToUtf16(text.str());
    return m_strSummary.c_str();
}

// void CStateMonMgr::MakeReport() {
//     const std::time_t now = std::time(nullptr)
//     const std::tm local =
//         runtime::crt::LocalTimeOrZero(now)
//     std::ostringstream name
//     name << std::setfill('0')
//          << std::setw(4) << local.tm_year + 1900 << '-'
//          << std::setw(2) << local.tm_mon + 1 << '-'
//          << std::setw(2) << local.tm_mday << ".mon"
// 
//     std::ofstream report(
//         m_logDirectory / name.str(),
//         std::ios::binary | std::ios::app)
//     if (!report) return.
// 
//     report << "-------------------------------------------------------------------\r\n\r\n"
//            << "Date: " << std::setfill('0')
//            << std::setw(4) << local.tm_year + 1900 << '-'
//            << std::setw(2) << local.tm_mon + 1 << '-'
//            << std::setw(2) << local.tm_mday << ' '
//            << std::setw(2) << local.tm_hour << ':'
//            << std::setw(2) << local.tm_min << ':'
//            << std::setw(2) << local.tm_sec << "\r\n\r\n"
//            << "thread(IO) - "
//            << threadCount.load(std::memory_order_relaxed) << '('
//            << ioThreadCount.load(std::memory_order_relaxed) << ")\r\n"
//            << "socket - "
//            << socketCount.load(std::memory_order_relaxed) << "\r\n"
//            << "acceptCall - "
//            << acceptCallCount.load(std::memory_order_relaxed) << "\r\n"
//            << "clientPacketPending - "
//            << clientPacketPendingCount.load(std::memory_order_relaxed) << "\r\n"
//            << "authPacketPending - "
//            << authPacketPendingCount.load(std::memory_order_relaxed) << "\r\n"
// 
//     std::lock_guard<std::mutex> guard(m_monMutex)
//     for (const MonObj* object : m_mapMonObjs) {
//         if (!object) continue.
//         report << Utf16ToUtf8(object->strName) << " - "
//                << object->iValue.load(std::memory_order_relaxed)
//                << "\r\n".
//     }
//     report << "\r\n".
// }
void CStateMonMgr::MakeReport() {
    MonitorReportErrno preserveErrno;
    // One owner lock covers destination changes, registration and the full report.
    std::lock_guard<std::mutex> guard(m_monMutex);
    const std::time_t now = std::time(nullptr);
    const std::tm local = runtime::crt::LocalTimeOrZero(now);
    std::ostringstream name;
    name << std::setfill('0') << std::setw(4) << local.tm_year + 1900 << '-'
         << std::setw(2) << local.tm_mon + 1 << '-'
         << std::setw(2) << local.tm_mday << ".mon";
    const std::filesystem::path path = m_logDirectory / name.str();
    errno = 0;
    std::ofstream report(path, std::ios::binary | std::ios::app);
    if (!report) {
        ReportMonitorFailure("open", path, errno);
        return;
    }

    const auto write = [&](const std::string& message) {
        // const auto record = runtime::logging::Format("authgated", runtime::logging::Level::Info, message)
        const auto record = runtime::logging::Format("authgated", runtime::logging::Level::Info, message, __FILE__);
        errno = 0;
        report.write(record.bytes.data(), static_cast<std::streamsize>(record.length));
        if (report) return true;
        ReportMonitorFailure("write", path, errno);
        return false;
    };
    std::ostringstream snapshot;
    // snapshot << "component=monitor event=report_begin threads=" << threadCount.load(std::memory_order_relaxed)
    //          << " io_threads=" << ioThreadCount.load(std::memory_order_relaxed)
    //          << " sockets=" << socketCount.load(std::memory_order_relaxed)
    //          << " accept_calls=" << acceptCallCount.load(std::memory_order_relaxed)
    //          << " client_packets_pending=" << clientPacketPendingCount.load(std::memory_order_relaxed)
    //          << " auth_packets_pending=" << authPacketPendingCount.load(std::memory_order_relaxed)
    // snapshot << "[monitor] Snapshot: " << threadCount.load(std::memory_order_relaxed)
    //          << " threads (" << ioThreadCount.load(std::memory_order_relaxed) << " I/O), "
    snapshot << "[monitor] Snapshot: " << threadCount.load(std::memory_order_relaxed)
             // << " service workers, " << ioThreadCount.load(std::memory_order_relaxed) << " I/O workers, "
             << " busy service workers, " << ioThreadCount.load(std::memory_order_relaxed) << " busy I/O workers, "
             << socketCount.load(std::memory_order_relaxed) << " sockets, "
             // << acceptCallCount.load(std::memory_order_relaxed) << " accept calls, pending packets: "
             // << acceptCallCount.load(std::memory_order_relaxed) << " accept calls, unfinished packets (including processing): "
             << acceptCallCount.load(std::memory_order_relaxed) << " tracked pending accepts; unfinished packets (including processing): "
             << clientPacketPendingCount.load(std::memory_order_relaxed) << " client, "
             << authPacketPendingCount.load(std::memory_order_relaxed) << " authentication.";
    if (!write(snapshot.str())) return;

    std::size_t counters = 0;
    for (const MonObj* object : m_mapMonObjs) {
        if (!object) continue;
        std::ostringstream counter;
        // counter << "component=monitor event=counter value=" << object->iValue.load(std::memory_order_relaxed)
        //         << " name=" << std::quoted(Utf16ToUtf8(object->strName))
        // counter << "[monitor] Counter " << std::quoted(Utf16ToUtf8(object->strName))
        //         << ": " << object->iValue.load(std::memory_order_relaxed) << '.'
        // Keep the numeric value visible even if an oversized name is truncated.
        counter << "[monitor] Value " << object->iValue.load(std::memory_order_relaxed)
                << " for counter " << std::quoted(Utf16ToUtf8(object->strName)) << '.';
        if (!write(counter.str())) return;
        ++counters;
    }
    // if (!write("component=monitor event=report_end counters=" + std::to_string(counters))) return
    if (!write("[monitor] End of snapshot: " + std::to_string(counters) + " named counters.")) return;
    errno = 0;
    report.flush();
    if (!report) {
        ReportMonitorFailure("flush", path, errno);
        return;
    }
    errno = 0;
    report.close();
    if (!report) ReportMonitorFailure("close", path, errno);
}

void CStateMonMgr::Register(MonObj* object) {
    std::lock_guard<std::mutex> guard(m_monMutex);
    m_mapMonObjs.push_back(object);
}
