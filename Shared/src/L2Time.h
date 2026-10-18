#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Protocol/src/NpcPacketLogNamesGen.h"
#include "Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/TypeManip.h"

#if defined(L2_BUILD_L2SERVER)
#include "L2Server/src/User.h"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace packet_stat_detail {

inline std::int64_t AtomicLoad(const volatile std::int64_t& value) noexcept {
#if defined(_MSC_VER)
    return _InterlockedCompareExchange64(
        reinterpret_cast<volatile long long*>(const_cast<volatile std::int64_t*>(&value)), 0, 0
    );
#else
    return __atomic_load_n(const_cast<const std::int64_t*>(&value), __ATOMIC_SEQ_CST);
#endif
}

inline void AtomicStore(volatile std::int64_t& target, std::int64_t value) noexcept {
#if defined(_MSC_VER)
    _InterlockedExchange64(reinterpret_cast<volatile long long*>(&target), value);
#else
    __atomic_store_n(const_cast<std::int64_t*>(&target), value, __ATOMIC_SEQ_CST);
#endif
}

inline void AtomicAdd(volatile std::int64_t& target, std::int64_t value) noexcept {
#if defined(_MSC_VER)
    _InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(&target), value);
#else
    __atomic_fetch_add(const_cast<std::int64_t*>(&target), value, __ATOMIC_SEQ_CST);
#endif
}

inline void AtomicIncrement(volatile std::int64_t& target) noexcept {
#if defined(_MSC_VER)
    _InterlockedIncrement64(reinterpret_cast<volatile long long*>(&target));
#else
    __atomic_fetch_add(const_cast<std::int64_t*>(&target), std::int64_t{1}, __ATOMIC_SEQ_CST);
#endif
}

template <int MaxCount, int ExtendedOpcodeBase>
inline constexpr bool IsKnownPacketStat = (MaxCount == 37 && ExtendedOpcodeBase == 37) ||
    (MaxCount == 48 && ExtendedOpcodeBase == 48) || (MaxCount == 103 && ExtendedOpcodeBase == 103) ||
    (MaxCount == 200 && ExtendedOpcodeBase == 200) || (MaxCount == 312 && ExtendedOpcodeBase == 254) ||
    (MaxCount == 506 && ExtendedOpcodeBase == 198) || (MaxCount == 560 && ExtendedOpcodeBase == 208) ||
    (MaxCount == 695 && ExtendedOpcodeBase == 234) || (MaxCount == 768 && ExtendedOpcodeBase == 254);

template <int MaxCount, int ExtendedOpcodeBase>
constexpr const wchar_t* PacketStatReportGuardName() {
    static_assert(
        IsKnownPacketStat<MaxCount, ExtendedOpcodeBase>, "an emitted CPacketStat specialization needs its exact guard name"
    );
    if constexpr (MaxCount == 37 && ExtendedOpcodeBase == 37) {
        return L"void __cdecl CPacketStat<37,37>::Report(enum LogType)";
    } else if constexpr (MaxCount == 48 && ExtendedOpcodeBase == 48) {
        return L"void __cdecl CPacketStat<48,48>::Report(enum LogType)";
    } else if constexpr (MaxCount == 103 && ExtendedOpcodeBase == 103) {
        return L"void __cdecl CPacketStat<103,103>::Report(enum LogType)";
    } else if constexpr (MaxCount == 200 && ExtendedOpcodeBase == 200) {
        return L"void __cdecl CPacketStat<200,200>::Report(enum LogType)";
    } else if constexpr (MaxCount == 312 && ExtendedOpcodeBase == 254) {
        return L"void __cdecl CPacketStat<312,254>::Report(enum LogType)";
    } else if constexpr (MaxCount == 506 && ExtendedOpcodeBase == 198) {
        return L"void __cdecl CPacketStat<506,198>::Report(enum LogType)";
    } else if constexpr (MaxCount == 560 && ExtendedOpcodeBase == 208) {
        return L"void __cdecl CPacketStat<560,208>::Report(enum LogType)";
    } else if constexpr (MaxCount == 695 && ExtendedOpcodeBase == 234) {
        return L"void __cdecl CPacketStat<695,234>::Report(enum LogType)";
    } else {
        return L"void __cdecl CPacketStat<768,254>::Report(enum LogType)";
    }
}

