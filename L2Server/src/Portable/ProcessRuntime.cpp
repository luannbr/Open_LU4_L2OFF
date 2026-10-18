#include "Portable/ProcessRuntime.h"

#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/MemorySub.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <new>
#include <string>
#include <string_view>

namespace runtime::process {

namespace {

thread_local int g_requestWorldId = 0;
thread_local int g_requestDatabaseId = 0;

struct LockDiagnostic final {
    std::uint64_t address = 0;
    std::uint8_t type = 0;
    bool entered = false;
};

thread_local std::array<LockDiagnostic, 256> g_lockDiagnostics{};
thread_local std::size_t g_lockDiagnosticCount = 0;

std::string Utf8(const wchar_t* value) { return ::WideToUtf8(value ? value : L""); }

void LogWideV(LogType type, const wchar_t* format, va_list arguments) {
    // std::array<wchar_t, 4096> message{}
    // const std::wstring hostFormat =
    //     ::WindowsWideFormatToHost(format ? format : L"")
    // va_list copy.
    // va_copy(copy, arguments)
    // std::vswprintf(
    //     message.data(),
    //     message.size(),
    //     hostFormat.c_str(),
    //     copy)
    // va_end(copy)
    // const std::string utf8 = ::WideToUtf8(message.data())
    // Log.Add(type, "%s", utf8.c_str())

    Log.AddV(type, const_cast<wchar_t*>(format ? format : L""), arguments);
}

} // namespace

int MemoryObjectClassId() noexcept { return L2_MEMORY_CLASS_ID(MemoryObject); }

MemoryPoolQueueConfiguration GetMemoryPoolQueueConfiguration() noexcept {
    // Current L2Server.exe contains the same 0x40-byte
    // CSmartQueue, with its process-specific sentinel fixed to 0x21 at
    // 0x00830910.
    return {
        MemoryPoolQueueModel::Smart,
        0x21,
    };
}

int CurrentDatabaseId() noexcept { return g_requestDatabaseId; }

void SetCurrentDatabaseId(int databaseId) noexcept { g_requestDatabaseId = databaseId; }

int CurrentWorldId() noexcept { return g_requestWorldId; }

void SetCurrentWorldId(int worldId) noexcept { g_requestWorldId = worldId; }

GameInfoPipelineConfiguration ReadGameInfoPipelineConfiguration() {
    GameInfoPipelineConfiguration result;
    result.enabled = false;
    result.worldId = g_nWorldId;
    return result;
}

void* AllocateMemoryObject(
    std::size_t requestedSize,
    bool objectAllocation,
    int classId,
    const wchar_t* allocationName,
    const char* reportName,
    std::int32_t classSize
) {
    // return L2ServerMemory::Instance().Allocate(
    return AllocateMemoryClass(requestedSize, objectAllocation, classId, allocationName, reportName, classSize);
}

void FreeMemoryObject(void* allocation) noexcept {
    // L2ServerMemory::Instance().Free(allocation)
    g_Memory.Free(allocation, 0);
}

void DecrementMemoryClassReference(int classId) noexcept {
    // L2ServerMemory::Instance().DecrementClassReference(classId)
    ::DecrementMemoryClassReference(classId);
}

void ReportDeletingInTimer(const char* className, const char* callerFile, int callerLine) {
    // Log.Add(
    //     LOG_ERROR,
    //     callerFile
    //         ? "deleting in-timer-object![%s] at file[%s], line[%d]"
    //         : "deleting in-timer-object![%s]",
    //     className ? className : "",
    //     callerFile ? callerFile : "",
    //     callerLine)

    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=release_while_in_timer class=\"%.160s\" caller=\"%.240s\" line=%d ref_count=0
    //     action=report_object_references", className ? className : "unknown", callerFile ? callerFile : "unknown",
    //     callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Releasing \"%.160s\" while its timer is active (caller \"%.240s\", line %d. No references left).
    //     Reporting object references.", className ? className : "unknown", callerFile ? callerFile : "unknown",
    //     callerLine)
    L2_LOG_ERROR(
        Log,
        "[memory] Releasing \"%.160s\" while its timer is active (caller \"%.240s\", line %d; no references left). "
        "Reporting object references.",
        className ? className : "unknown", callerFile ? callerFile : "unknown", callerLine
    );
}

void ReportInvalidReference(
    std::int32_t remaining, const char* className, std::int32_t objectId, const char* callerFile, int callerLine
) {
    // Noise: The separate caller line is merged into the reference-underflow event for reliable attribution.
    // Log.Add(
    //     LOG_ERROR,
    //     "Invalid Memory Ex(%d) on %s(%d)",
    //     remaining,
    //     className ? className : "",
    //     objectId)
    // if (callerFile) {
    //     Log.Add(
    //         LOG_ERROR,
    //         "           Caller : file[%s], line[%d]",
    //         callerFile,
    //         callerLine)
    // }

    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=reference_underflow class=\"%.160s\" object_id=%d ref_count=%d caller=\"%.240s\" line=%d
    //     action=report_object_references_and_stack", className ? className : "unknown", objectId, remaining, callerFile ?
    //     callerFile : "unknown", callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Reference count below zero for \"%.160s\", object %d: %d references (caller \"%.240s\", line %d).
    //     Reporting references and call stack.", className ? className : "unknown", objectId, remaining, callerFile ?
    //     callerFile : "unknown", callerLine)
    L2_LOG_ERROR(
        Log,
        "[memory] Reference count below zero for \"%.160s\", object %d: %d references (caller \"%.240s\", line %d). "
        "Reporting references and call stack.",
        className ? className : "unknown", objectId, remaining, callerFile ? callerFile : "unknown", callerLine
    );
}

[[noreturn]] void FailCheckSizeInt(const std::source_location location) {
    // Log.Add(
    //     LOG_ERROR,
    //     "CHECK_SIZE_INT: %s. %s. %s",
    //     Utf8(functionName).c_str(),
    //     Utf8(sourceFile).c_str(),
    //     Utf8(lineText).c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=size_check_failed function=\"%.240s\" source=\"%.240s\" line=\"%.40s\" action=abort",
    //     Utf8(functionName).c_str(), Utf8(sourceFile).c_str(), Utf8(lineText).c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Integer size check failed in \"%.240s\" at \"%.240s\", line \"%.40s\". Aborting.",
    //     Utf8(functionName).c_str(), Utf8(sourceFile).c_str(), Utf8(lineText).c_str())
    // Previous: L2_LOG_ERROR(
    //     Log, "[memory] Integer size check failed in \"%.240s\" at \"%.240s\", line \"%.40s\". Aborting.",
    //     Utf8(functionName).c_str(), Utf8(sourceFile).c_str(), Utf8(lineText).c_str())
    L2_LOG_ERROR(
        Log, "[memory] Integer size check failed in \"%.240s\" at \"%.240s\", line %u. Aborting.",
        location.function_name(), ::source_location::FileName(location.file_name()),
        static_cast<unsigned>(location.line())
    );
    std::abort();
}

[[noreturn]] void ReportForbiddenDelete(const wchar_t* className, const wchar_t* sourceFile, int sourceLine) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"Never Call Operator Delete!!! %s at file[%s], line[%d]",
    //     className,
    //     sourceFile,
    //     sourceLine)
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
    L2_LOG_ERROR(
        Log, "[memory] Forbidden delete of \"%.160s\" at \"%.240s\", line %d. Generating a crash report.",
        ::WideToUtf8(className ? className : L"unknown").c_str(), ::WideToUtf8(sourceFile ? sourceFile : L"unknown").c_str(),
        sourceLine
    );
    // Noise: Emit the recorded stack once, keeping the required local-file copy
    // without repeating the same frames in the console and GUI.
    // ::ReportCallStack(LOG_LOCAL, false)
    // ::ReportCallStack(LOG_ERROR, false)
    runtime::log::ReportCallStackWithLocalCopy();
    ForceCrash(
        CrashInfo::NEVER_CALL_OPERATOR_DELETE, L"void __cdecl MemoryObject::operator delete(void *)", sourceFile, sourceLine
    );
    std::abort();
}

