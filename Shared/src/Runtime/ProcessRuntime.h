#pragma once

#include "L2Core/src/inc/Guard.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <source_location>

class MemoryObject;

// Host-only process bindings used while compiling the original Shared sources
// into each executable. This is not presented as a recovered
// target class: the Windows images read their own globals and TLS directly.
namespace runtime::process {

// Shared/GIP.cpp reads these values from process globals in the Windows
// build. Each portable executable publishes its already-parsed equivalent at
// the same process boundary used for the TLS database selector.
struct GameInfoPipelineConfiguration {
    bool enabled = false;
    std::string address{"127.0.0.1"};
    std::uint16_t port = 3999;
    std::uint32_t heartbeatMilliseconds = 10000;
    int worldId = 0;
    // Product-fixed execution metadata. Current Cached.exe registers its
    // suspended GIP worker as logical thread 0x11 and gives that worker two
    // 0x200-entry I/O-buffer pools before entering GIPConnectThread_common.
    // Products without an admitted GIP worker leave these disabled.
    int logicalThreadId = -1;
    int ioBufferPoolSize = 0;
};

// MemorySub.h is one physical Shared owner, but the shipping executables do
// not all contain the same free-queue creation. Current L2NPC uses
// CMwmrGrowingQueue<65536>. Current CacheD and L2Server use CSmartQueue with
// process-sized admission sentinels (23 and 33 respectively). The Windows
// build obtained this distinction by compiling MemorySub.obj for each
// executable. The portable build selects that same fixed process contract at
// its composition boundary without duplicating the Shared class.
enum class MemoryPoolQueueModel : std::uint8_t {
    MwmrGrowing65536,
    Smart,
};

struct MemoryPoolQueueConfiguration {
    MemoryPoolQueueModel model = MemoryPoolQueueModel::MwmrGrowing65536;
    std::int32_t smartQueueSemaphoreLimit = 0;
};

MemoryPoolQueueConfiguration GetMemoryPoolQueueConfiguration() noexcept;

int MemoryObjectClassId() noexcept;

// Current Windows Shared code reads the active database selector from its
// request TLS. Each portable executable owns the matching process
// context. Shared classes consume it through this boundary.
int CurrentDatabaseId() noexcept;
// CacheD's CServerPacket restores the database selector captured from the
// three-byte L2Server frame before invoking its callback. Current
// Cached.exe 0x004e63cc writes this value directly to request TLS.
void SetCurrentDatabaseId(int databaseId) noexcept;
GameInfoPipelineConfiguration ReadGameInfoPipelineConfiguration();

void* AllocateMemoryObject(
    std::size_t requestedSize,
    bool objectAllocation,
    int classId,
    const wchar_t* allocationName,
    const char* reportName,
    std::int32_t classSize
);

void FreeMemoryObject(void* allocation) noexcept;
void DecrementMemoryClassReference(int classId) noexcept;

void ReportDeletingInTimer(const char* className, const char* callerFile, int callerLine);

void ReportInvalidReference(
    std::int32_t remaining, const char* className, std::int32_t objectId, const char* callerFile, int callerLine
);

// Shipping CHECK_SIZE_INT paths publish process-owned crash metadata and
// fault. Each executable owns the equivalent fatal boundary.
// Original CHECK_SIZE_INT failure. It reports the caller location.
[[noreturn]] void FailCheckSizeInt(std::source_location location = std::source_location::current());

[[noreturn]] void ReportForbiddenDelete(const wchar_t* className, const wchar_t* sourceFile, int sourceLine);

void ReportNullSmartPointerDereference();
// CObjectStorage is a physical Shared template, while each shipping product
// owns its CLog global. Keep only that process-owned diagnostic sink behind
// the binding. Key calculation, validation and fallback remain in the
// template body.
void ReportObjectStorageKeyBelowZero(int key, std::intptr_t address);
void ReportObjectStorageKeyAboveMaximum(int key, std::intptr_t address, int maximum);
void ReportInvalidBaseAttributeRemoval(int attributeType);

// Shared packet assembly is identical in the server processes, while guard
// stacks and invalid-parameter crash state belong to each executable. Keep
// those two observable diagnostics behind the same process binding as the
// allocator and lock diagnostics above.
void ReportNamedCallStack(const wchar_t* name, int logType, bool sendMail);
void ReportCallStack(int logType, bool sendMail);
// Current Shared/ThreadSafeLibrary.h FOR_ALL reports a bucket that holds its
// read lock for more than 5 ms and dumps the first 16 call/lock stacks after
// 500 ms. The stacks and logger are process-owned, so only that sink crosses
// this process binding.
void ReportObjectMapForAllDuration(std::uint32_t elapsedMilliseconds, int bucket, int mapSize);
void HandleInvalidParameter(const wchar_t* expression = nullptr);

// SEH/mailing wrappers are compiled around process-lifetime Shared workers.
// Their crash metadata and final process action belong to the executable,
// not to the reusable transport or logger body.
[[noreturn]] void HandleUnhandledSharedThreadException(const char* sourceFile, int sourceLine, const char* buildTimestamp);

// Shared/ioc.cpp is linked by several server executables, but its diagnostic
// sinks and crash metadata are process globals in each image. Keep the
// socket/container implementation shared while routing only those
// process-owned effects through the executable composition root.
void WriteIOErrorV(const char* format, va_list arguments);
void WriteIOErrorV(const wchar_t* format, va_list arguments);
void WriteIOLocalErrorV(const wchar_t* format, va_list arguments);
void ReportIOCloseCallStack();
void ReportIOCompletionPostFailure();
[[noreturn]] void FailIOBufferAllocation(std::source_location location);
void ReportInvalidIOBufferAllocation(
    int threadId, int remaining, int referenceCount, std::uint32_t dataSize, const void* next
);
void ReportMwsrQueueLowMemory(const char* allocationName);
void ReportNewMwsrQueueNull(const wchar_t* allocationName);
void ReportExecutionSerializerFastOverflow(int stage, int threadId, const wchar_t* executableName, int queueSize);
void ReportExecutionSerializerRecursive(
    int threadId,
    const char16_t* ownerName,
    const wchar_t* executableName,
    const char* ownerTypeName,
    std::size_t queueSize,
    int recursiveCount,
    int x,
    int y,
    int z
);
void ReportExecutionSerializerAbnormalRunner(
    int threadId,
    int runningThreadId,
    const char16_t* ownerName,
    const char* ownerTypeName,
    std::size_t queueSize,
    int x,
    int y,
    int z
);
void ReportExecutionSerializerOverflow(
    int stage,
    int threadId,
    int runningThreadId,
    const char16_t* masterName,
    const char16_t* ownerName,
    const wchar_t* executableName,
    const char* ownerTypeName,
    std::size_t queueSize,
    int x,
    int y,
    int z
);
void BeginIOGeneralTimer();
void EndIOGeneralTimer();

void RecordLockWaiting(const wchar_t* sourceFile, int sourceLine, std::uint64_t lockAddress, std::uint8_t lockType);
void RecordLockEntered(const wchar_t* sourceFile, int sourceLine, std::uint64_t lockAddress, std::uint8_t lockType);
void RecordLockReleased(const wchar_t* sourceFile, int sourceLine, std::uint64_t lockAddress, std::uint8_t lockType);

void ReportRWLockWaitFailure(std::uint32_t waitResult, int lastError, const wchar_t* callerFile, int callerLine);
void ReportRWLockReleaseFailure(int lastError);
void ReportRWLockBadRelease(std::int32_t previousCount);

// CLog is instantiated separately by each executable. Shared/LogFormatter
// produces the target CSV payload. This binding selects that process's local,
// LogD and audit destinations without linking one executable's CLog global
// into another executable.
void WriteLogFormatterLine(int logType, const std::u16string& line);

} // namespace runtime::process