template <int MaxCount, int ExtendedOpcodeBase>
constexpr const wchar_t* PacketStatReportRankGuardName() {
    static_assert(
        IsKnownPacketStat<MaxCount, ExtendedOpcodeBase>, "an emitted CPacketStat specialization needs its exact guard name"
    );
    if constexpr (MaxCount == 37 && ExtendedOpcodeBase == 37) {
        return L"void __cdecl CPacketStat<37,37>::ReportRank(enum LogType,"
               L"enum CPacketStat<37,37>::PredType)";
    } else if constexpr (MaxCount == 48 && ExtendedOpcodeBase == 48) {
        return L"void __cdecl CPacketStat<48,48>::ReportRank(enum LogType,"
               L"enum CPacketStat<48,48>::PredType)";
    } else if constexpr (MaxCount == 103 && ExtendedOpcodeBase == 103) {
        return L"void __cdecl CPacketStat<103,103>::ReportRank(enum LogType,"
               L"enum CPacketStat<103,103>::PredType)";
    } else if constexpr (MaxCount == 200 && ExtendedOpcodeBase == 200) {
        return L"void __cdecl CPacketStat<200,200>::ReportRank(enum LogType,"
               L"enum CPacketStat<200,200>::PredType)";
    } else if constexpr (MaxCount == 312 && ExtendedOpcodeBase == 254) {
        return L"void __cdecl CPacketStat<312,254>::ReportRank(enum LogType,"
               L"enum CPacketStat<312,254>::PredType)";
    } else if constexpr (MaxCount == 506 && ExtendedOpcodeBase == 198) {
        return L"void __cdecl CPacketStat<506,198>::ReportRank(enum LogType,"
               L"enum CPacketStat<506,198>::PredType)";
    } else if constexpr (MaxCount == 560 && ExtendedOpcodeBase == 208) {
        return L"void __cdecl CPacketStat<560,208>::ReportRank(enum LogType,"
               L"enum CPacketStat<560,208>::PredType)";
    } else if constexpr (MaxCount == 695 && ExtendedOpcodeBase == 234) {
        return L"void __cdecl CPacketStat<695,234>::ReportRank(enum LogType,"
               L"enum CPacketStat<695,234>::PredType)";
    } else {
        return L"void __cdecl CPacketStat<768,254>::ReportRank(enum LogType,"
               L"enum CPacketStat<768,254>::PredType)";
    }
}

// NPC traffic uses the published callback name and wire opcode.
// Keep this diagnostic formatting outside CPacketStat's recovered object layout.
inline bool ReportNpcPacketEntry(
    LogType logType, const wchar_t* name, bool extended, int number, std::int64_t count, std::int64_t bytes, int rank = 0
) {
    const wchar_t* const direction = npc_packet_log::Direction(name ? name : L"");
    if (direction == nullptr) {
        return false;
    }
    const wchar_t* const packet = npc_packet_log::PacketName(name, extended, number);
    const std::wstring rankLabel = rank > 0 ? L"rank=" + std::to_wstring(rank) + L", " : L"";
    if (extended) {
        L2_LOG(
            Log, logType,
            u"[traffic] %ls: %lspacket=%ls opcode=0xFE ext_opcode=0x%04X (%d), "
            u"count=%lld, frame_bytes=%lld.",
            direction, rankLabel.c_str(), packet ? packet : L"unknown", static_cast<unsigned int>(number), number,
            static_cast<long long>(count), static_cast<long long>(bytes)
        );
    } else {
        L2_LOG(
            Log, logType,
            u"[traffic] %ls: %lspacket=%ls opcode=0x%02X (%d), "
            u"count=%lld, frame_bytes=%lld.",
            direction, rankLabel.c_str(), packet ? packet : L"unknown", static_cast<unsigned int>(number), number,
            static_cast<long long>(count), static_cast<long long>(bytes)
        );
    }
    return true;
}