void ReportNullSmartPointerDereference() {
    // Log.Add(
    //     LOG_ERROR,
    //     "[shared/ThreadSafeLibrary.h][0x75] referencing NULL object.")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=null_smart_pointer source=Shared/ThreadSafeLibrary.h line=117 action=report_stack")
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Null smart pointer dereference at Shared/ThreadSafeLibrary.h:117. Reporting the call stack.")
    L2_LOG_ERROR(
        Log, "[memory] Null smart pointer dereference at Shared/ThreadSafeLibrary.h:117. Reporting the call stack."
    );
    ::ReportCallStack(LOG_ERROR, false);
}

void ReportObjectStorageKeyBelowZero(int key, std::intptr_t address) {
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
    L2_LOG_ERROR(
        Log, "[object storage] Negative lock key %d for address 0x%llx. Using lock 0.", key,
        static_cast<unsigned long long>(static_cast<std::uintptr_t>(address))
    );
}

void ReportObjectStorageKeyAboveMaximum(int key, std::intptr_t address, int maximum) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"object storage key[%d] (address[%ld]) > MAX(%d)",
    //     key,
    //     static_cast<long>(address),
    //     maximum)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=object_storage event=invalid_lock_key key=%d address=0x%llx lock_count=%d reason=out_of_range
    //     action=use_lock_zero", key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)), maximum)
    // Log.Add(
    //     LOG_ERROR,
    //     "[object storage] Lock key %d is out of range for address 0x%llx (%d locks available). Using lock 0.",
    //     key, static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)), maximum)
    L2_LOG_ERROR(
        Log, "[object storage] Lock key %d is out of range for address 0x%llx (%d locks available). Using lock 0.", key,
        static_cast<unsigned long long>(static_cast<std::uintptr_t>(address)), maximum
    );
}

