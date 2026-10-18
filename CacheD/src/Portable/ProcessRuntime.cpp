#include "Portable/ProcessRuntime.h"

#include "Cached.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIP.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/LockStatusStack.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <cstdarg>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <new>
#include <optional>

namespace runtime::process {

namespace {

thread_local std::optional<int> g_requestDatabaseId;

// Current Cached.exe writes this class id at 0x006a3537. Its initializer
// pointer is the 24th class-id entry in the shipping CRT table. Independent
// anchors CAccount=5, CUser=29 and CWareHouse=31 fix the table ordering.
// Shipping number: 24. Portable code obtains a registered ID below.

} // namespace

int MemoryObjectClassId() noexcept { return L2_MEMORY_CLASS_ID(MemoryObject); }

MemoryPoolQueueConfiguration GetMemoryPoolQueueConfiguration() noexcept {
    // Current Cached.exe CSmartQueue::CSmartQueue at 0x004f5e3c stores
    // 0x17 both as its initial ring size and as the exclusive semaphore
    // sentinel used by Push/Pop/MonopolizeSemaphore.
    return {
        MemoryPoolQueueModel::Smart,
        0x17,
    };
}

int CurrentDatabaseId() noexcept { return g_requestDatabaseId ? *g_requestDatabaseId : GetCacheDConfiguration().worldId; }

int ConfiguredDatabaseId() noexcept { return GetCacheDConfiguration().worldId; }

void SetCurrentDatabaseId(int databaseId) noexcept { g_requestDatabaseId = databaseId; }

GameInfoPipelineConfiguration ReadGameInfoPipelineConfiguration() {
    const CacheDConfiguration& source = GetCacheDConfiguration();
    GameInfoPipelineConfiguration result;
    result.enabled = source.gipEnabled;
    result.address = source.gipAddress;
    result.port = source.gipPort;
    result.heartbeatMilliseconds = source.gipHeartbeatMilliseconds;
    result.worldId = source.worldId;
    // Current Cached.exe GIP entry 0x004d9c94 registers logical thread 0x11
    // and constructs both target buffer pools with 0x200 entries before its
    // process-lifetime loop.
    result.logicalThreadId = ThreadID::GIP;
    result.ioBufferPoolSize = 0x200;
    return result;
}

bool IsQsDataEnabled() noexcept { return g_bSaveQsData != 0; }

bool ExchangeQsDataEnabled(bool enabled) noexcept {
    const bool previous = g_bSaveQsData != 0;
    g_bSaveQsData = enabled ? 1U : 0U;
    return previous;
}

bool IsServerToCacheDPacketStatMonitorEnabled() noexcept { return g_bServerToCacheDPacketStatMonitor; }

bool IsCacheDToServerPacketStatMonitorEnabled() noexcept { return g_bCacheDToServerPacketStatMonitor; }

void* AllocateMemoryObject(
    std::size_t requestedSize,
    bool objectAllocation,
    int classId,
    const wchar_t* allocationName,
    const char* reportName,
    std::int32_t classSize
) {
    return AllocateMemoryClass(requestedSize, objectAllocation, classId, allocationName, reportName, classSize);
}

void FreeMemoryObject(void* allocation) noexcept { g_Memory.Free(allocation, 0); }

void DecrementMemoryClassReference(int classId) noexcept { ::DecrementMemoryClassReference(classId); }

void ReportDeletingInTimer(const char* className, const char* callerFile, int callerLine) {
    // if (callerFile) {
    //     Log.Add(
    //         LOG_ERROR, "deleting in-timer-object![%s] at file[%s], line[%d]", className, callerFile, callerLine
    //     )
    // } else {
    //     Log.Add(LOG_ERROR, "deleting in-timer-object![%s]", className)
    // }

    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=release_while_in_timer class=\"%.160s\" caller=\"%.240s\" line=%d ref_count=0 action=report_object_references",
    //     className ? className : "unknown", callerFile ? callerFile : "unknown", callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Releasing \"%.160s\" while its timer is active (caller \"%.240s\", line %d. No references left). Reporting object references.",
    //     className ? className : "unknown", callerFile ? callerFile : "unknown", callerLine)
    L2_LOG_ERROR(Log,
        "[memory] Releasing \"%.160s\" while its timer is active (caller \"%.240s\", line %d; no references left). Reporting object references.",
        className ? className : "unknown", callerFile ? callerFile : "unknown", callerLine);
}

void ReportInvalidReference(
    std::int32_t remaining, const char* className, std::int32_t objectId, const char* callerFile, int callerLine
) {
    // Noise: The separate caller line is merged into the reference-underflow event for reliable attribution.
    // Log.Add(LOG_ERROR, "Invalid Memory Ex(%d) on %s(%d)", remaining, className, objectId)
    // if (callerFile) {
    //     Log.Add(LOG_ERROR, "           Caller : file[%s], line[%d]", callerFile, callerLine)
    // }

    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=reference_underflow class=\"%.160s\" object_id=%d ref_count=%d caller=\"%.240s\" line=%d action=report_object_references_and_stack",
    //     className ? className : "unknown", objectId, remaining,
    //     callerFile ? callerFile : "unknown", callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Reference count below zero for \"%.160s\", object %d: %d references (caller \"%.240s\", line %d). Reporting references and call stack.",
    //     className ? className : "unknown", objectId, remaining,
    //     callerFile ? callerFile : "unknown", callerLine)
    L2_LOG_ERROR(Log,
        "[memory] Reference count below zero for \"%.160s\", object %d: %d references (caller \"%.240s\", line %d). Reporting references and call stack.",
        className ? className : "unknown", objectId, remaining,
        callerFile ? callerFile : "unknown", callerLine);
}

[[noreturn]] void ReportForbiddenDelete(const wchar_t* className, const wchar_t* sourceFile, int sourceLine) {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_ERROR, L"Never Call Operator Delete!!! %s at file[%s], line[%d]", className, sourceFile, sourceLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=forbidden_delete class=\"%.160s\" source=\"%.240s\" line=%d action=crash",
    //     ::WideToUtf8(className ? className : L"unknown").c_str(),
    //     ::WideToUtf8(sourceFile ? sourceFile : L"unknown").c_str(), sourceLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Forbidden delete of \"%.160s\" at \"%.240s\", line %d. Generating a crash report.",
    //     ::WideToUtf8(className ? className : L"unknown").c_str(),
    //     ::WideToUtf8(sourceFile ? sourceFile : L"unknown").c_str(), sourceLine)
    L2_LOG_ERROR(Log,
        "[memory] Forbidden delete of \"%.160s\" at \"%.240s\", line %d. Generating a crash report.",
        ::WideToUtf8(className ? className : L"unknown").c_str(),
        ::WideToUtf8(sourceFile ? sourceFile : L"unknown").c_str(), sourceLine);
    // Noise: Emit the recorded stack once, keeping the required local-file copy
    // without repeating the same frames in the console and GUI.
    // ::ReportCallStack(LOG_LOCAL, false)
    // ::ReportCallStack(LOG_ERROR, false)
    log::ReportCallStackWithLocalCopy();
    ForceCrash(
        CrashInfo::NEVER_CALL_OPERATOR_DELETE, L"void __cdecl MemoryObject::operator delete(void *)", sourceFile, sourceLine
    );
    std::abort();
}

void ReportNullSmartPointerDereference() {
    // Log.Add(
    //     LOG_ERROR, "[%s][%d] referencing NULL object.",
    //     "c:\\work\\lineage2build\\server\\main\\shared\\"
    //     "ThreadSafeLibrary.h",
    //     0x75
    // )
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=null_smart_pointer source=Shared/ThreadSafeLibrary.h line=117 action=report_stack")
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Null smart pointer dereference at Shared/ThreadSafeLibrary.h:117. Reporting the call stack.")
    L2_LOG_ERROR(Log,
        "[memory] Null smart pointer dereference at Shared/ThreadSafeLibrary.h:117. Reporting the call stack.");
    ::ReportCallStack(LOG_ERROR, false);
}

void ReportObjectStorageKeyBelowZero(
    int key,
    std::intptr_t address) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"object storage key[%d] (address[%ld]) < 0",
    //     key,
    //     static_cast<long>(address))
    // Log.Add(
    //     LOG_ERROR,
    //     "component=object_storage event=invalid_lock_key key=%d address=0x%llx reason=negative action=use_lock_zero",
    //     key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)))
    // Log.Add(
    //     LOG_ERROR,
    //     "[object storage] Negative lock key %d for address 0x%llx. Using lock 0.",
    //     key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)))
    L2_LOG_ERROR(Log,
        "[object storage] Negative lock key %d for address 0x%llx. Using lock 0.",
        key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)));
}

