#pragma once

#include <cstdint>

namespace AdminUtil {

bool CancelPostProc(int postId, wchar_t* auditText, int* reason);

bool CancelCommissionProc(std::int64_t commissionId, wchar_t* auditText, int* reason);

} // namespace AdminUtil
