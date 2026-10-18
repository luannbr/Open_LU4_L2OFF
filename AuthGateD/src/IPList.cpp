#include "IPList.h"

#include "FileLog.h"
#include "GlobalFunc.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

std::string NativePath(const char16_t* fileName) {
    std::string path = Utf16Ascii(fileName);
#if !defined(_WIN32)
    std::replace(path.begin(), path.end(), '\\', '/');
#endif
    return path;
}

std::uint32_t ParseWindowsLong(char* text, char** end) {
    errno = 0;
    const long value = std::strtol(text, end, 10);
    if (value > INT32_MAX) {
        return static_cast<std::uint32_t>(INT32_MAX);
    }
    if (value < INT32_MIN) {
        return static_cast<std::uint32_t>(INT32_MIN);
    }
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(value));
}

bool ParseAddress(
    char*& cursor,
    std::uint32_t& address,
    std::uint32_t& thirdPart,
    std::uint32_t& fourthPart) {
    char* end = nullptr;
    const std::uint32_t first = ParseWindowsLong(cursor, &end);
    if (*end != '.') return false;

    cursor = end + 1;
    const std::uint32_t second = ParseWindowsLong(cursor, &end);
    if (*end != '.') return false;

    cursor = end + 1;
    thirdPart = ParseWindowsLong(cursor, &end);
    if (*end != '.') return false;

    cursor = end + 1;
    fourthPart = ParseWindowsLong(cursor, &end);
    cursor = end;
    address = (((first << 8U) | second) << 8U | thirdPart) << 8U |
        fourthPart;
    return true;
}

std::vector<IPRecord>::iterator FindContainingOrNext(
    std::vector<IPRecord>& table,
    std::uint32_t value) {
    return std::upper_bound(
        table.begin(),
        table.end(),
        value,
        [](std::uint32_t address, const IPRecord& record) {
            return address < record.end;
        });
}

// void LogMergedRange(
//     const IPRecord& record,
//     std::int32_t lineNumber) {
//     const std::uint32_t inclusiveEnd = record.end - 1U
//     CFileLog::g_pDefaultFileLog->Log(
//         LOG_ERROR,
//         u"IPList: address is merged with %d.%d.%d.%d-%d.%d.%d.%d at line %d",
//         (record.begin >> 24U) & 0xffU,
//         (record.begin >> 16U) & 0xffU,
//         (record.begin >> 8U) & 0xffU,
//         record.begin & 0xffU,
//         (inclusiveEnd >> 24U) & 0xffU,
//         (inclusiveEnd >> 16U) & 0xffU,
//         (inclusiveEnd >> 8U) & 0xffU,
//         inclusiveEnd & 0xffU,
//         lineNumber)
// }
void LogMergedRange(const IPRecord& record, std::int32_t lineNumber, const char16_t* path) {
    const std::uint32_t inclusiveEnd = record.end - 1U;
    // CFileLog::g_pDefaultFileLog->Log(LOG_WARN,
    //     u"component=ip_filter event=range_overlap file=\"%s\" line=%d existing_range=%u.%u.%u.%u-%u.%u.%u.%u action=merge",
    //     path, lineNumber,
    //     (record.begin >> 24U) & 0xffU, (record.begin >> 16U) & 0xffU,
    //     (record.begin >> 8U) & 0xffU, record.begin & 0xffU,
    //     (inclusiveEnd >> 24U) & 0xffU, (inclusiveEnd >> 16U) & 0xffU,
    //     (inclusiveEnd >> 8U) & 0xffU, inclusiveEnd & 0xffU)
    // CFileLog::g_pDefaultFileLog->Log(LOG_WARN,
    //     u"[ip-filter] \"%s\", line %d overlaps %u.%u.%u.%u-%u.%u.%u.%u. Merging the ranges.",
    //     path, lineNumber,
    //     (record.begin >> 24U) & 0xffU, (record.begin >> 16U) & 0xffU,
    //     (record.begin >> 8U) & 0xffU, record.begin & 0xffU,
    //     (inclusiveEnd >> 24U) & 0xffU, (inclusiveEnd >> 16U) & 0xffU,
    //     (inclusiveEnd >> 8U) & 0xffU, inclusiveEnd & 0xffU)
    L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
        u"[ip-filter] \"%s\", line %d overlaps %u.%u.%u.%u-%u.%u.%u.%u. Merging the ranges.",
        path, lineNumber,
        (record.begin >> 24U) & 0xffU, (record.begin >> 16U) & 0xffU,
        (record.begin >> 8U) & 0xffU, record.begin & 0xffU,
        (inclusiveEnd >> 24U) & 0xffU, (inclusiveEnd >> 16U) & 0xffU,
        (inclusiveEnd >> 8U) & 0xffU, inclusiveEnd & 0xffU);
}

} // namespace

