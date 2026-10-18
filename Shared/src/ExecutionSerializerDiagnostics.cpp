#include "ThreadSafeLibrary.h"

#include "Shared/src/Runtime/ProcessRuntime.h"

namespace thread_safe_detail {

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
    runtime::process::ReportExecutionSerializerRecursive(
        threadId, ownerName, executableName, ownerTypeName, queueSize, recursiveCount, x, y, z
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
    runtime::process::ReportExecutionSerializerAbnormalRunner(
        threadId, runningThreadId, ownerName, ownerTypeName, queueSize, x, y, z
    );
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
    runtime::process::ReportExecutionSerializerOverflow(
        stage, threadId, runningThreadId, masterName, ownerName, executableName, ownerTypeName, queueSize, x, y, z
    );
}

} // namespace thread_safe_detail