void ReportInvalidBaseAttributeRemoval(int attributeType) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"Invalid eAttr %d requested to delete!",
    //     attributeType)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=object_attributes event=invalid_removal attribute_type=%d action=report_stack",
    //     attributeType)
    // Log.Add(
    //     LOG_ERROR,
    //     "[attributes] Cannot remove invalid attribute type %d. Reporting the call stack.",
    //     attributeType)
    L2_LOG_ERROR(Log, "[attributes] Cannot remove invalid attribute type %d. Reporting the call stack.", attributeType);
    ::ReportCallStack(LOG_ERROR, false);
}

void ReportNamedCallStack(const wchar_t* name, int logType, bool sendMail) {
    ::ReportCallStackNew(const_cast<wchar_t*>(name), static_cast<LogType>(logType), sendMail);
}

void ReportCallStack(int logType, bool sendMail) { ::ReportCallStack(static_cast<LogType>(logType), sendMail); }

void ReportObjectMapForAllDuration(std::uint32_t elapsedMilliseconds, int bucket, int mapSize) {
    if (elapsedMilliseconds <= 5) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[TimeChecker][c:\\work\\lineage2build\\server\\main\\shared\\"
    //     "ThreadSafeLibrary.h][%d]Tick = %llu",
    //     0x23d,
    //     static_cast<unsigned long long>(elapsedMilliseconds))
    // Log.Add(
    //     elapsedMilliseconds > 500 ? LOG_ERROR : LOG_WARN,
    //     "component=object_map event=slow_bucket elapsed_ms=%u bucket=%d bucket_size=%d report_threshold_ms=5
    //     stack_threshold_ms=500 source=Shared/ThreadSafeLibrary.h line=573", static_cast<unsigned>(elapsedMilliseconds),
    //     bucket, mapSize)
    // Log.Add(
    //     elapsedMilliseconds > 500 ? LOG_ERROR : LOG_WARN,
    //     "[object map] Bucket processing took %u ms (bucket %d, %d objects). Warning threshold: 5 ms. Stack report
    //     threshold: 500 ms.", static_cast<unsigned>(elapsedMilliseconds), bucket, mapSize)
    L2_LOG(
        Log, elapsedMilliseconds > 500 ? LOG_ERROR : LOG_WARN,
        "[object map] Bucket processing took %u ms (bucket %d, %d objects). Warning threshold: 5 ms; stack report "
        "threshold: 500 ms.",
        static_cast<unsigned>(elapsedMilliseconds), bucket, mapSize
    );
    // Noise: Bucket and size are included in slow_bucket. A second uncorrelated line adds no information.
    // Log.Add(
    //     LOG_ERROR,
    //     "[TimeChecker] FOR_ALL LOOP [count : %d][map size : %d]",
    //     bucket,
    //     mapSize)
    if (elapsedMilliseconds > 500) {
        ::ReportCallStack(LOG_ERROR, false);
    }
}

const wchar_t* CurrentGuardCaller() noexcept { return GuardHelper::getCaller(::CThreadManager::GetThreadID()); }

void HandleInvalidParameter(const wchar_t* expression) {
    // Log.Add(
    //     LOG_ERROR,
    //     "L2Server invalid parameter: %s",
    //     Utf8(expression).c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "component=runtime event=invalid_parameter expression=\"%.400s\" action=report_stack",
    //     Utf8(expression).c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[runtime] Invalid parameter: \"%.400s\". Reporting the call stack.",
    //     Utf8(expression).c_str())
    L2_LOG_ERROR(Log, "[runtime] Invalid parameter: \"%.400s\". Reporting the call stack.", Utf8(expression).c_str());
    ::ReportCallStack(LOG_ERROR, false);
}

[[noreturn]] void HandleUnhandledSharedThreadException(const char* sourceFile, int sourceLine, const char* buildTimestamp) {
    // Log.Add(
    //     LOG_ERROR,
    //     "*%s:%d(%s) exception",
    //     sourceFile ? sourceFile : "",
    //     sourceLine,
    //     buildTimestamp ? buildTimestamp : "")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=runtime event=unhandled_thread_exception source=\"%.240s\" line=%d build=\"%.80s\" action=abort",
    //     sourceFile ? sourceFile : "unknown", sourceLine, buildTimestamp ? buildTimestamp : "unknown")
    // Log.Add(
    //     LOG_ERROR,
    //     "[runtime] Unhandled thread exception at \"%.240s\", line %d (build \"%.80s\"). Aborting.",
    //     sourceFile ? sourceFile : "unknown", sourceLine, buildTimestamp ? buildTimestamp : "unknown")
    L2_LOG_ERROR(
        Log, "[runtime] Unhandled thread exception at \"%.240s\", line %d (build \"%.80s\"). Aborting.",
        sourceFile ? sourceFile : "unknown", sourceLine, buildTimestamp ? buildTimestamp : "unknown"
    );
    // On Windows std::abort ends the process without the crash report.
    ReportCaughtExceptionAsCrash();
    std::abort();
}

void WriteIOErrorV(const char* format, va_list arguments) {
    // std::array<char, 4096> message{}
    // va_list copy.
    // va_copy(copy, arguments)
    // std::vsnprintf(
    //     message.data(),
    //     message.size(),
    //     format ? format : "",
    //     copy)
    // va_end(copy)
    // Log.Add(LOG_ERROR, "%s", message.data())

    Log.AddV(LOG_ERROR, const_cast<char*>(format ? format : ""), arguments);
}

void WriteIOErrorV(const wchar_t* format, va_list arguments) { LogWideV(LOG_ERROR, format, arguments); }

void WriteIOLocalErrorV(const wchar_t* format, va_list arguments) { LogWideV(LOG_LOCAL, format, arguments); }

void ReportIOCloseCallStack() {
    // Current CIOSocketEx::{GracefulClose,Close} call the ordinary
    // ReportCallStack(LOG_ERROR, false) overload, not ReportCallStackNew.
    ::ReportCallStack(LOG_ERROR, false);
}

void ReportIOCompletionPostFailure() {
    // Log.Add(
    //     LOG_ERROR,
    //     "CIOObject::OnTimerCallback failed to post completion")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=io event=completion_post_failed operation=CIOObject::OnTimerCallback")
    // Log.Add(
    //     LOG_ERROR,
    //     "[I/O] CIOObject::OnTimerCallback could not post its completion.")
    L2_LOG_ERROR(Log, "[I/O] CIOObject::OnTimerCallback could not post its completion.");
}

[[noreturn]] void FailIOBufferAllocation(const std::source_location location) {
    static constexpr wchar_t kIOBufferCrash[] = L"io_buffer_crash";
    GuardHelper stackGuard(kIOBufferCrash, CThreadManager::GetThreadID());

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
    //     L"C:\\Work\\Lineage2Build\\Server\\Main\\Shared\\ioc.h",
    //     0x1db,
    //     threadId,
    //     remaining,
    //     referenceCount,
    //     static_cast<int>(dataSize),
    //     next)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=io_buffer event=invalid_allocation logical_thread=%d remaining=%d ref_count=%d data_size=%u next=%p
    //     action=try_next_buffer source=Shared/ioc.h line=475", threadId, remaining, referenceCount,
    //     static_cast<unsigned>(dataSize), next)
    // Log.Add(
    //     LOG_ERROR,
    //     "[I/O buffers] Invalid buffer on worker %d: %d buffers remaining, %d references, %u bytes, next %p. Trying the
    //     next buffer.", threadId, remaining, referenceCount, static_cast<unsigned>(dataSize), next)
    L2_LOG_ERROR(
        Log,
        "[I/O buffers] Invalid buffer on worker %d: %d buffers remaining, %d references, %u bytes, next %p. Trying the next "
        "buffer.",
        threadId, remaining, referenceCount, static_cast<unsigned>(dataSize), next
    );
}

void ReportMwsrQueueLowMemory(const char* allocationName) {
    // Log.Add(
    //     LOG_ERROR,
    //     "%s is low memory",
    //     allocationName ? allocationName : "")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=queue_allocation_failed queue=CMwsrQueue allocation=\"%.160s\"
    //     action=return_partial_initialization", allocationName ? allocationName : "unknown")
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Cannot allocate CMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
    //     allocationName ? allocationName : "unknown")
    L2_LOG_ERROR(
        Log, "[memory] Cannot allocate CMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
        allocationName ? allocationName : "unknown"
    );
}

void ReportNewMwsrQueueNull(const wchar_t* allocationName) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"%s is NULL",
    //     allocationName ? allocationName : L"")
    // Log.Add(
    //     LOG_ERROR,
    //     "component=memory event=queue_allocation_failed queue=CNewMwsrQueue allocation=\"%.160s\"
    //     action=return_partial_initialization",
    //     ::WideToUtf8(allocationName ? allocationName : L"unknown").c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[memory] Cannot allocate CNewMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
    //     ::WideToUtf8(allocationName ? allocationName : L"unknown").c_str())
    L2_LOG_ERROR(
        Log, "[memory] Cannot allocate CNewMwsrQueue storage \"%.160s\". Initialization remains incomplete.",
        ::WideToUtf8(allocationName ? allocationName : L"unknown").c_str()
    );
}

