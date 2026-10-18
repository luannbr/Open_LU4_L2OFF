#pragma once

#include <array>
#include <type_traits>

// Portable factoring of Shared/TemplateHelper.h lock helpers. Keep this
// header independent of product MemoryObject definitions so AuthD and the
// game-server products can use the same helpers without importing each
// other's object model. TemplateHelper.h remains the compatibility include.
#ifndef __WFILE__
#define L2_SCOPED_LOCK_WIDEN_IMPL(value) L##value
#define L2_SCOPED_LOCK_WIDEN(value) L2_SCOPED_LOCK_WIDEN_IMPL(value)
#ifdef __FILE_NAME__
#define __WFILE__ L2_SCOPED_LOCK_WIDEN(__FILE_NAME__)
#else
#define __WFILE__ L2_SCOPED_LOCK_WIDEN(__FILE__)
#endif
#endif

namespace scoped_lock_detail {

template <int Level>
struct Priority : Priority<Level - 1> {};
template <>
struct Priority<0> {};

// Portable selector for owners with separate named lock APIs. The guard calls
// these owner methods, including their instrumentation, not the backend lock.
template <auto Acquire, auto Release>
struct OwnerMethods {};

// Portable adoption after an checked conditional acquisition.
// Only use on the successful branch. The guard must not acquire a second time.
template <auto Release>
struct AdoptOwnerMethod {};

// Portable helper access point: owners with non-public lock methods may
// friend this type without exposing those methods or bypassing their API.
struct OwnerAccess {
    // Prefer the existing location-taking acquisition API. The fallback is only
    // viable for an actual zero-argument method, never for Enter(index).
    template <typename T>
    static auto Enter(T& lock, const wchar_t* file, int line, Priority<5>) -> decltype(lock.Enter(file, line), void()) {
        lock.Enter(file, line);
    }

    template <typename T>
    static auto Enter(T& lock, const wchar_t*, int, Priority<4>) -> decltype(lock.Enter(), void()) {
        lock.Enter();
    }

    // AuthGateD exposes Lock/Unlock on its owner. Keep that API, including its
    // monitoring side effects, instead of reaching through to its atomic field.
    template <typename T>
    static auto Enter(T& lock, const wchar_t*, int, Priority<3>) -> decltype(lock.Lock(), void()) {
        lock.Lock();
    }

    // ADL keeps each native carrier's own namespace/API. No product headers,
    // storage offsets or backend mutex access are introduced into the helper.
    template <typename T>
    static auto Enter(T& lock, const wchar_t*, int, Priority<0>) -> decltype(EnterCriticalSection(lock), void()) {
        EnterCriticalSection(lock);
    }

    template <typename T>
    static auto Leave(T& lock, const wchar_t* file, int line, Priority<5>) -> decltype(lock.Leave(file, line), void()) {
        lock.Leave(file, line);
    }

    template <typename T>
    static auto Leave(T& lock, const wchar_t*, int, Priority<4>) -> decltype(lock.Leave(), void()) {
        lock.Leave();
    }

    template <typename T>
    static auto Leave(T& lock, const wchar_t*, int, Priority<3>) -> decltype(lock.Unlock(), void()) {
        lock.Unlock();
    }

    template <typename T>
    static auto Leave(T& lock, const wchar_t*, int, Priority<0>) -> decltype(LeaveCriticalSection(lock), void()) {
        LeaveCriticalSection(lock);
    }

    template <typename T>
    static auto Enter(T& lock, const wchar_t*, int, Priority<2>) -> decltype(lock._mlock(), void()) {
        lock._mlock();
    }

    template <typename T>
    static auto Leave(T& lock, const wchar_t*, int, Priority<2>) -> decltype(lock._munlock(), void()) {
        lock._munlock();
    }

    template <typename T>
    static auto Leave(T& lock, const wchar_t*, int, Priority<1>) -> decltype(lock.UnLock(), void()) {
        lock.UnLock();
    }

    // Portable indexed-owner support. Never reach through to its lock array.
    // Prefer an owner method that records the caller location.
    template <typename T, typename Index>
    static auto EnterIndexed(T& lock, Index index, const wchar_t* file, int line, Priority<1>)
        -> decltype(lock.Enter(index, file, line), void()) {
        lock.Enter(index, file, line);
    }