void ReportObjectStorageKeyAboveMaximum(
    int key,
    std::intptr_t address,
    int maximum) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"object storage key[%d] (address[%ld]) > MAX(%d)",
    //     key,
    //     static_cast<long>(address),
    //     maximum)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=object_storage event=invalid_lock_key key=%d address=0x%llx lock_count=%d reason=out_of_range action=use_lock_zero",
    //     key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)), maximum)
    // Log.Add(
    //     LOG_ERROR,
    //     "[object storage] Lock key %d is out of range for address 0x%llx (%d locks available). Using lock 0.",
    //     key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)), maximum)
    L2_LOG_ERROR(Log,
        "[object storage] Lock key %d is out of range for address 0x%llx (%d locks available). Using lock 0.",
        key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)), maximum);
}

void ReportNamedCallStack(const wchar_t* name, int logType, bool sendMail) {
    // Current Cached.exe 0x004ca3a8 is the Shared/Exception.cpp
    // ReportCallStackNew owner. Shared::CacheD creates that source with
    // the target's 22-thread enum, so keep this executable binding as routing
    // only instead of duplicating the guard-stack implementation here.
    ::ReportCallStackNew(const_cast<wchar_t*>(name), static_cast<LogType>(logType), sendMail);
}

void ReportCallStack(int logType, bool sendMail) {
    // Current Cached.exe 0x004cb1cc forwards the current logical thread to
    // Shared/Exception.cpp's three-argument report at 0x004ca28c.
    ::ReportCallStack(static_cast<LogType>(logType), sendMail);
}