void ReportExecutionSerializerFastOverflow(int stage, int threadId, const wchar_t* executableName, int queueSize) {
    // Log.Add(
    //     LOG_ERROR,
    //     stage == 1
    //         ? L"[tid:%d][%s] Asynchronous Fast Execution "
    //           L"Queue[%d] Overflow.1"
    //         : L"[tid:%d][%s] Asynchronous Fast Execution "
    //           L"Queue[%d] Overflow.2",
    //     threadId,
    //     executableName,
    //     queueSize)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=execution_queue event=overflow serializer=fast logical_thread=%d capacity=%d stage=%d outcome=%s
    //     executable=\"%.240s\"", threadId, queueSize, stage == 1 ? 1 : 2, stage == 1 ? "rejected_before_enqueue" :
    //     "already_enqueued",
    //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[execution] Fast queue overflow on worker %d (capacity %d, stage %d): %s. Executable: \"%.240s\".",
    //     threadId, queueSize, stage == 1 ? 1 : 2,
    //     stage == 1 ? "job rejected before enqueueing" : "job already enqueued",
    //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str())
    L2_LOG_ERROR(
        Log, "[execution] Fast queue overflow on worker %d (capacity %d, stage %d): %s. Executable: \"%.240s\".", threadId,
        queueSize, stage == 1 ? 1 : 2, stage == 1 ? "job rejected before enqueueing" : "job already enqueued",
        ::WideToUtf8(executableName ? executableName : L"unknown").c_str()
    );
    ::ReportCallStack(LOG_ERROR, false);
}

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
) {
    // const std::wstring owner = ownerName
    //     ? ::Utf16ToWide(std::u16string(ownerName))
    //     : L"name is null".
    const std::string owner = ownerName ? ::Utf16ToUtf8(std::u16string_view(ownerName)) : "unknown";
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d][tid:%d][%s][%s][%S] "
    //     L"Async Exec [%d] Recursive Count[%d] (%d, %d, %d)",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x872,
    //     threadId,
    //     owner.c_str(),
    //     executableName,
    //     ownerTypeName,
    //     static_cast<int>(queueSize),
    //     recursiveCount,
    //     x,
    //     y,
    //     z)
    // Log.Add(
    //     LOG_WARN,
    //     "component=execution_queue event=excessive_recursion logical_thread=%d capacity=%zu recursive_count=%d threshold=4
    //     owner=\"%.120s\" executable=\"%.180s\" owner_type=\"%.100s\" x=%d y=%d z=%d action=execute_inline", threadId,
    //     queueSize, recursiveCount, owner.c_str(),
    //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str(),
    //     ownerTypeName ? ownerTypeName : "unknown", x, y, z)
    // Log.Add(
    //     LOG_WARN,
    //     "[execution] Worker %d exceeded the recursion threshold (queue capacity %zu, depth %d. Threshold 4). Owner
    //     \"%.120s\", executable \"%.180s\", type \"%.100s\", position (%d, %d, %d). Executing inline.", threadId,
    //     queueSize, recursiveCount, owner.c_str(),
    //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str(),
    //     ownerTypeName ? ownerTypeName : "unknown", x, y, z)
    L2_LOG_WARN(
        Log,
        "[execution] Worker %d exceeded the recursion threshold (queue capacity %zu, depth %d; threshold 4). Owner "
        "\"%.120s\", executable \"%.180s\", type \"%.100s\", position (%d, %d, %d). Executing inline.",
        threadId, queueSize, recursiveCount, owner.c_str(),
        ::WideToUtf8(executableName ? executableName : L"unknown").c_str(), ownerTypeName ? ownerTypeName : "unknown", x, y,
        z
    );
}

