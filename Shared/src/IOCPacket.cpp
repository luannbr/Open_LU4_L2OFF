#include "Shared/src/Log.h"

#include "Shared/src/IOCPacket.h"
#include "TextRuntime.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "platform/Runtime.h"

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

// These functions are owned by the original Shared/ioc.cpp source. They are a
// separate translation unit only because a static archive links at object
// granularity: pulling the codec must not also pull either executable's I/O
// threads, socket runtime, or NPC-maker globals. Current Cached.exe and
// L2NPC.exe confirm the same wire implementation at the addresses below.

namespace {

std::size_t Utf16LeLength(const unsigned char* value) noexcept {
    std::size_t length = 0;
    while (target_utf16::LoadLittleEndian(value + length * sizeof(char16_t)) != u'\0') {
        ++length;
    }
    return length;
}

// Noise: repeated capacity failures can occur once per remaining row of a
// response. Preserve the first diagnostic/stack for each format prefix, field
// and capacity on each thread. Summarize repeats on its next report after ten
// seconds or when its bounded cache entry is replaced. No timer or shutdown
// callback calls the logger, and no packet values or borrowed pointers are kept.
// int ReportAssembleOverflow() {
//     Log.Add(
//         LOG_ERROR,
//         "Buffer overflow in Assemble")
//     runtime::process::ReportNamedCallStack(
//         L"BUFOVER",
//         2,
//         false)
//     return 0
// }
struct AssembleOverflowLogEntry {
    bool used = false;
    std::uint64_t fingerprint = 0;
    std::size_t fieldOffset = 0;
    int capacity = 0;
    unsigned char field = 0;
    std::uint32_t reportTick = 0;
    std::uint64_t suppressed = 0;
    char prefix[161]{};
    bool prefixTruncated = false;
};

struct AssembleOverflowLogState {
    AssembleOverflowLogEntry entries[16]{};
    bool reporting = false;
};

int ReportAssembleOverflow(const char* format, const char* field, int capacity, std::ptrdiff_t writeOffset) {
    // Diagnostic state is thread-local, bounded and trivially destructible.
    // It adds no lock or lifetime dependency to the codec's failure path.
    static thread_local AssembleOverflowLogState state;
    if (state.reporting) {
        // Noise: a failure while the logger/stack reporter is assembling its
        // own output must not recursively print another diagnostic or stack.
        return 0;
    }
    struct ReportingScope {
        bool& reporting;
        explicit ReportingScope(bool& value) : reporting(value) { reporting = true; }
        ~ReportingScope() { reporting = false; }
    } reportingScope(state.reporting);

    const std::size_t fieldOffset = static_cast<std::size_t>(field - format);
    // Hash only format bytes the caller has already examined, including this
    // field. Do not scan the unvisited suffix or keep the caller's storage.
    std::uint64_t fingerprint = 14695981039346656037ULL;
    for (std::size_t index = 0; index <= fieldOffset; ++index) {
        fingerprint ^= static_cast<unsigned char>(format[index]);
        fingerprint *= 1099511628211ULL;
    }
    const std::uint32_t now = runtime::GetTickCount();
    AssembleOverflowLogEntry* entry = nullptr;
    for (auto& candidate : state.entries) {
        if (candidate.used && candidate.fingerprint == fingerprint && candidate.fieldOffset == fieldOffset &&
            candidate.capacity == capacity) {
            entry = &candidate;
            if (static_cast<std::uint32_t>(now - entry->reportTick) < 10000U) {
                if (entry->suppressed != UINT64_MAX) {
                    ++entry->suppressed;
                }
                return 0;
            }
            break;
        }
    }
    if (entry == nullptr) {
        entry = &state.entries[0];
        for (auto& candidate : state.entries) {
            if (!candidate.used) {
                entry = &candidate;
                break;
            }
            if (static_cast<std::uint32_t>(now - candidate.reportTick) >
                static_cast<std::uint32_t>(now - entry->reportTick)) {
                entry = &candidate;
            }
        }
    }
    if (entry->suppressed != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[packet encode] Suppressed %s%llu repeated buffer-overflow reports on this thread "
        //     "(field '%c', format offset %llu, capacity %d bytes, format prefix: %s%s).",
        //     entry->suppressed == UINT64_MAX ? "at least " : "",
        //     static_cast<unsigned long long>(entry->suppressed), static_cast<int>(entry->field),
        //     static_cast<unsigned long long>(entry->fieldOffset), entry->capacity, entry->prefix,
        //     entry->prefixTruncated ? " [prefix truncated]" : ""
        // )
        L2_LOG_ERROR(
            Log,
            "[packet encode] Suppressed %s%llu repeated buffer-overflow reports on this thread "
            "(field '%c', format offset %llu, capacity %d bytes, format prefix: %s%s).",
            entry->suppressed == UINT64_MAX ? "at least " : "", static_cast<unsigned long long>(entry->suppressed),
            static_cast<int>(entry->field), static_cast<unsigned long long>(entry->fieldOffset), entry->capacity,
            entry->prefix, entry->prefixTruncated ? " [prefix truncated]" : ""
        );
    }
    *entry = AssembleOverflowLogEntry{};
    entry->used = true;
    entry->fingerprint = fingerprint;
    entry->fieldOffset = fieldOffset;
    entry->capacity = capacity;
    entry->field = static_cast<unsigned char>(*field);
    entry->reportTick = now;
    const std::size_t prefixLength = fieldOffset < 160 ? fieldOffset + 1 : 160;
    std::memcpy(entry->prefix, format, prefixLength);
    entry->prefixTruncated = fieldOffset >= 160;
    // Log.Add(
    //     LOG_ERROR,
    //     "[packet encode] Field '%c' at format offset %llu exceeds buffer capacity %d bytes "
    //     "(write offset %lld, format prefix: %s%s). Assembly returns zero. Repeats on this thread "
    //     "are summarized at the next report after ten seconds.",
    //     static_cast<int>(entry->field), static_cast<unsigned long long>(fieldOffset), capacity,
    //     static_cast<long long>(writeOffset), entry->prefix,
    //     entry->prefixTruncated ? " [prefix truncated]" : ""
    // )
    L2_LOG_ERROR(
        Log,
        "[packet encode] Field '%c' at format offset %llu exceeds buffer capacity %d bytes "
        "(write offset %lld, format prefix: %s%s). Assembly returns zero; repeats on this thread "
        "are summarized at the next report after ten seconds.",
        static_cast<int>(entry->field), static_cast<unsigned long long>(fieldOffset), capacity,
        static_cast<long long>(writeOffset), entry->prefix, entry->prefixTruncated ? " [prefix truncated]" : ""
    );
    runtime::process::ReportNamedCallStack(L"BUFOVER", 2, false);
    return 0;
}

} // namespace

