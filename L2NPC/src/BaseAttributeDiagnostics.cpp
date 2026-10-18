#include "Shared/src/BaseAttribute.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"

namespace runtime::process {

void ReportInvalidBaseAttributeRemoval(int attributeType) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"Invalid eAttr %d requested to delete!",
    //     attributeType)
    // Log.Add(LOG_ERROR,
    //     "[attributes] Cannot remove element type %d: valid types are %d through %d. Attributes unchanged.",
    //     attributeType, static_cast<int>(ATTRT_FIRE), static_cast<int>(ATTRT_MAX) - 1)
    L2_LOG_ERROR(
        Log, "[attributes] Cannot remove element type %d: valid types are %d through %d; attributes unchanged.",
        attributeType, static_cast<int>(ATTRT_FIRE), static_cast<int>(ATTRT_MAX) - 1
    );
    ::ReportCallStack(LOG_ERROR, false);
}

} // namespace runtime::process