    template <typename T, typename Index>
    static auto EnterIndexed(T& lock, Index index, const wchar_t*, int, Priority<0>) -> decltype(lock.Enter(index), void()) {
        lock.Enter(index);
    }

    template <typename T, typename Index>
    static auto LeaveIndexed(T& lock, Index index, const wchar_t*, int) -> decltype(lock.Leave(index), void()) {
        lock.Leave(index);
    }

    template <typename T, auto Acquire, auto Release>
    static void EnterIndexed(T& lock, OwnerMethods<Acquire, Release>, const wchar_t* file, int line, Priority<0>) {
        if constexpr (std::is_invocable_v<decltype(Acquire), T&, const wchar_t*, int>) {
            (lock.*Acquire)(file, line);
        } else {
            (lock.*Acquire)();
        }
    }

    template <typename T, auto Acquire, auto Release>
    static void LeaveIndexed(T& lock, OwnerMethods<Acquire, Release>, const wchar_t* file, int line) {
        if constexpr (std::is_invocable_v<decltype(Release), T&, const wchar_t*, int>) {
            (lock.*Release)(file, line);
        } else {
            (lock.*Release)();
        }
    }

    template <typename T, auto Release>
    static void EnterIndexed(T&, AdoptOwnerMethod<Release>, const wchar_t*, int, Priority<0>) {}

    template <typename T, auto Release>
    static void LeaveIndexed(T& lock, AdoptOwnerMethod<Release>, const wchar_t*, int) {
        (lock.*Release)();
    }
};

template <typename T>
auto ReadLock(T& lock, const wchar_t* file, int line, int) -> decltype(lock.ReadLock(file, line), void()) {
    lock.ReadLock(file, line);
}

template <typename T>
auto ReadLock(T& lock, const wchar_t*, int, long) -> decltype(lock.ReadLock(), void()) {
    lock.ReadLock();
}

template <typename T>
auto WriteLock(T& lock, const wchar_t* file, int line, int) -> decltype(lock.WriteLock(file, line), void()) {
    lock.WriteLock(file, line);
}

template <typename T>
auto WriteLock(T& lock, const wchar_t*, int, long) -> decltype(lock.WriteLock(), void()) {
    lock.WriteLock();
}

// Read/write guards keep the zero-argument release when available.
template <typename T>
auto ReadUnlock(T& lock, const wchar_t*, int, int) -> decltype(lock.ReadUnlock(), void()) {
    lock.ReadUnlock();
}

template <typename T>
auto ReadUnlock(T& lock, const wchar_t* file, int line, long) -> decltype(lock.ReadUnlock(file, line), void()) {
    lock.ReadUnlock(file, line);
}

template <typename T>
auto WriteUnlock(T& lock, const wchar_t*, int, int) -> decltype(lock.WriteUnlock(), void()) {
    lock.WriteUnlock();
}

template <typename T>
auto WriteUnlock(T& lock, const wchar_t* file, int line, long) -> decltype(lock.WriteUnlock(file, line), void()) {
    lock.WriteUnlock(file, line);
}

// Portable array support keeps all-bucket snapshots: acquire ascending,
// release descending. ReadGuard still stores one pointer and allocates nothing.
template <typename T, std::size_t Count>
void ReadLock(std::array<T, Count>& locks, const wchar_t* file, int line, int) {
    for (auto& lock : locks) {
        ReadLock(lock, file, line, 0);
    }
}

template <typename T, std::size_t Count>
void ReadUnlock(std::array<T, Count>& locks, const wchar_t* file, int line, int) {
    for (std::size_t index = Count; index > 0; --index) {
        ReadUnlock(locks[index - 1], file, line, 0);
    }
}

} // namespace scoped_lock_detail

// Portable indexed form stores the acquisition index by value, so
// later caller changes cannot select a different lock during release.
template <typename T, typename Index = void>
class Guard {
public:
    Guard(T* lock, Index index, const wchar_t* callerFile, int callerLine) : mpLock(lock), mIndex(index) {
        scoped_lock_detail::OwnerAccess::EnterIndexed(
            *mpLock, mIndex, callerFile, callerLine, scoped_lock_detail::Priority<1>{}
        );
    }