// Operator presentation only: keep each counter and the caller's severity.
inline void ReportPacketEntry(
    LogType logType,
    const wchar_t* name,
    const wchar_t* kind,
    bool present,
    int number,
    std::int64_t count,
    std::int64_t bytes
) {
    // Noise: Missing tail slots and zero counters add no information to a report.
    // Negative nonzero counters remain visible. They are not treated as empty.
    if (!present || (count == 0 && bytes == 0)) {
        return;
    }
    // keep the original numeric format below for other links.
    if (ReportNpcPacketEntry(logType, name, kind && std::wcscmp(kind, L"extended entry") == 0, number, count, bytes)) {
        return;
    }
    // Log.Add(logType,
    //     L"[traffic] %ls: %ls %d, count %lld, bytes %lld.",
    //     name && *name ? name : L"(unnamed)", kind, number,
    //     static_cast<long long>(count), static_cast<long long>(bytes))
    L2_LOG(
        Log, logType, u"[traffic] %ls: %ls %d, count %lld, bytes %lld.", name && *name ? name : L"(unnamed)", kind, number,
        static_cast<long long>(count), static_cast<long long>(bytes)
    );
}

} // namespace packet_stat_detail

// Shared L2Time.h template recovered from the current L2NPC and CacheD images,
// with the kept TotalChecker2015.pdb type record used only after both current
// machine layouts agree. The class is in the global namespace:
// that is how both binaries decorate its template symbols.
template <int MaxCount, int ExtendedOpcodeBase>
class CPacketStat {
private:
    Loki::CompileTimeError<(MaxCount >= ExtendedOpcodeBase) ? 1 : 0> ERROR_MustGreaterThanEx;

public:
    struct PACKETSTAT {
        int packetNo;
        volatile std::int64_t cnt;
        volatile std::int64_t bytes;

        PACKETSTAT() noexcept : packetNo(0), cnt(0), bytes(0) {}

        PACKETSTAT(int packet, std::int64_t count, std::int64_t byteCount) noexcept :
            packetNo(packet), cnt(count), bytes(byteCount) {}

        PACKETSTAT(const PACKETSTAT& other) noexcept :
            PACKETSTAT(
                other.packetNo, packet_stat_detail::AtomicLoad(other.cnt), packet_stat_detail::AtomicLoad(other.bytes)
            ) {}

        PACKETSTAT(PACKETSTAT&& other) noexcept : PACKETSTAT(other) {}

        PACKETSTAT& operator=(const PACKETSTAT& other) noexcept {
            packetNo = other.packetNo;
            packet_stat_detail::AtomicStore(cnt, packet_stat_detail::AtomicLoad(other.cnt));
            packet_stat_detail::AtomicStore(bytes, packet_stat_detail::AtomicLoad(other.bytes));
            return *this;
        }

        PACKETSTAT& operator=(PACKETSTAT&& other) noexcept { return operator=(other); }
    };

    enum PredType {
        PRED_CNT = 0,
        PRED_BYTES = 1,
    };

    struct CntPred {
        bool operator()(const PACKETSTAT& left, const PACKETSTAT& right) const noexcept {
            return packet_stat_detail::AtomicLoad(left.cnt) > packet_stat_detail::AtomicLoad(right.cnt);
        }
    };

    struct BytesPred {
        bool operator()(const PACKETSTAT& left, const PACKETSTAT& right) const noexcept {
            return packet_stat_detail::AtomicLoad(left.bytes) > packet_stat_detail::AtomicLoad(right.bytes);
        }
    };

    static CPacketStat& Inst() {
        static CPacketStat inst;
        return inst;
    }

