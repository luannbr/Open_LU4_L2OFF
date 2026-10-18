#pragma once

#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace runtime::raw_atomic {


inline bool LoadBool(const bool* value) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile char*>(const_cast<bool*>(value));
    return _InterlockedCompareExchange8(address, 0, 0) != 0;
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

inline void StoreBool(bool* value, bool desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile char*>(value);
    _InterlockedExchange8(address, desired ? 1 : 0);
#else
    __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#endif
}

// Portable publication reader, paired with StoreI32Release.
inline std::int32_t LoadI32Acquire(const volatile std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(const_cast<volatile std::int32_t*>(value));
    return static_cast<std::int32_t>(_InterlockedCompareExchange(address, 0, 0));
#else
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
#endif
}

inline std::int32_t LoadI32(const volatile std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(const_cast<volatile std::int32_t*>(value));
    return static_cast<std::int32_t>(_InterlockedCompareExchange(address, 0, 0));
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

// Some target inline accessors perform an aligned volatile
// read instead of issuing InterlockedCompareExchange. Preserve that
// distinction while avoiding a C++ data race on non-MSVC hosts.
inline std::int32_t LoadI32Relaxed(const volatile std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    return *value;
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

// The target also has selected aligned volatile stores which are plain MOVs,
// not InterlockedExchange calls. Non-MSVC hosts still need a race-free
// implementation of that source-level volatile-long contract.
inline void StoreI32Relaxed(volatile std::int32_t* value, std::int32_t desired) noexcept {
#if defined(_MSC_VER)
    *value = desired;
#else
    __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#endif
}

inline std::uint32_t LoadU32Relaxed(const volatile std::uint32_t* value) noexcept {
#if defined(_MSC_VER)
    return *value;
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

inline void StoreU32Relaxed(volatile std::uint32_t* value, std::uint32_t desired) noexcept {
#if defined(_MSC_VER)
    *value = desired;
#else
    __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#endif
}

// Plain aligned enum reads/writes in the Win64 images are ordinary DWORD
// MOVs. Keep that instruction contract on MSVC while making the same
// publication race-free under the non-MSVC C++ memory model.
template <typename Enum>
inline Enum LoadEnum32Relaxed(const volatile Enum* value) noexcept {
    static_assert(std::is_enum_v<Enum>);
    static_assert(sizeof(Enum) == sizeof(std::int32_t));
#if defined(_MSC_VER)
    return *value;
#else
    Enum observed{};
    __atomic_load(value, &observed, __ATOMIC_RELAXED);
    return observed;
#endif
}

template <typename Enum>
inline void StoreEnum32Relaxed(volatile Enum* value, Enum desired) noexcept {
    static_assert(std::is_enum_v<Enum>);
    static_assert(sizeof(Enum) == sizeof(std::int32_t));
#if defined(_MSC_VER)
    *value = desired;
#else
    __atomic_store(value, &desired, __ATOMIC_RELAXED);
#endif
}

inline void StoreI32(volatile std::int32_t* value, std::int32_t desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    _InterlockedExchange(address, static_cast<long>(desired));
#else
    __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#endif
}

inline void StoreI32Release(volatile std::int32_t* value, std::int32_t desired) noexcept {
#if defined(_MSC_VER)
    StoreI32(value, desired);
#else
    __atomic_store_n(value, desired, __ATOMIC_RELEASE);
#endif
}

inline std::int32_t ExchangeI32(volatile std::int32_t* value, std::int32_t desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::int32_t>(_InterlockedExchange(address, static_cast<long>(desired)));
#else
    return __atomic_exchange_n(value, desired, __ATOMIC_SEQ_CST);
#endif
}

inline std::uint32_t ExchangeU32(volatile std::uint32_t* value, std::uint32_t desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::uint32_t>(_InterlockedExchange(address, static_cast<long>(desired)));
#else
    return __atomic_exchange_n(value, desired, __ATOMIC_SEQ_CST);
#endif
}

// Win32 InterlockedCompareExchange returns the value that was present before
// the attempted exchange. AtomicValue exposes that return contract directly.
inline std::int32_t CompareExchangeI32(volatile std::int32_t* value, std::int32_t desired, std::int32_t comparand) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::int32_t>(
        _InterlockedCompareExchange(address, static_cast<long>(desired), static_cast<long>(comparand))
    );
#else
    std::int32_t observed = comparand;
    __atomic_compare_exchange_n(value, &observed, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return observed;
#endif
}

inline std::uint32_t CompareExchangeU32(
    volatile std::uint32_t* value, std::uint32_t desired, std::uint32_t comparand
) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::uint32_t>(
        _InterlockedCompareExchange(address, static_cast<long>(desired), static_cast<long>(comparand))
    );
#else
    std::uint32_t observed = comparand;
    __atomic_compare_exchange_n(value, &observed, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return observed;
#endif
}

inline bool CompareExchangeI32Acquire(volatile std::int32_t* value, std::int32_t expected, std::int32_t desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return _InterlockedCompareExchange(address, static_cast<long>(desired), static_cast<long>(expected)) ==
        static_cast<long>(expected);
#else
    return __atomic_compare_exchange_n(value, &expected, desired, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
#endif
}

inline std::int32_t AddI32(volatile std::int32_t* value, std::int32_t delta) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::int32_t>(_InterlockedExchangeAdd(address, static_cast<long>(delta)) + delta);
#else
    return __atomic_add_fetch(value, delta, __ATOMIC_SEQ_CST);
#endif
}

inline std::int32_t IncrementI32(volatile std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::int32_t>(_InterlockedIncrement(address));
#else
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
#endif
}

inline std::int32_t DecrementI32(volatile std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile long*>(value);
    return static_cast<std::int32_t>(_InterlockedDecrement(address));
#else
    return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
#endif
}

// Exact GCC memory orders for CSharedRWLock. MSVC interlocked operations are
// full barriers, so they satisfy the same orders.
inline void StoreU32Release(volatile std::uint32_t* value, std::uint32_t desired) noexcept {
#if defined(_MSC_VER)
    ExchangeU32(value, desired);
#else
    __atomic_store_n(value, desired, __ATOMIC_RELEASE);
#endif
}

inline bool CompareExchangeI32AcqRel(volatile std::int32_t* value, std::int32_t expected, std::int32_t desired) noexcept {
#if defined(_MSC_VER)
    return CompareExchangeI32Acquire(value, expected, desired);
#else
    return __atomic_compare_exchange_n(value, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
}

inline std::int32_t AddI32AcqRel(volatile std::int32_t* value, std::int32_t delta) noexcept {
#if defined(_MSC_VER)
    return AddI32(value, delta);
#else
    return __atomic_add_fetch(value, delta, __ATOMIC_ACQ_REL);
#endif
}

inline std::int64_t LoadI64(const volatile std::int64_t* value) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile __int64*>(const_cast<volatile std::int64_t*>(value));
    return static_cast<std::int64_t>(_InterlockedCompareExchange64(address, 0, 0));
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

// Selected x64 target accessors use an ordinary aligned volatile QWORD read
// instead of InterlockedCompareExchange64. Preserve the MOV distinction on
// MSVC while using a race-free relaxed load on other hosts.
inline std::int64_t LoadI64Relaxed(const volatile std::int64_t* value) noexcept {
#if defined(_MSC_VER)
    return *value;
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

// CSmartQueue's resize-generation check is an ordinary aligned QWORD
// load/store pair in the Win64 image. Preserve those MOV operations for
// MSVC while giving non-MSVC hosts a race-free relaxed publication.
inline std::uint64_t LoadU64Relaxed(const volatile std::uint64_t* value) noexcept {
#if defined(_MSC_VER)
    return *value;
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

inline void StoreU64Relaxed(volatile std::uint64_t* value, std::uint64_t desired) noexcept {
#if defined(_MSC_VER)
    *value = desired;
#else
    __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#endif
}

inline std::int64_t AddI64(volatile std::int64_t* value, std::int64_t delta) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile __int64*>(value);
    return static_cast<std::int64_t>(_InterlockedExchangeAdd64(address, static_cast<__int64>(delta)) + delta);
#else
    return __atomic_add_fetch(value, delta, __ATOMIC_SEQ_CST);
#endif
}

inline std::int64_t ExchangeI64(volatile std::int64_t* value, std::int64_t desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile __int64*>(value);
    return static_cast<std::int64_t>(_InterlockedExchange64(address, static_cast<__int64>(desired)));
#else
    return __atomic_exchange_n(value, desired, __ATOMIC_SEQ_CST);
#endif
}

inline void StoreI64(std::int64_t* value, std::int64_t desired) noexcept {
#if defined(_MSC_VER)
    auto* address = reinterpret_cast<volatile __int64*>(value);
    _InterlockedExchange64(address, static_cast<__int64>(desired));
#else
    __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#endif
}

template <typename T>
inline T* ExchangePointer(T* volatile* value, T* desired) noexcept {
#if defined(_MSC_VER)
    return static_cast<T*>(_InterlockedExchangePointer(reinterpret_cast<void* volatile*>(value), desired));
#else
    return __atomic_exchange_n(value, desired, __ATOMIC_SEQ_CST);
#endif
}

// Win64 InterlockedCompareExchangePointer returns the value observed before
// the attempted exchange. This also models the target's common
// InterlockedCompareExchangePointer(address, nullptr, nullptr) atomic read.
template <typename T>
inline T* CompareExchangePointer(T* volatile* value, T* desired, T* comparand) noexcept {
#if defined(_MSC_VER)
    return static_cast<T*>(_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(value), desired, comparand));
#else
    T* observed = comparand;
    __atomic_compare_exchange_n(value, &observed, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return observed;
#endif
}

// Some target paths read an aligned pointer without an
// interlocked instruction. Keep that source-level distinction without
// introducing a C++ data race in the portable build.
template <typename T>
inline T* LoadPointerRelaxed(T* const volatile* value) noexcept {
#if defined(_MSC_VER)
    return *value;
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

// Win64's ordered aligned MOV publication is enough for these target
// pointer hand-offs. Non-MSVC hosts need the matching C++ acquire /
// release edge so readers cannot observe a partially reinitialized object.
template <typename T>
inline T* LoadPointerAcquire(T* const volatile* value) noexcept {
#if defined(_MSC_VER)
    return *value;
#else
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
#endif
}

template <typename T>
inline void StorePointerRelease(T* volatile* value, T* desired) noexcept {
#if defined(_MSC_VER)
    *value = desired;
#else
    __atomic_store_n(value, desired, __ATOMIC_RELEASE);
#endif
}

} // namespace runtime::raw_atomic