void ReportObjectMapForAllDuration(std::uint32_t elapsedMilliseconds, int bucket, int mapSize) {
    // Current Cached.exe 0x005c1f78 calls the local TimeChecker worker
    // 0x005bf708 after releasing each bucket lock.
    if (elapsedMilliseconds <= 5)
        return;
    // Log.Add(
    //     LOG_ERROR, L"[TimeChecker][%s][%d]Tick = %I64d",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x23d, static_cast<unsigned long long>(elapsedMilliseconds)
    // )
    // Log.Add(
    //     elapsedMilliseconds > 500 ? LOG_ERROR : LOG_WARN,
    //     "component=object_map event=slow_bucket elapsed_ms=%u bucket=%d bucket_size=%d report_threshold_ms=5 stack_threshold_ms=500 source=Shared/ThreadSafeLibrary.h line=573",
    //     static_cast<unsigned>(elapsedMilliseconds), bucket, mapSize)
    // Log.Add(
    //     elapsedMilliseconds > 500 ? LOG_ERROR : LOG_WARN,
    //     "[object map] Bucket processing took %u ms (bucket %d, %d objects). Warning threshold: 5 ms. Stack report threshold: 500 ms.",
    //     static_cast<unsigned>(elapsedMilliseconds), bucket, mapSize)
    L2_LOG(Log, elapsedMilliseconds > 500 ? LOG_ERROR : LOG_WARN,
        "[object map] Bucket processing took %u ms (bucket %d, %d objects). Warning threshold: 5 ms; stack report threshold: 500 ms.",
        static_cast<unsigned>(elapsedMilliseconds), bucket, mapSize);
    // Noise: Bucket and size are included in slow_bucket. A second uncorrelated line adds no information.
    // Log.Add(LOG_ERROR, L"[TimeChecker] FOR_ALL LOOP [count : %d][map size : %d]", bucket, mapSize)
    if (elapsedMilliseconds <= 500)
        return;
    for (int threadId = 0; threadId < 16; ++threadId) {
        ::ReportCallStack(threadId, LOG_ERROR, false);
        ::ReportLockStack(threadId, L"TimeChecker", LOG_ERROR);
    }
}

void HandleInvalidParameter(const wchar_t* expression) {
    // Current Cached.exe 0x004ca640 is the CacheD creation of this
    // Shared invalid-parameter handler: it keeps only expression, records
    // Exception.cpp as the source and crosses the intentional-fault boundary.
    ::myInvalidParameterHandler(expression, nullptr, nullptr, 0, 0);
}