    void Init(const wchar_t* name) {
        std::size_t index = 0;
        if (name) {
            while (index + 1 < std::size(name_) && name[index] != L'\0') {
                name_[index] = static_cast<char16_t>(name[index]);
                ++index;
            }
        }
        name_[index] = u'\0';
        while (++index < std::size(name_)) {
            name_[index] = u'\0';
        }
        Reset();
    }

    void Reset() {
        for (int packetNo = 0; packetNo < MaxCount; ++packetNo) {
            PACKETSTAT& entry = stat_[static_cast<std::size_t>(packetNo)];
            entry.packetNo = packetNo;
            packet_stat_detail::AtomicStore(entry.cnt, 0);
            packet_stat_detail::AtomicStore(entry.bytes, 0);
        }
        packet_stat_detail::AtomicStore(totalBytes_, 0);
        lastTick_ = static_cast<std::int32_t>(runtime::GetTickCount());
    }

    void Report(LogType logType) {
        L2_GUARD_FUNCTION();
        const std::int32_t elapsed = static_cast<std::int32_t>(runtime::GetTickCount() - static_cast<std::uint32_t>(lastTick_));
#if defined(L2_BUILD_L2SERVER)
        const int currentUserCount = g_UserMap.GetSize();
#endif
        // const std::int64_t bytesPerSecond =
        //     runtime::Win64SignedDivide(
        //         runtime::MultiplyInt64Wrapping(
        //             packet_stat_detail::AtomicLoad(
        //                 totalBytes_),
        //             INT64_C(1000)),
        //         static_cast<std::int64_t>(
        //             elapsed))
        // An immediate report after Reset has no measurable interval.
        // Avoid a diagnostic-only divide fault. Counters and reset time are untouched.
        const std::int64_t bytesPerSecond = elapsed > 0
            ? runtime::Win64SignedDivide(
                  runtime::MultiplyInt64Wrapping(packet_stat_detail::AtomicLoad(totalBytes_), INT64_C(1000)),
                  static_cast<std::int64_t>(elapsed)
              )
            : 0;
        const std::wstring name = NameForLog();
        // state the full-frame byte unit for NPC traffic only.
        // Send counters measure prepared frames. They do not acknowledge delivery.
        const wchar_t* const npcDirection = npc_packet_log::Direction(name);
        if (elapsed <= 0) {
            // Log.Add(logType,
            //     L"[traffic] %ls: average rate unavailable. The counter interval is %d ms.",
            //     name.empty() ? L"(unnamed)" : name.c_str(), elapsed)
            L2_LOG(
                Log, logType, u"[traffic] %ls: average rate unavailable; the counter interval is %d ms.",
                name.empty() ? L"(unnamed)" : name.c_str(), elapsed
            );
        } else if (npcDirection != nullptr) {
            L2_LOG(
                Log, logType,
                u"[traffic] %ls: interval_ms=%d, frame_bytes=%lld, average_bytes_per_second=%lld. "
                u"Bytes include packet headers; TCP/IP overhead excluded.",
                npcDirection, elapsed, static_cast<long long>(packet_stat_detail::AtomicLoad(totalBytes_)),
                static_cast<long long>(bytesPerSecond)
            );
#if defined(L2_BUILD_L2SERVER)
            L2_LOG(Log, logType, u"[traffic] %ls: connected_users=%d.", npcDirection, currentUserCount);
#endif
        } else {
#if defined(L2_BUILD_L2SERVER)
            // Log.Add(
            //     logType,
            //     L"[%s] Total bytes/sec = %I64d "
            //     L"CurrentUserCount = %d",
            //     name.c_str(),
            //     bytesPerSecond,
            //     currentUserCount)
            // Log.Add(
            //     logType,
            //     L"[traffic] %ls: average %lld bytes/s over %d ms. Connected users %d.",
            //     name.empty() ? L"(unnamed)" : name.c_str(),
            //     static_cast<long long>(bytesPerSecond), elapsed, currentUserCount)
            L2_LOG(
                Log, logType, u"[traffic] %ls: average %lld bytes/s over %d ms; connected users %d.",
                name.empty() ? L"(unnamed)" : name.c_str(), static_cast<long long>(bytesPerSecond), elapsed, currentUserCount
            );
#else
            // Log.Add(
            //     logType,
            //     L"[%s] Total bytes/sec = %I64d",
            //     name.c_str(),
            //     bytesPerSecond)
            // Log.Add(
            //     logType,
            //     L"[traffic] %ls: average %lld bytes/s over %d ms.",
            //     name.empty() ? L"(unnamed)" : name.c_str(),
            //     static_cast<long long>(bytesPerSecond), elapsed)
            L2_LOG(
                Log, logType, u"[traffic] %ls: average %lld bytes/s over %d ms.", name.empty() ? L"(unnamed)" : name.c_str(),
                static_cast<long long>(bytesPerSecond), elapsed
            );
#endif
        }

        for (int first = 0; first < ExtendedOpcodeBase; first += 4) {
            const PACKETSTAT firstEntry = ReadOrMissing(first, ExtendedOpcodeBase);
            const PACKETSTAT secondEntry = ReadOrMissing(first + 1, ExtendedOpcodeBase);
            const PACKETSTAT thirdEntry = ReadOrMissing(first + 2, ExtendedOpcodeBase);
            const PACKETSTAT fourthEntry = ReadOrMissing(first + 3, ExtendedOpcodeBase);
            // Log.Add(
            //     logType,
            //     L"[%s] %3d:c[%I64d]b[%I64d] "
            //     L"%3d:c[%I64d]b[%I64d] "
            //     L"%3d:c[%I64d]b[%I64d] "
            //     L"%3d:c[%I64d]b[%I64d]",
            //     name.c_str(),
            //     firstEntry.packetNo,
            //     packet_stat_detail::AtomicLoad(
            //         firstEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         firstEntry.bytes),
            //     secondEntry.packetNo,
            //     packet_stat_detail::AtomicLoad(
            //         secondEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         secondEntry.bytes),
            //     thirdEntry.packetNo,
            //     packet_stat_detail::AtomicLoad(
            //         thirdEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         thirdEntry.bytes),
            //     fourthEntry.packetNo,
            //     packet_stat_detail::AtomicLoad(
            //         fourthEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         fourthEntry.bytes))
            // Noise: Omit zero counters and missing tail slots. Report the remaining entries individually.
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"entry", first + 0 < ExtendedOpcodeBase, firstEntry.packetNo,
                packet_stat_detail::AtomicLoad(firstEntry.cnt), packet_stat_detail::AtomicLoad(firstEntry.bytes)
            );
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"entry", first + 1 < ExtendedOpcodeBase, secondEntry.packetNo,
                packet_stat_detail::AtomicLoad(secondEntry.cnt), packet_stat_detail::AtomicLoad(secondEntry.bytes)
            );
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"entry", first + 2 < ExtendedOpcodeBase, thirdEntry.packetNo,
                packet_stat_detail::AtomicLoad(thirdEntry.cnt), packet_stat_detail::AtomicLoad(thirdEntry.bytes)
            );
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"entry", first + 3 < ExtendedOpcodeBase, fourthEntry.packetNo,
                packet_stat_detail::AtomicLoad(fourthEntry.cnt), packet_stat_detail::AtomicLoad(fourthEntry.bytes)
            );
        }

        for (int first = ExtendedOpcodeBase; first < MaxCount; first += 4) {
            const PACKETSTAT firstEntry = ReadOrMissing(first, MaxCount);
            const PACKETSTAT secondEntry = ReadOrMissing(first + 1, MaxCount);
            const PACKETSTAT thirdEntry = ReadOrMissing(first + 2, MaxCount);
            const PACKETSTAT fourthEntry = ReadOrMissing(first + 3, MaxCount);
            // Log.Add(
            //     logType,
            //     L"[%s] EX %3d:h[%I64d]b[%I64d] "
            //     L"%3d:h[%I64d]b[%I64d] "
            //     L"%3d:h[%I64d]b[%I64d] "
            //     L"%3d:h[%I64d]b[%I64d]",
            //     name.c_str(),
            //     ExtendedPacketNo(firstEntry),
            //     packet_stat_detail::AtomicLoad(
            //         firstEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         firstEntry.bytes),
            //     ExtendedPacketNo(secondEntry),
            //     packet_stat_detail::AtomicLoad(
            //         secondEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         secondEntry.bytes),
            //     ExtendedPacketNo(thirdEntry),
            //     packet_stat_detail::AtomicLoad(
            //         thirdEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         thirdEntry.bytes),
            //     ExtendedPacketNo(fourthEntry),
            //     packet_stat_detail::AtomicLoad(
            //         fourthEntry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         fourthEntry.bytes))
            // Noise: Omit zero counters and missing tail slots. Report the remaining entries individually.
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"extended entry", first + 0 < MaxCount, ExtendedPacketNo(firstEntry),
                packet_stat_detail::AtomicLoad(firstEntry.cnt), packet_stat_detail::AtomicLoad(firstEntry.bytes)
            );
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"extended entry", first + 1 < MaxCount, ExtendedPacketNo(secondEntry),
                packet_stat_detail::AtomicLoad(secondEntry.cnt), packet_stat_detail::AtomicLoad(secondEntry.bytes)
            );
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"extended entry", first + 2 < MaxCount, ExtendedPacketNo(thirdEntry),
                packet_stat_detail::AtomicLoad(thirdEntry.cnt), packet_stat_detail::AtomicLoad(thirdEntry.bytes)
            );
            packet_stat_detail::ReportPacketEntry(
                logType, name.c_str(), L"extended entry", first + 3 < MaxCount, ExtendedPacketNo(fourthEntry),
                packet_stat_detail::AtomicLoad(fourthEntry.cnt), packet_stat_detail::AtomicLoad(fourthEntry.bytes)
            );
        }
    }

    void ReportRank(LogType logType, PredType predicate) {
        L2_GUARD_FUNCTION();
        std::vector<PACKETSTAT, std::allocator<PACKETSTAT>> sorted;
        std::copy(std::begin(stat_), std::begin(stat_) + MaxCount, std::back_inserter(sorted));

        if (predicate == PRED_CNT) {
            std::stable_sort(sorted.begin(), sorted.end(), CntPred{});
        } else {
            std::stable_sort(sorted.begin(), sorted.end(), BytesPred{});
        }

        const std::wstring name = NameForLog();
        // const wchar_t* const predicateName =
        //     condition == PRED_BYTES
        //         ? L"Byte"
        //         : L"Count".
        const wchar_t* const predicateName = predicate == PRED_CNT ? L"count" : L"bytes";
        const int reportCount = std::min<int>(10, static_cast<int>(sorted.size()));
        // Log.Add(logType,
        //     L"[traffic] %ls: top %d entries by %ls (zero counters omitted).",
        //     name.empty() ? L"(unnamed)" : name.c_str(), reportCount, predicateName)
        // identify the direction, reset interval and byte unit.
        // Preserve the original report format for non-NPC protocol counters.
        const wchar_t* const npcDirection = npc_packet_log::Direction(name);
        if (npcDirection != nullptr) {
            const std::uint32_t elapsed = runtime::GetTickCount() - static_cast<std::uint32_t>(lastTick_);
            L2_LOG(
                Log, logType,
                u"[traffic] %ls: top %d packet types by %ls, interval_ms=%u "
                u"since counter reset. Bytes include packet headers; TCP/IP overhead excluded; zero counters omitted.",
                npcDirection, reportCount, predicateName, static_cast<unsigned int>(elapsed)
            );
        } else {
            L2_LOG(
                Log, logType, u"[traffic] %ls: top %d entries by %ls (zero counters omitted).",
                name.empty() ? L"(unnamed)" : name.c_str(), reportCount, predicateName
            );
        }
        for (int rank = 0; rank < reportCount; ++rank) {
            const PACKETSTAT& entry = sorted[static_cast<std::size_t>(rank)];
            const bool extended = entry.packetNo >= ExtendedOpcodeBase;
            // Log.Add(
            //     logType,
            //     L"[%s][%s][%d] %3d:%s"
            //     L"[%I64d]b[%I64d]",
            //     name.c_str(),
            //     predicateName,
            //     rank,
            //     extended
            //         ? entry.packetNo -
            //               ExtendedOpcodeBase
            //         : entry.packetNo,
            //     extended ? L"h" : L"c",
            //     packet_stat_detail::AtomicLoad(
            //         entry.cnt),
            //     packet_stat_detail::AtomicLoad(
            //         entry.bytes))
            const auto count = packet_stat_detail::AtomicLoad(entry.cnt);
            const auto bytes = packet_stat_detail::AtomicLoad(entry.bytes);
            // Noise: A ranking entry with both counters zero carries no traffic information.
            if (count == 0 && bytes == 0) {
                continue;
            }
            // named packet rows share one format with Report().
            if (packet_stat_detail::ReportNpcPacketEntry(
                    logType, name.c_str(), extended, extended ? entry.packetNo - ExtendedOpcodeBase : entry.packetNo, count,
                    bytes, rank + 1
                )) {
                continue;
            }
            // Log.Add(
            //     logType,
            //     L"[traffic] %ls: rank %d by %ls, %ls %d, count %lld, bytes %lld.",
            //     name.empty() ? L"(unnamed)" : name.c_str(), rank + 1,
            //     predicateName, extended ? L"extended entry" : L"entry",
            //     extended ? entry.packetNo - ExtendedOpcodeBase : entry.packetNo,
            //     static_cast<long long>(count), static_cast<long long>(bytes))
            L2_LOG(
                Log, logType, u"[traffic] %ls: rank %d by %ls, %ls %d, count %lld, bytes %lld.",
                name.empty() ? L"(unnamed)" : name.c_str(), rank + 1, predicateName, extended ? L"extended entry" : L"entry",
                extended ? entry.packetNo - ExtendedOpcodeBase : entry.packetNo, static_cast<long long>(count),
                static_cast<long long>(bytes)
            );
        }
    }

    void Accumulate(int packetNo, int bytes) {
        packet_stat_detail::AtomicAdd(totalBytes_, static_cast<std::int64_t>(bytes));
        PACKETSTAT& entry = stat_[static_cast<std::size_t>(packetNo)];
        packet_stat_detail::AtomicIncrement(entry.cnt);
        packet_stat_detail::AtomicAdd(entry.bytes, static_cast<std::int64_t>(bytes));
    }

