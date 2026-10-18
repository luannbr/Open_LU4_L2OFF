#include "ThreadSafeLibrary.h"

#include "Shared/src/Runtime/ProcessRuntime.h"

namespace thread_safe_detail {

void ReportNullSmartPointerDereference() { runtime::process::ReportNullSmartPointerDereference(); }

} // namespace thread_safe_detail