[[noreturn]] void HandleUnhandledSharedThreadException(const char* sourceFile, int sourceLine, const char* buildTimestamp) {
    // ELOG(L"*%S:%d(%S) exception\n", sourceFile ? sourceFile : "", sourceLine, buildTimestamp ? buildTimestamp : "")
    // ELOG(
    //     L"component=runtime event=unhandled_thread_exception source=\"%.240S\" line=%d "
    //     L"build=\"%.80S\" action=flush_report_and_abort\n",
    //     sourceFile ? sourceFile : "unknown", sourceLine, buildTimestamp ? buildTimestamp : "unknown")
    ELOG(
        L"[runtime] Unhandled thread exception at \"%.240S\", line %d (build \"%.80S\"). "
        L"Flushing logs, reporting the exception and aborting.\n",
        sourceFile ? sourceFile : "unknown", sourceLine, buildTimestamp ? buildTimestamp : "unknown");
    Log.FlushLogFile();
    GIP.FlushGIPFile();
    send_exception(true);
    std::abort();
}

void WriteIOErrorV(const char* format, va_list arguments) {
    Log.AddV(LOG_ERROR, const_cast<char*>(format ? format : ""), arguments);
}

void WriteIOErrorV(const wchar_t* format, va_list arguments) {
    Log.AddV(LOG_ERROR, const_cast<wchar_t*>(format ? format : L""), arguments);
}

void WriteIOLocalErrorV(
    const wchar_t* format,
    va_list arguments) {
    Log.AddV(
        LOG_LOCAL,
        const_cast<wchar_t*>(format ? format : L""),
        arguments);
}

void ReportIOCloseCallStack() { ::ReportCallStack(LOG_ERROR, false); }

void ReportIOCompletionPostFailure() {
    // Current Cached.exe CIOObject::OnTimerCallback 0x004e5210 calls the
    // CacheD creation of Shared::violation at 0x004cc9c4 before
    // releasing its timer reference. This is a one-shot intentional
    // exception report, not an ordinary log line.
    ::violation();
}

[[noreturn]] void FailIOBufferAllocation(const std::source_location location) {
    L2_GUARD_FUNCTION();
    ForceCrash(CrashInfo::LOW_IO_BUFFER, location);
    std::abort();
}

void ReportInvalidIOBufferAllocation(
    int threadId, int remaining, int referenceCount, std::uint32_t dataSize, const void* next
) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] S-DEBUG Invalid IO Buffer Allocation "
    //     L"threadid[%d], remain iobuffer size[%d] "
    //     L"refcount[%d], dwsize[%d], pNext[%p]",
    //     L"C:\\Work\\Lineage2Build\\Server\\Main\\Shared\\ioc.h", 0x1db, threadId, remaining, referenceCount,
    //     static_cast<int>(dataSize), next
    // )
    // Log.Add(
    //     LOG_ERROR,
    //     "component=io_buffer event=invalid_allocation logical_thread=%d remaining=%d ref_count=%d data_size=%u next=%p action=try_next_buffer source=Shared/ioc.h line=475",
    //     threadId, remaining, referenceCount, static_cast<unsigned>(dataSize), next)
    // Log.Add(
    //     LOG_ERROR,
    //     "[I/O buffers] Invalid buffer on worker %d: %d buffers remaining, %d references, %u bytes, next %p. Trying the next buffer.",
    //     threadId, remaining, referenceCount, static_cast<unsigned>(dataSize), next)
    L2_LOG_ERROR(Log,
        "[I/O buffers] Invalid buffer on worker %d: %d buffers remaining, %d references, %u bytes, next %p. Trying the next buffer.",
        threadId, remaining, referenceCount, static_cast<unsigned>(dataSize), next);
}

void ReportMwsrQueueLowMemory(const char* allocationName) {
    // Log.Add(LOG_ERROR, "%s is low memory", allocationName ? allocationName : "")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=queue_allocation_failed queue=CMwsrQueue allocation=\"%.160s\" action=return_partial_initialization",
    //     allocationName ? allocationName : "unknown")
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Cannot allocate CMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
    //     allocationName ? allocationName : "unknown")
    L2_LOG_ERROR(Log,
        "[memory] Cannot allocate CMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
        allocationName ? allocationName : "unknown");
}