private:
    CPacketStat() = default;
    ~CPacketStat() = default;

    PACKETSTAT ReadEntry(int index) const noexcept {
        const PACKETSTAT& entry = stat_[static_cast<std::size_t>(index)];
        return PACKETSTAT(
            entry.packetNo, packet_stat_detail::AtomicLoad(entry.cnt), packet_stat_detail::AtomicLoad(entry.bytes)
        );
    }

    PACKETSTAT ReadOrMissing(int index, int end) const noexcept {
        if (index >= end) {
            return PACKETSTAT(-1, -1, -1);
        }
        return ReadEntry(index);
    }

    static int ExtendedPacketNo(const PACKETSTAT& entry) noexcept {
        return entry.packetNo < 0 ? -1 : entry.packetNo - ExtendedOpcodeBase;
    }

    std::wstring NameForLog() const {
        std::wstring result;
        result.reserve(std::size(name_));
        for (const char16_t value : name_) {
            if (value == u'\0') {
                break;
            }
            result.push_back(static_cast<wchar_t>(value));
        }
        return result;
    }

    volatile std::int32_t lastTick_ = 0;
    volatile std::int64_t totalBytes_ = 0;
    char16_t name_[64]{};
    PACKETSTAT stat_[MaxCount + 1]{};
};


// Current L2Server NPC.cpp send-info counter. Unlike CNpcStat below this
// owner has no vtable: its complete physical state is the 40,000-entry table.
class CSendNPCInfoStat {
public:
    struct SENDNPCINFOSTAT {
        std::int32_t npc_class_id{};
        std::int32_t alignment04{};
        volatile std::int64_t cnt{};

