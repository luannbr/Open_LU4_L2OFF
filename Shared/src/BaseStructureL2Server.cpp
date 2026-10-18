#include "Shared/src/BaseStructure.h"

#include "Shared/src/Config.h"
#include "L2Core/src/inc/Guard.h"

namespace ServiceVersion {

bool IsLiveTestVer() {
    L2_GUARD_FUNCTION();

    return g_L2ServerType == L2ServerType::LIVE || g_L2ServerType == L2ServerType::TEST;
}

} // namespace ServiceVersion