const char* StrNChr(const char* str, char character, std::uint64_t length) {
    // Current L2Server.exe 0x007cc1c8. Current Cached.exe 0x004e5ae4.
    // current L2NPC.exe 0x00454b9c.
    L2_GUARD_FUNCTION();
    while (length != 0 && *str != character) {
        ++str;
        --length;
    }
    return str;
}

const unsigned char* VDisassemble(const unsigned char* packet, const char* format, va_list arguments) {
    // Current L2Server.exe 0x007cc278. Current Cached.exe 0x004e5b94.
    // current L2NPC.exe 0x00454c4c.
    // The Windows source receives va_list as a
    // value-type char pointer. Use a copy so hosts with array-based va_list
    // keep the same caller-visible behavior.
    L2_GUARD_FUNCTION();
    va_list cursorArguments;
    va_copy(cursorArguments, arguments);

    for (const char* cursor = format; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
            case 'B': {
                char* const lengthOutput = va_arg(cursorArguments, char*);
                void* const destination = va_arg(cursorArguments, void*);
                const std::ptrdiff_t length = static_cast<std::int8_t>(*packet);
                *lengthOutput = static_cast<char>(*packet);
                std::memcpy(destination, packet + 1, static_cast<std::size_t>(length));
                packet += 1 + length;
                break;
            }
            case 'F':
            case 'd': {
                void* const destination = va_arg(cursorArguments, void*);
                std::memcpy(destination, packet, sizeof(std::uint32_t));
                packet += sizeof(std::uint32_t);
                break;
            }
            case 'Q':
            case 'f':
            case 'p': {
                void* const destination = va_arg(cursorArguments, void*);
                std::memcpy(destination, packet, sizeof(std::uint64_t));
                packet += sizeof(std::uint64_t);
                break;
            }
            case 'S': {
                const int capacity = va_arg(cursorArguments, int);
                auto* const destination = va_arg(cursorArguments, char16_t*);
                const std::size_t sourceLength = Utf16LeLength(packet);
                const std::size_t maximumLength =
                    (static_cast<std::uint64_t>(static_cast<std::int64_t>(capacity)) >> 1U) - 1U;
                const std::size_t copyLength = std::min(sourceLength, maximumLength);
                for (std::size_t index = 0; index < copyLength; ++index) {
                    destination[index] = target_utf16::LoadLittleEndian(packet + index * sizeof(char16_t));
                }
                destination[copyLength] = u'\0';
                packet += (copyLength + 1U) * sizeof(char16_t);
                break;
            }
            case 'b': {
                const int length = va_arg(cursorArguments, int);
                void* const destination = va_arg(cursorArguments, void*);
                std::memcpy(destination, packet, static_cast<std::size_t>(length));
                packet += length;
                break;
            }
            case 'c': {
                char* const destination = va_arg(cursorArguments, char*);
                *destination = static_cast<char>(*packet);
                ++packet;
                break;
            }
            case 'e': {
                void* const destination = va_arg(cursorArguments, void*);
                const std::ptrdiff_t length = static_cast<std::int8_t>(*packet);
                ++packet;
                std::memcpy(destination, packet, static_cast<std::size_t>(length));
                packet += length;
                break;
            }
            case 'h': {
                void* const destination = va_arg(cursorArguments, void*);
                std::memcpy(destination, packet, sizeof(std::uint16_t));
                packet += sizeof(std::uint16_t);
                break;
            }
            case 's': {
                const int capacity = va_arg(cursorArguments, int);
                char* const destination = va_arg(cursorArguments, char*);
                const std::size_t boundedCapacity = static_cast<std::size_t>(capacity);
                const char* const source = reinterpret_cast<const char*>(packet);

                // The target calls VS2010
                // strncpy_s(dst, capacity, source, _TRUNCATE). Invalid
                // arguments reach _invalid_parameter_noinfo. If that handler
                // returns, the explicit shipping dst[capacity - 1] store and
                // StrNChr call still execute.
                if (!destination || boundedCapacity == 0U || !source) {
                    if (destination && boundedCapacity != 0U && !source) {
                        destination[0] = '\0';
                    }
                    runtime::process::HandleInvalidParameter();
                } else {
                    std::size_t remaining = boundedCapacity;
                    std::size_t copied = 0;
                    do {
                        const char value = source[copied];
                        destination[copied] = value;
                        ++copied;
                        if (value == '\0') {
                            break;
                        }
                        --remaining;
                    } while (remaining != 0U);
                    if (remaining == 0U) {
                        destination[boundedCapacity - 1U] = '\0';
                    }
                }
                destination[boundedCapacity - 1U] = '\0';

                const char* const sourceEnd = StrNChr(source, '\0', boundedCapacity);
                packet = reinterpret_cast<const unsigned char*>(sourceEnd + 1);
                break;
            }
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"unknown type %c in %S",
                //     static_cast<int>(*cursor),
                //     format)
                // Log.Add(
                //     LOG_ERROR,
                //     "[packet decode] Unknown format byte 0x%02x at offset %llu. This byte is ignored and decoding
                //     continues "
                //     "(format prefix: %.*s%s).",
                //     static_cast<unsigned>(static_cast<unsigned char>(*cursor)),
                //     static_cast<unsigned long long>(cursor - format),
                //     cursor - format < 160 ? static_cast<int>(cursor - format) + 1 : 160, format,
                //     cursor - format >= 160 ? " [prefix truncated]" : ""
                // )
                L2_LOG_ERROR(
                    Log,
                    "[packet decode] Unknown format byte 0x%02x at offset %llu; this byte is ignored and decoding continues "
                    "(format prefix: %.*s%s).",
                    static_cast<unsigned>(static_cast<unsigned char>(*cursor)),
                    static_cast<unsigned long long>(cursor - format),
                    cursor - format < 160 ? static_cast<int>(cursor - format) + 1 : 160, format,
                    cursor - format >= 160 ? " [prefix truncated]" : ""
                );
                break;
        }
    }

    va_end(cursorArguments);
    return packet;
}