void ReportNewMwsrQueueNull(const wchar_t* allocationName) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"%s is NULL",
    //     allocationName ? allocationName : L"")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=queue_allocation_failed queue=CNewMwsrQueue allocation=\"%.160s\" action=return_partial_initialization",
    //     ::WideToUtf8(allocationName ? allocationName : L"unknown").c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Cannot allocate CNewMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
    //     ::WideToUtf8(allocationName ? allocationName : L"unknown").c_str())
    L2_LOG_ERROR(Log,
        "[memory] Cannot allocate CNewMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
        ::WideToUtf8(allocationName ? allocationName : L"unknown").c_str());
}

void ReportExecutionSerializerFastOverflow(int stage, int threadId, const wchar_t* executableName, int queueSize) {
    // const std::string name = WideToUtf8(executableName ? executableName : L"")
    // Log.Add(
    //     LOG_ERROR,
    //     "[tid:%d][%s] Asynchronous Fast Execution Queue[%d] "
    //     "Overflow.%d",
    //     threadId, name.c_str(), queueSize, stage == 1 ? 1 : 2
    // )
    // Log.Add(
    //     LOG_ERROR,
    //     "component=execution_queue event=overflow serializer=fast logical_thread=%d capacity=%d stage=%d outcome=%s executable=\"%.240s\"",
    //     threadId, queueSize, stage == 1 ? 1 : 2,
    //     stage == 1 ? "rejected_before_enqueue" : "already_enqueued",
    //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[execution] Fast queue overflow on worker %d (capacity %d, stage %d): %s. Executable: \"%.240s\".",
    //     threadId, queueSize, stage == 1 ? 1 : 2,
    //     stage == 1 ? "job rejected before enqueueing" : "job already enqueued",
    //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str())
    L2_LOG_ERROR(Log,
        "[execution] Fast queue overflow on worker %d (capacity %d, stage %d): %s. Executable: \"%.240s\".",
        threadId, queueSize, stage == 1 ? 1 : 2,
        stage == 1 ? "job rejected before enqueueing" : "job already enqueued",
        ::WideToUtf8(executableName ? executableName : L"unknown").c_str());
    ReportNamedCallStack(L"CExecutionSerializerFast::AddExecutable", 2, false);
}

void BeginIOGeneralTimer() {
    // Intentionally empty for CacheD. Current CIOObject::OnIOCallback at
    // 0x004e5304 contains the guard, virtual dispatch and Release only. Unlike
    // L2NPC, this image has no TimeAnalysis BeginJob/EndJob instrumentation.
}

void EndIOGeneralTimer() {}

namespace {

CLockStatusStack& CurrentLockStack() {
    // Current Cached.exe selects one 0xea608-byte stack by request TLS
    // thread ID. The process-owned array is defined in CacheD/default.cpp.
    return g_LockStack[CThreadManager::GetThreadID()];
}

} // namespace

void RecordLockWaiting(const wchar_t* sourceFile, int sourceLine, std::uint64_t lockAddress, std::uint8_t lockType) {
    // CacheD CLockStatusStack::Lock creation: 0x004e7f54.
    CurrentLockStack().Lock(0, 0, sourceFile, sourceLine, lockAddress, lockType);
}

void RecordLockEntered(const wchar_t* sourceFile, int sourceLine, std::uint64_t lockAddress, std::uint8_t lockType) {
    // Current CacheD callers inline LockEntered after acquisition and mark
    // the just-pushed node's +0x15 byte.
    CurrentLockStack().LockEntered(sourceFile, sourceLine, lockAddress, lockType);
}

void RecordLockReleased(const wchar_t* sourceFile, int sourceLine, std::uint64_t lockAddress, std::uint8_t lockType) {
    // CacheD CLockStatusStack::Unlock creation: 0x004e7ff4.
    CurrentLockStack().Unlock(0, 0, sourceFile, static_cast<std::uint32_t>(sourceLine), lockAddress, lockType);
}

