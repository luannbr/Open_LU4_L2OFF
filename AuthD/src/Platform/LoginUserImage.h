#pragma once

#include "AuthType.h"

#include <cstring>

namespace authd::portable {

inline void CopyLoginUserImage(
    LoginUser& destination,
    const LoginUser& source) noexcept {
    // Selected L2Authd.exe call sites copy the complete 0xB64-byte x86 image
    // with REP MOVSD. Both portable operands use the same host representation,
    // so the complete-object memcpy is the matching operation.
    std::memcpy(
        &destination,
        &source,
        sizeof(LoginUser));
}

} // namespace authd::portable