const unsigned char* Disassemble(const unsigned char* packet, const char* format, ...) {
    // Current L2Server.exe 0x007cc558. Current Cached.exe 0x004e5e4c.
    // current L2NPC.exe 0x00454f04.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    const unsigned char* const result = VDisassemble(packet, format, arguments);
    va_end(arguments);
    return result;
}

int VAssemble(char* buffer, int bufferLength, const char* format, va_list arguments) {
    // Current Cached.exe 0x004e4bc4 and current L2NPC.exe 0x00454fa8.
    // Windows WCHAR values are represented by
    // char16_t so the sent packet remains UTF-16LE on every host.
    va_list cursorArguments;
    va_copy(cursorArguments, arguments);

    char* cursor = buffer;
    char* const end = buffer + bufferLength;
    for (const char* cursorFormat = format; *cursorFormat != '\0'; ++cursorFormat) {
        switch (*cursorFormat) {
            case 'c': {
                if (cursor + 1 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                *cursor++ = static_cast<char>(va_arg(cursorArguments, int));
                break;
            }
            case 'C': {
                if (cursor + 2 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                *cursor = static_cast<char>(va_arg(cursorArguments, int));
                cursor += 2;
                break;
            }
            case 'F': {
                if (cursor + 4 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                const float value = static_cast<float>(va_arg(cursorArguments, double));
                std::memcpy(cursor, &value, sizeof(value));
                cursor += sizeof(value);
                break;
            }
            case 'Q':
            case 'p': {
                if (cursor + 8 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                const std::int64_t value = va_arg(cursorArguments, std::int64_t);
                std::memcpy(cursor, &value, sizeof(value));
                cursor += sizeof(value);
                break;
            }
            case 'S': {
                const char16_t* const value = va_arg(cursorArguments, const char16_t*);
                const std::size_t byteCount =
                    value ? (target_utf16::Length(value) + 1U) * sizeof(char16_t) : sizeof(char16_t);
                if (cursor + byteCount > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                if (value != nullptr) {
                    const std::size_t unitCount = byteCount / sizeof(char16_t);
                    for (std::size_t index = 0; index < unitCount; ++index) {
                        target_utf16::StoreLittleEndian(cursor + index * sizeof(char16_t), value[index]);
                    }
                } else {
                    target_utf16::StoreLittleEndian(cursor, u'\0');
                }
                cursor += byteCount;
                break;
            }
            case 'b': {
                const int byteCount = va_arg(cursorArguments, int);
                const void* const value = va_arg(cursorArguments, const void*);
                if (cursor + byteCount > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                std::memcpy(cursor, value, static_cast<std::size_t>(byteCount));
                cursor += byteCount;
                break;
            }
            case 'd': {
                if (cursor + 4 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                const std::int32_t value = va_arg(cursorArguments, std::int32_t);
                std::memcpy(cursor, &value, sizeof(value));
                cursor += sizeof(value);
                break;
            }
            case 'f': {
                if (cursor + 8 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                const double value = va_arg(cursorArguments, double);
                std::memcpy(cursor, &value, sizeof(value));
                cursor += sizeof(value);
                break;
            }
            case 'h': {
                if (cursor + 2 > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }
                const std::uint16_t value = static_cast<std::uint16_t>(va_arg(cursorArguments, int));
                std::memcpy(cursor, &value, sizeof(value));
                cursor += sizeof(value);
                break;
            }
            case 's': {
                const char* const value = va_arg(cursorArguments, const char*);
                if (!value) {
                    if (cursor + 1 > end) {
                        va_end(cursorArguments);
                        // return ReportAssembleOverflow()
                        return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                    }
                    *cursor = '\0';
                    ++cursor;
                    break;
                }

                // Shipping narrows strlen to signed int before adding the
                // terminator and computing the prospective end pointer.
                const std::uint32_t byteCountBits = static_cast<std::uint32_t>(std::strlen(value)) + 1U;
                std::int32_t byteCount;
                std::memcpy(&byteCount, &byteCountBits, sizeof(byteCount));
                char* const next = cursor + byteCount;
                if (next > end) {
                    va_end(cursorArguments);
                    // return ReportAssembleOverflow()
                    return ReportAssembleOverflow(format, cursorFormat, bufferLength, cursor - buffer);
                }

                const std::size_t capacity = static_cast<std::size_t>(end - cursor);
                if (!cursor || capacity == 0U) {
                    runtime::process::HandleInvalidParameter();
                } else {
                    std::size_t remaining = capacity;
                    const char* source = value;
                    char* destination = cursor;
                    do {
                        const char character = *source++;
                        *destination++ = character;
                        if (character == '\0') {
                            break;
                        }
                        --remaining;
                    } while (remaining != 0U);
                    if (remaining == 0U) {
                        *cursor = '\0';
                        runtime::process::HandleInvalidParameter();
                    }
                }
                cursor = next;
                break;
            }
            default:
                // Shipping VAssemble silently ignores unknown format bytes.
                break;
        }
    }
    va_end(cursorArguments);

    const std::uint32_t assembledBits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cursor)) -
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer));
    std::int32_t assembled;
    std::memcpy(&assembled, &assembledBits, sizeof(assembled));
    if (assembled < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "buf - start is less than 0")
        // Log.Add(
        //     LOG_ERROR,
        //     "[packet encode] Computed packet length %d is negative after the signed 32-bit offset calculation "
        //     "(buffer capacity %d bytes). Returning zero.",
        //     assembled, bufferLength
        // )
        L2_LOG_ERROR(
            Log,
            "[packet encode] Computed packet length %d is negative after the signed 32-bit offset calculation "
            "(buffer capacity %d bytes); returning zero.",
            assembled, bufferLength
        );
        return 0;
    }
    return assembled;
}

int Assemble(char* buffer, int bufferLength, const char* format, ...) {
    // Current Cached.exe 0x004e5a3c. Current L2NPC.exe 0x00455ef8.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    const int assembled = VAssemble(buffer, bufferLength, format, arguments);
    va_end(arguments);
    return assembled;
}