        SENDNPCINFOSTAT() noexcept = default;

        SENDNPCINFOSTAT(const SENDNPCINFOSTAT& other) noexcept :
            npc_class_id(other.npc_class_id),
            alignment04(other.alignment04),
            cnt(packet_stat_detail::AtomicLoad(other.cnt)) {}

        SENDNPCINFOSTAT& operator=(const SENDNPCINFOSTAT& other) noexcept {
            npc_class_id = other.npc_class_id;
            alignment04 = other.alignment04;
            packet_stat_detail::AtomicStore(cnt, packet_stat_detail::AtomicLoad(other.cnt));
            return *this;
        }
    };

    struct CntPred {
        bool operator()(const SENDNPCINFOSTAT& left, const SENDNPCINFOSTAT& right) const noexcept {
            return packet_stat_detail::AtomicLoad(left.cnt) > packet_stat_detail::AtomicLoad(right.cnt);
        }
    };

    static CSendNPCInfoStat& Inst() {
        static CSendNPCInfoStat instance;
        return instance;
    }

    void Init() noexcept { Reset(); }

    void Reset() noexcept {
        for (std::size_t index = 0; index < std::size(stat_); ++index) {
            stat_[index].npc_class_id = static_cast<std::int32_t>(index);
            stat_[index].alignment04 = 0;
            packet_stat_detail::AtomicStore(stat_[index].cnt, 0);
        }
    }

