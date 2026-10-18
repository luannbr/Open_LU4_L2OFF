#include "Shared/src/ShareDefine.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "User.h"
#endif
#include "Shared/src/Log.h"

#include <cmath>
#include <cstring>

bool g_bSilentWhenNoUser = true;

SharedUserMapDensity::SharedUserMapDensity() : m_UserMapDensity(L"UserMapDensity") {}

SharedUserMapDensity& SharedUserMapDensity::Inst() {
    static SharedUserMapDensity density;
    return density;
}

void SharedUserMapDensity::Init() {
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedUserMapDensity] Init")
    // Noise: this Init hook does not initialize or validate the density grid.
}

SharedLong SharedUserMapDensity::CountByPos(const FVector& position) {
    const std::int32_t shiftedY = runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.y), 0x40000U);
    const std::int32_t shiftedX = runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.x), 0x50000U);
    const std::int32_t cellY = shiftedY / 0x800;
    const std::int32_t cellX = shiftedX / 0x800;
    const std::uint32_t index = static_cast<std::uint32_t>(cellY) * 0x140U + static_cast<std::uint32_t>(cellX);

    if (index < kUserMapDensityRecordCount) {
        return m_UserMapDensity[static_cast<int>(index)];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"CountByPos error [x][y][z][nIndex]",
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     index)
    // Log.Add(LOG_ERROR,
    //     "[user density] Cannot look up position (%d, %d, %d): cell index %u exceeds the %zu-cell grid. Returning 0.",
    //     runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z), index, kUserMapDensityRecordCount)
    L2_LOG_ERROR(
        Log, "[user density] Cannot look up position (%d, %d, %d): cell index %u exceeds the %zu-cell grid; returning 0.",
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), index, kUserMapDensityRecordCount
    );
    return 0;
}

#if defined(L2_BUILD_L2SERVER)
void SharedUserMapDensity::Increase(int index, const FVector& position, User& user) {
    L2_GUARD_FUNCTION();
    static int errorCount = 0;
    SharedLong& count = m_UserMapDensity[index];
    const std::int32_t result = runtime::raw_atomic::IncrementI32(&count);
    if (result < 0 && errorCount < 20) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"UserMapD Increase ret = [%d]. index = [%d], "
        //     L"pos=[%d, %d, %d] user[%s]",
        //     result,
        //     index,
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     user.GetName())
        // Log.Add(LOG_ERROR,
        //     L"[user density] Negative count %d after increment at cell %d, position (%d, %d, %d), character \"%ls\". The
        //     count remains stored. This path reports only its first 20 failures.", result, index,
        //     runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z), user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[user density] Negative count %d after increment at cell %d, position (%d, %d, %d), character \"%ls\". The
        //     count remains stored. This path reports only its first 20 failures.", result, index,
        //     runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z), user.GetName())
        L2_LOG_ERROR(
            Log,
            u"[user density] Negative count %d after increment at cell %d, position (%d, %d, %d), character \"%ls\"; the "
            u"count remains stored. This path reports only its first 20 failures.",
            result, index, runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), user.GetName()
        );
        runtime::process::ReportCallStack(2, false);
        ++errorCount;
    }
}

void SharedUserMapDensity::Decrease(int index, const FVector& position, User& user) {
    L2_GUARD_FUNCTION();
    static int errorCount = 0;
    SharedLong& count = m_UserMapDensity[index];
    const std::int32_t result = runtime::raw_atomic::DecrementI32(&count);
    if (result < 0 && errorCount < 20) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"UserMapD Decrease ret = [%d]. index = [%d], "
        //     L"pos=[%d, %d, %d] user[%s]",
        //     result,
        //     index,
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     user.GetName())
        // Log.Add(LOG_ERROR,
        //     L"[user density] Negative count %d after decrement at cell %d, position (%d, %d, %d), character \"%ls\". The
        //     count remains stored. This path reports only its first 20 failures.", result, index,
        //     runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z), user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[user density] Negative count %d after decrement at cell %d, position (%d, %d, %d), character \"%ls\". The
        //     count remains stored. This path reports only its first 20 failures.", result, index,
        //     runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z), user.GetName())
        L2_LOG_ERROR(
            Log,
            u"[user density] Negative count %d after decrement at cell %d, position (%d, %d, %d), character \"%ls\"; the "
            u"count remains stored. This path reports only its first 20 failures.",
            result, index, runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), user.GetName()
        );
        runtime::process::ReportCallStack(2, false);
        ++errorCount;
    }
}
#endif