// AuthGateD.exe 0x00403C20, IPList.cpp.
CIPList::CIPList() = default;

// AuthGateD.exe 0x00402CA0, IPList.cpp.
bool CIPList::IpExists(in_addr address) {
    const std::uint32_t hostAddress = ntohl(address.s_addr);
    const auto found = std::upper_bound(
        m_table.begin(),
        m_table.end(),
        hostAddress,
        [](std::uint32_t value, const IPRecord& record) {
            return value < record.end;
        });
    return found != m_table.end() && found->begin <= hostAddress;
}

// AuthGateD.exe 0x00403C80, IPList.cpp.
bool CIPList::Load(const char16_t* fileName) {
    const std::string path = NativePath(fileName);
    // Report the actual (legacy ASCII-converted) path used to open the file.
    const std::u16string diagnosticPath = Utf8ToUtf16(path);
    // Capture only errno produced by this open, not an earlier operation.
    const int previousError = errno;
    errno = 0;
    std::ifstream input(path);
    const int openError = errno;
    if (openError == 0) errno = previousError;
    if (!input) {
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=ip_filter event=open_failed file=\"%s\" action=retain_previous_ranges ranges=%llu",
        //     diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[ip-filter] Could not open \"%s\". Keeping the previous %llu ranges.",
        //     diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()))
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[ip-filter] Could not open \"%s\". Keeping the previous %llu ranges.",
        //     diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()))
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"Could not open IP-filter file \"%s\" (errno %d; 0 means no error code was supplied). Keeping the previous %llu ranges.",
            diagnosticPath.c_str(), openError, static_cast<unsigned long long>(m_table.size()));
        return false;
    }

    std::vector<IPRecord>().swap(m_table);
    char line[1024];
    std::int32_t lineNumber = 0;
    std::uint64_t unparsedLines = 0;
    std::uint64_t rejectedRanges = 0;
    int readError = 0;
    while (true) {
        // Some filebuf implementations expose read errors only through errno.
        // Preserve the previous errno when getline does not set a new one.
        const int previousReadErrno = errno;
        errno = 0;
        input.getline(line, static_cast<std::streamsize>(sizeof(line)));
        const int currentReadErrno = errno;
        if (currentReadErrno == 0) errno = previousReadErrno;
        ++lineNumber;
        if (!input) {
            readError = currentReadErrno;
            break;
        }

        char* cursor = line;
        IPRecord incoming;
        std::uint32_t thirdPart = 0;
        std::uint32_t fourthPart = 0;
        if (!ParseAddress(
                cursor,
                incoming.begin,
                thirdPart,
                fourthPart)) {
            ++unparsedLines;
            continue;
        }

        cursor += std::strspn(cursor, " \t");
        if (*cursor == '-') {
            ++cursor;
            cursor += std::strspn(cursor, " \t");
            std::uint32_t ignoredThird = 0;
            std::uint32_t ignoredFourth = 0;
            if (!ParseAddress(
                    cursor,
                    incoming.end,
                    ignoredThird,
                    ignoredFourth)) {
                ++unparsedLines;
                continue;
            }
            ++incoming.end;
        } else if (fourthPart != 0) {
            incoming.end = incoming.begin + 1U;
        } else if (thirdPart == 0) {
            incoming.end = incoming.begin + 0x10000U;
        } else {
            incoming.end = incoming.begin + 0x100U;
        }

        if (incoming.begin >= incoming.end) {
            ++rejectedRanges;
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"IPList: start address address is greater than end address at line %d",
            //     lineNumber)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=ip_filter event=invalid_range file=\"%s\" line=%d reason=empty_or_reversed_range action=skip_line",
            //     diagnosticPath.c_str(), lineNumber)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[ip-filter] \"%s\", line %d contains an empty or reversed range. Skipping this line.",
            //     diagnosticPath.c_str(), lineNumber)
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[ip-filter] \"%s\", line %d contains an empty or reversed range. Skipping this line.",
                diagnosticPath.c_str(), lineNumber);
            continue;
        }

        auto position = FindContainingOrNext(
            m_table,
            incoming.begin - 1U);
        if (position != m_table.end() &&
            position->begin <= incoming.begin - 1U) {
            if (position->end != incoming.begin) {
                // LogMergedRange(*position, lineNumber)
                LogMergedRange(*position, lineNumber, diagnosticPath.c_str());
            }
            if (incoming.end <= position->end) {
                continue;
            }
            incoming.begin = position->begin;
            position = m_table.erase(position);
        }

        while (position != m_table.end() &&
               position->begin <= incoming.end) {
            if (position->begin != incoming.end) {
                // LogMergedRange(*position, lineNumber)
                LogMergedRange(*position, lineNumber, diagnosticPath.c_str());
            }
            if (incoming.end < position->end) {
                incoming.end = position->end;
            }
            position = m_table.erase(position);
        }
        m_table.insert(position, incoming);
    }

    // CFileLog::g_pDefaultFileLog->Log(
    //     LOG_NORMAL,
    //     u"IPList loaded from %s",
    //     fileName)
    if (readError != 0 || input.bad() || (!input.eof() && input.fail())) {
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=ip_filter event=read_incomplete file=\"%s\" line=%d ranges=%llu stream_bad=%d read_errno=%d max_line_chars=1023 action=retain_loaded_prefix legacy_return=success",
        //     diagnosticPath.c_str(), lineNumber, static_cast<unsigned long long>(m_table.size()),
        //     input.bad() ? 1 : 0, readError)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[ip-filter] Reading \"%s\" stopped at line %d after %llu ranges (stream failure %d, errno %d. Maximum line length 1023). Keeping the loaded prefix. The loader still returns success.",
        //     diagnosticPath.c_str(), lineNumber, static_cast<unsigned long long>(m_table.size()),
        //     input.bad() ? 1 : 0, readError)
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[ip-filter] Reading \"%s\" stopped at line %d after %llu ranges (stream failure %d, errno %d. Maximum line length 1023). Keeping the loaded prefix. The loader still returns success.",
        //     diagnosticPath.c_str(), lineNumber, static_cast<unsigned long long>(m_table.size()), input.bad() ? 1 : 0, readError)
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[ip-filter] Reading \"%s\" stopped at line %d. %llu merged ranges remain (stream failure %d, errno %d. Maximum line length 1023). The previous list was cleared. Only the loaded prefix is retained, and the loader still returns success.",
        //     diagnosticPath.c_str(), lineNumber, static_cast<unsigned long long>(m_table.size()), input.bad() ? 1 : 0, readError)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[ip-filter] Reading \"%s\" stopped at line %d; %llu merged ranges remain, %llu invalid ranges rejected, %llu lines unparsed (stream failure %d, errno %d; maximum line length 1023). The previous list was cleared; only the loaded prefix is retained, and the loader still returns success.",
            diagnosticPath.c_str(), lineNumber, static_cast<unsigned long long>(m_table.size()),
            static_cast<unsigned long long>(rejectedRanges), static_cast<unsigned long long>(unparsedLines),
            input.bad() ? 1 : 0, readError);
    } else {
        // CFileLog::g_pDefaultFileLog->Log(LOG_NORMAL,
        //     u"component=ip_filter event=loaded file=\"%s\" ranges=%llu unparsed_lines=%llu",
        //     diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()),
        //     static_cast<unsigned long long>(unparsedLines))
        // CFileLog::g_pDefaultFileLog->Log(LOG_NORMAL,
        //     u"[ip-filter] Loaded \"%s\": %llu ranges. %llu lines could not be parsed as addresses.",
        //     diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()),
        //     static_cast<unsigned long long>(unparsedLines))
        // Original: L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
        //     u"[ip-filter] Loaded \"%s\": %llu ranges. %llu lines could not be parsed as addresses.",
        //     diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()), static_cast<unsigned long long>(unparsedLines))
        L2_LOG(*CFileLog::g_pDefaultFileLog, rejectedRanges != 0 ? LOG_WARN : LOG_NORMAL,
            u"[ip-filter] Loaded \"%s\": %llu merged ranges, %llu invalid ranges rejected; %llu lines could not be parsed as addresses.",
            diagnosticPath.c_str(), static_cast<unsigned long long>(m_table.size()),
            static_cast<unsigned long long>(rejectedRanges), static_cast<unsigned long long>(unparsedLines));
    }
    return true;
}