    ~Guard() { scoped_lock_detail::OwnerAccess::LeaveIndexed(*mpLock, mIndex, __WFILE__, __LINE__); }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    T* mpLock;
    Index mIndex;
};

// PDB: template<class T> class Guard contains only T* mpLock.
// Its constructor is inline and records the caller's source location on
// Enter. The out-of-line destructor is TemplateHelper.h:98-104 and records
// line 0x65 on Leave.
template <typename T>
class Guard<T, void> {
public:
    Guard(T* lock, const wchar_t* callerFile, int callerLine) : mpLock(lock) {
        scoped_lock_detail::OwnerAccess::Enter(*mpLock, callerFile, callerLine, scoped_lock_detail::Priority<5>{});
    }

    ~Guard() { scoped_lock_detail::OwnerAccess::Leave(*mpLock, __WFILE__, __LINE__, scoped_lock_detail::Priority<5>{}); }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    T* mpLock;
};

template <typename T>
Guard(T*, const wchar_t*, int) -> Guard<T>;

template <typename T, typename Index>
Guard(T*, Index, const wchar_t*, int) -> Guard<T, Index>;

// Shared/TemplateHelper.h: the instantiated ReadGuard<CInventory> in the PDB
// contains exactly one CInventory* and delegates to ReadLock/ReadUnlock.
template <typename T>
class ReadGuard {
public:
    ReadGuard(T& lock, const wchar_t* callerFile, int callerLine) : mpLock(&lock) {
        scoped_lock_detail::ReadLock(*mpLock, callerFile, callerLine, 0);
    }

    ~ReadGuard() { scoped_lock_detail::ReadUnlock(*mpLock, __WFILE__, __LINE__, 0); }

    ReadGuard(const ReadGuard&) = delete;
    ReadGuard& operator=(const ReadGuard&) = delete;

private:
    T* mpLock;
};

// Shared/TemplateHelper.h counterpart used by CNPCAIManager. The PDB
// records the exact { T* mpLock. } layout and a (T&, file, line)
// constructor. Destruction releases the write side of the RW lock.
template <typename T>
class WriteGuard {
public:
    WriteGuard(T& lock, const wchar_t* callerFile, int callerLine) : mpLock(&lock) {
        scoped_lock_detail::WriteLock(*mpLock, callerFile, callerLine, 0);
    }

    ~WriteGuard() { scoped_lock_detail::WriteUnlock(*mpLock, __WFILE__, __LINE__, 0); }

    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;

private:
    T* mpLock;
};

#define L2_TEMPLATE_HELPER_CONCAT_IMPL(left, right) left##right
#define L2_TEMPLATE_HELPER_CONCAT(left, right) L2_TEMPLATE_HELPER_CONCAT_IMPL(left, right)

#define L2_TEMPLATE_HELPER_UNIQUE(prefix) L2_TEMPLATE_HELPER_CONCAT(prefix, __LINE__)

#define L2_SCOPED_GUARD_SINGLE(lock) ::Guard L2_TEMPLATE_HELPER_UNIQUE(l2_guard_)(&(lock), __WFILE__, __LINE__)

#define L2_SCOPED_GUARD_INDEXED(lock, index)                                                                                \
    ::Guard L2_TEMPLATE_HELPER_UNIQUE(l2_guard_)(&(lock), (index), __WFILE__, __LINE__)

#define L2_SCOPED_GUARD_SELECT(first, second, selected, ...) selected
#define L2_GUARD(...)                                                                                                       \
    L2_SCOPED_GUARD_SELECT(__VA_ARGS__, L2_SCOPED_GUARD_INDEXED, L2_SCOPED_GUARD_SINGLE, unused)(__VA_ARGS__)

#define L2_READ_GUARD(lock) ::ReadGuard L2_TEMPLATE_HELPER_UNIQUE(l2_read_guard_)((lock), __WFILE__, __LINE__)

#define L2_WRITE_GUARD(lock) ::WriteGuard L2_TEMPLATE_HELPER_UNIQUE(l2_write_guard_)((lock), __WFILE__, __LINE__)