    void Report(LogType logType) {
        L2_GUARD_FUNCTION();
        std::vector<SENDNPCINFOSTAT, std::allocator<SENDNPCINFOSTAT>> sorted;
        std::copy(std::begin(stat_), std::end(stat_), std::back_inserter(sorted));
        std::stable_sort(sorted.begin(), sorted.end(), CntPred{});

        const int reportCount = std::min<int>(20, static_cast<int>(sorted.size()));
        // Log.Add(logType,
        //     L"[traffic] NPC info sends: top %d template indices by count (zero counters omitted).",
        //     reportCount)
        L2_LOG(
            Log, logType, u"[traffic] NPC info sends: top %d template indices by count (zero counters omitted).", reportCount
        );
        for (int rank = 0; rank < reportCount; ++rank) {
            const SENDNPCINFOSTAT& entry = sorted[static_cast<std::size_t>(rank)];
            // Log.Add(
            //     logType,
            //     L"[SNIS][%d] %d:%I64d",
            //     rank,
            //     entry.npc_class_id,
            //     packet_stat_detail::AtomicLoad(
            //         entry.cnt))
            const auto count = packet_stat_detail::AtomicLoad(entry.cnt);
            // Noise: Templates with no recorded sends add nothing to this ranking.
            if (count == 0) {
                continue;
            }
            // Log.Add(
            //     logType,
            //     L"[traffic] NPC info sends: rank %d, template index %d, count %lld.",
            //     rank + 1, entry.npc_class_id, static_cast<long long>(count))
            L2_LOG(
                Log, logType, u"[traffic] NPC info sends: rank %d, template index %d, count %lld.", rank + 1,
                entry.npc_class_id, static_cast<long long>(count)
            );
        }
    }

    void Accumulate(unsigned int npcClassId) noexcept {
        const unsigned int index = npcClassId - 1000000U;
        if (index >= std::size(stat_)) {
            return;
        }
        packet_stat_detail::AtomicIncrement(stat_[index].cnt);
    }

private:
    CSendNPCInfoStat() = default;

    SENDNPCINFOSTAT stat_[40000]{};
};