void ReportRWLockWaitFailure(std::uint32_t waitResult, int lastError, const wchar_t* callerFile, int callerLine) {
    // const int threadId = CThreadManager::GetThreadID()
    // if (waitResult == 0x80U) {
    //     Log.Add(
    //         LOG_ERROR, "ReadLock abondoned on dataLock, [%ls][%d], thread[%d]", callerFile, callerLine, threadId
    //     )
    // } else if (waitResult == 0xffffffffU) {
    //     Log.Add(
    //         LOG_ERROR, "ReadLock failed on dataLock[%d], [%ls][%d], thread[%d]", lastError, callerFile, callerLine,
    //         threadId
    //     )
    // } else {
    //     Log.Add(
    //         LOG_ERROR, "ReadLock failed on dataLock, [%ls][%d], thread[%d]", callerFile, callerLine, threadId
    //     )
    // }

    // Log.Add(
    //     LOG_ERROR,
    //     "component=rwlock event=read_wait_failed logical_thread=%d wait_result=0x%08x error=%d error_valid=%s reason=%s caller=\"%.240s\" line=%d action=continue_read_lock",
    //     CThreadManager::GetThreadID(), static_cast<unsigned>(waitResult), lastError,
    //     waitResult == 0xffffffffU ? "true" : "false",
    //     waitResult == 0x80U ? "abandoned" : waitResult == 0xffffffffU ? "wait_failed" : "unexpected_wait_result",
    //     ::WideToUtf8(callerFile ? callerFile : L"unknown").c_str(), callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[locks] Worker %d could not acquire the read semaphore (wait result 0x%08x, error %d. Error valid: %s. %s). Caller: \"%.240s\", line %d. Read-lock processing continues.",
    //     CThreadManager::GetThreadID(), static_cast<unsigned>(waitResult), lastError,
    //     waitResult == 0xffffffffU ? "true" : "false",
    //     waitResult == 0x80U ? "abandoned" : waitResult == 0xffffffffU ? "wait failed" : "unexpected wait result",
    //     ::WideToUtf8(callerFile ? callerFile : L"unknown").c_str(), callerLine)
    L2_LOG_ERROR(Log,
        "[locks] Worker %d could not acquire the read semaphore (wait result 0x%08x, error %d; error valid: %s; %s). Caller: \"%.240s\", line %d. Read-lock processing continues.",
        CThreadManager::GetThreadID(), static_cast<unsigned>(waitResult), lastError,
        waitResult == 0xffffffffU ? "true" : "false",
        waitResult == 0x80U ? "abandoned" : waitResult == 0xffffffffU ? "wait failed" : "unexpected wait result",
        ::WideToUtf8(callerFile ? callerFile : L"unknown").c_str(), callerLine);
}

void ReportRWLockBadRelease(std::int32_t previousCount) {
    // Log.Add(LOG_ERROR, "WriteUnlock semaphore was not locked [%d]", previousCount)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=rwlock event=unexpected_release_count previous_count=%d expected=0 action=report_stack",
    //     previousCount)
    // Log.Add(
    //     LOG_ERROR,
    //     "[locks] Unexpected semaphore count after write unlock: %d. Expected 0. Reporting the call stack.",
    //     previousCount)
    L2_LOG_ERROR(Log,
        "[locks] Unexpected semaphore count after write unlock: %d; expected 0. Reporting the call stack.",
        previousCount);
    // Current Cached.exe RWLock::WriteUnlock 0x005292d0 reports the guard
    // stack immediately after this diagnostic.
    ReportCallStack(LOG_ERROR, false);
}

void ReportRWLockReleaseFailure(int lastError) {
    // Log.Add(
    //     LOG_ERROR,
    //     "WriteUnlock failed to release semaphore, error [%d]",
    //     lastError)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=rwlock event=semaphore_release_failed error=%d action=report_stack",
    //     lastError)
    // Log.Add(
    //     LOG_ERROR,
    //     "[locks] Cannot release the write semaphore (error %d).",
    //     lastError)
    L2_LOG_ERROR(Log,
        "[locks] Cannot release the write semaphore (error %d).",
        lastError);
    ReportCallStack(LOG_ERROR, false);
}

void WriteLogFormatterLine(int logType, const std::u16string& line) {
    // Current CLogFormatter::WriteLog/WriteAuditLog at
    // 0x004efa60/0x004efc5c pass routes 6/8 to CacheD's wide CLog::Add
    // wrapper. Continue through that same process CLog owner so LogD, audit
    // files, reconnect fallback and the console ring all see the real type.
    log::WriteLogFormatterLine(Log, static_cast<LogType>(logType), line);
}

} // namespace runtime::process
