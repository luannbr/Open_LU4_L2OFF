// Recovered from shared/sharedefine.cpp.
#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/SharedType.h"

#include <cstddef>
#include <cstdint>

#if defined(L2_BUILD_L2SERVER)
class User;
#endif

// Windows `long` is 32-bit in the target ABI on both x86 and x64.
using SharedLong = std::int32_t;

class SharedUserMapDensity {
public:
    static SharedUserMapDensity& Inst();

    void Init();
    SharedLong CountByPos(const FVector& position);
#if defined(L2_BUILD_L2SERVER)
    void Increase(int index, const FVector& position, User& user);
    void Decrease(int index, const FVector& position, User& user);
#endif

private:
    SharedUserMapDensity();

    CSharedArray<SharedLong, 87040> m_UserMapDensity;
};

inline constexpr const char* kUserMapDensitySharedMemoryName = "UserMapDensity";
inline constexpr std::size_t kUserMapDensityRecordCount = 0x15400;
inline constexpr std::size_t kUserMapDensitySharedMemoryBytes = kUserMapDensityRecordCount * sizeof(SharedLong);

// Exact process global at target 0x006afcd3. The shipping image initializes
// it to TRUE. The GUI command toggles it at runtime.
extern bool g_bSilentWhenNoUser;