void ReportExecutionSerializerAbnormalRunner(
    int threadId,
    int runningThreadId,
    const char16_t* ownerName,
    const char* ownerTypeName,
    std::size_t queueSize,
    int x,
    int y,
    int z
) {
    // const std::wstring owner = ownerName
    //     ? ::Utf16ToWide(std::u16string(ownerName))
    //     : L"name is null".
    const std::string owner = ownerName ? ::Utf16ToUtf8(std::u16string_view(ownerName)) : "unknown";
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d][tid:%d][running tid:%d][%s][%S] "
    //     L"Asynchronous Execution [%d] Abnormal status!!! "
    //     L"(%d, %d, %d)",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x8a8,
    //     threadId,
    //     runningThreadId,
    //     owner.c_str(),
    //     ownerTypeName,
    //     static_cast<int>(queueSize),
    //     x,
    //     y,
    //     z)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=execution_queue event=multiple_runners logical_thread=%d running_thread=%d capacity=%zu
    //     owner=\"%.120s\" owner_type=\"%.100s\" x=%d y=%d z=%d action=report_stack_and_continue", threadId,
    //     runningThreadId, queueSize, owner.c_str(), ownerTypeName ? ownerTypeName : "unknown", x, y, z)
    // Log.Add(
    //     LOG_ERROR,
    //     "[execution] Multiple queue runners: worker %d, recorded runner %d, capacity %zu. Owner \"%.120s\", type
    //     \"%.100s\", position (%d, %d, %d). Reporting the stack and continuing.", threadId, runningThreadId, queueSize,
    //     owner.c_str(), ownerTypeName ? ownerTypeName : "unknown", x, y, z)
    L2_LOG_ERROR(
        Log,
        "[execution] Multiple queue runners: worker %d, recorded runner %d, capacity %zu. Owner \"%.120s\", type "
        "\"%.100s\", position (%d, %d, %d). Reporting the stack and continuing.",
        threadId, runningThreadId, queueSize, owner.c_str(), ownerTypeName ? ownerTypeName : "unknown", x, y, z
    );
    ::ReportCallStack(LOG_ERROR, false);
}

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
) {
    // const std::wstring owner = ownerName
    //     ? ::Utf16ToWide(std::u16string(ownerName))
    //     : L"name is null".
    const std::string owner = ownerName ? ::Utf16ToUtf8(std::u16string_view(ownerName)) : "unknown";
    if (stage == 1) {
        // const std::wstring master = masterName
        //     ? ::Utf16ToWide(std::u16string(masterName))
        //     : L"name is null".
        const std::string master = masterName ? ::Utf16ToUtf8(std::u16string_view(masterName)) : "unknown";
        // Log.Add(
        //     LOG_ERROR,
        //     L"[tid:%d][rtid:%d][%s][%s][%s][%S] "
        //     L"Async Exec Queue[%d] Overflow. 1 "
        //     L"(%d, %d, %d)",
        //     threadId,
        //     runningThreadId,
        //     master.c_str(),
        //     owner.c_str(),
        //     executableName,
        //     ownerTypeName,
        //     static_cast<int>(queueSize),
        //     x,
        //     y,
        //     z)
        // Log.Add(
        //     LOG_ERROR,
        //     "component=execution_queue event=overflow serializer=standard logical_thread=%d running_thread=%d capacity=%zu
        //     stage=1 outcome=rejected_before_enqueue master=\"%.100s\" owner=\"%.120s\" executable=\"%.180s\"
        //     owner_type=\"%.100s\" x=%d y=%d z=%d", threadId, runningThreadId, queueSize, master.c_str(), owner.c_str(),
        //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str(),
        //     ownerTypeName ? ownerTypeName : "unknown", x, y, z)
        // Log.Add(
        //     LOG_ERROR,
        //     "[execution] Queue overflow on worker %d (runner %d, capacity %zu): job rejected before enqueueing. Master
        //     \"%.100s\", owner \"%.120s\", executable \"%.180s\", type \"%.100s\", position (%d, %d, %d).", threadId,
        //     runningThreadId, queueSize, master.c_str(), owner.c_str(),
        //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str(),
        //     ownerTypeName ? ownerTypeName : "unknown", x, y, z)
        L2_LOG_ERROR(
            Log,
            "[execution] Queue overflow on worker %d (runner %d, capacity %zu): job rejected before enqueueing. Master "
            "\"%.100s\", owner \"%.120s\", executable \"%.180s\", type \"%.100s\", position (%d, %d, %d).",
            threadId, runningThreadId, queueSize, master.c_str(), owner.c_str(),
            ::WideToUtf8(executableName ? executableName : L"unknown").c_str(), ownerTypeName ? ownerTypeName : "unknown", x,
            y, z
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[tid:%d][rtid:%d][%s][%s][%S] "
        //     L"Async Exec Queue[%d] Overflow. 2 "
        //     L"(%d, %d, %d)",
        //     threadId,
        //     runningThreadId,
        //     owner.c_str(),
        //     executableName,
        //     ownerTypeName,
        //     static_cast<int>(queueSize),
        //     x,
        //     y,
        //     z)
        // Log.Add(
        //     LOG_ERROR,
        //     "component=execution_queue event=overflow serializer=standard logical_thread=%d running_thread=%d capacity=%zu
        //     stage=2 outcome=already_enqueued owner=\"%.120s\" executable=\"%.180s\" owner_type=\"%.100s\" x=%d y=%d z=%d",
        //     threadId, runningThreadId, queueSize, owner.c_str(),
        //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str(),
        //     ownerTypeName ? ownerTypeName : "unknown", x, y, z)
        // Log.Add(
        //     LOG_ERROR,
        //     "[execution] Queue overflow on worker %d (runner %d, capacity %zu): job is already enqueued. Owner \"%.120s\",
        //     executable \"%.180s\", type \"%.100s\", position (%d, %d, %d).", threadId, runningThreadId, queueSize,
        //     owner.c_str(),
        //     ::WideToUtf8(executableName ? executableName : L"unknown").c_str(),
        //     ownerTypeName ? ownerTypeName : "unknown", x, y, z)
        L2_LOG_ERROR(
            Log,
            "[execution] Queue overflow on worker %d (runner %d, capacity %zu): job is already enqueued. Owner \"%.120s\", "
            "executable \"%.180s\", type \"%.100s\", position (%d, %d, %d).",
            threadId, runningThreadId, queueSize, owner.c_str(),
            ::WideToUtf8(executableName ? executableName : L"unknown").c_str(), ownerTypeName ? ownerTypeName : "unknown", x,
            y, z
        );
    }
}

void BeginIOGeneralTimer() {}
void EndIOGeneralTimer() {}

void RecordLockWaiting(const wchar_t*, int, std::uint64_t lockAddress, std::uint8_t lockType) {
    if (g_lockDiagnosticCount == g_lockDiagnostics.size()) {
        return;
    }
    g_lockDiagnostics[g_lockDiagnosticCount++] = LockDiagnostic{lockAddress, lockType, false};
}

void RecordLockEntered(const wchar_t*, int, std::uint64_t lockAddress, std::uint8_t lockType) {
    for (std::size_t index = g_lockDiagnosticCount; index > 0; --index) {
        LockDiagnostic& record = g_lockDiagnostics[index - 1];
        if (record.address == lockAddress && record.type == lockType) {
            record.entered = true;
            return;
        }
    }
}

void RecordLockReleased(const wchar_t*, int, std::uint64_t lockAddress, std::uint8_t lockType) {
    for (std::size_t index = g_lockDiagnosticCount; index > 0; --index) {
        const LockDiagnostic& record = g_lockDiagnostics[index - 1];
        if (record.address == lockAddress && record.type == lockType) {
            for (std::size_t move = index; move < g_lockDiagnosticCount; ++move) {
                g_lockDiagnostics[move - 1] = g_lockDiagnostics[move];
            }
            --g_lockDiagnosticCount;
            return;
        }
    }
}

void ReportRWLockWaitFailure(std::uint32_t waitResult, int lastError, const wchar_t* callerFile, int callerLine) {
    // Log.Add(
    //     LOG_ERROR,
    //     "RWLock wait failure result=%u error=%d at %s:%d",
    //     waitResult,
    //     lastError,
    //     Utf8(callerFile).c_str(),
    //     callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=rwlock event=read_wait_failed logical_thread=%d wait_result=0x%08x error=%d error_valid=%s reason=%s
    //     caller=\"%.240s\" line=%d action=continue_read_lock", CThreadManager::GetThreadID(),
    //     static_cast<unsigned>(waitResult), lastError, waitResult == 0xffffffffU ? "true" : "false", waitResult == 0x80U ?
    //     "abandoned" : waitResult == 0xffffffffU ? "wait_failed" : "unexpected_wait_result", Utf8(callerFile).c_str(),
    //     callerLine)
    // Log.Add(
    //     LOG_ERROR,
    //     "[locks] Worker %d could not acquire the read semaphore (wait result 0x%08x, error %d. Error valid: %s. %s).
    //     Caller: \"%.240s\", line %d. Read-lock processing continues.", CThreadManager::GetThreadID(),
    //     static_cast<unsigned>(waitResult), lastError, waitResult == 0xffffffffU ? "true" : "false", waitResult == 0x80U ?
    //     "abandoned" : waitResult == 0xffffffffU ? "wait failed" : "unexpected wait result", Utf8(callerFile).c_str(),
    //     callerLine)
    L2_LOG_ERROR(
        Log,
        "[locks] Worker %d could not acquire the read semaphore (wait result 0x%08x, error %d; error valid: %s; %s). "
        "Caller: \"%.240s\", line %d. Read-lock processing continues.",
        CThreadManager::GetThreadID(), static_cast<unsigned>(waitResult), lastError,
        waitResult == 0xffffffffU ? "true" : "false",
        waitResult == 0x80U             ? "abandoned"
            : waitResult == 0xffffffffU ? "wait failed"
                                        : "unexpected wait result",
        Utf8(callerFile).c_str(), callerLine
    );
}

void ReportRWLockBadRelease(std::int32_t previousCount) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"WriteUnlock semaphore was not locked [%d]",
    //     previousCount)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=rwlock event=unexpected_release_count previous_count=%d expected=0 action=report_stack",
    //     previousCount)
    // Log.Add(
    //     LOG_ERROR,
    //     "[locks] Unexpected semaphore count after write unlock: %d. Expected 0. Reporting the call stack.",
    //     previousCount)
    L2_LOG_ERROR(
        Log, "[locks] Unexpected semaphore count after write unlock: %d; expected 0. Reporting the call stack.",
        previousCount
    );
    ReportCallStack(LOG_ERROR, false);
}

void ReportRWLockReleaseFailure(int lastError) {
    // Log.Add(
    //     LOG_ERROR,
    //     "WriteUnlock failed to release semaphore, error [%d]",
    //     lastError)
    // Log.Add(
    //     LOG_ERROR,
    //     "component=rwlock event=semaphore_release_failed error=%d action=return",
    //     lastError)
    // Log.Add(
    //     LOG_ERROR,
    //     "[locks] Cannot release the write semaphore (error %d).",
    //     lastError)
    L2_LOG_ERROR(Log, "[locks] Cannot release the write semaphore (error %d).", lastError);
}

void WriteLogFormatterLine(int logType, const std::u16string& line) {
    runtime::log::WriteLogFormatterLine(Log, static_cast<LogType>(logType), line);
}

} // namespace runtime::process
