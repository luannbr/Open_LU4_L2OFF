#pragma once

#include "L2Core/src/inc/Guard.h"
#include "L2Core/src/inc/XStd.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TypeManip.h"
// Main portable implementation of the CSPointer/CObjectMap portion of
// shipping Shared/ThreadSafeLibrary.h. L2NPC, CacheD and L2Server instantiate
// these exact global templates. Process diagnostics are adapter calls, not a
// reason to fork the class or hide it behind executable-local aliases.

#include "ProcessRuntime.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/platform/Runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>


// Runtime functor recovered with CObjectMap. The target has one pure virtual
// slot and no virtual destructor.
class CObjectMapFunc {
public:
    virtual bool Do(MemoryObject* object) = 0;
};


template <typename TObject>
class CSPointer {
public:
    CSPointer() = default;

    CSPointer(TObject* object, ObjectRefType type) { SetObject(object, type); }

    CSPointer(const CSPointer& other) : m_p(other.m_p), m_ort(other.m_ort) {
        // Current Cached.exe 0x004fa6d4 is the folded copy-construction body:
        // copy the pointer/ref type, then virtually AddRef at template line
        // 0x9b with that same ref type. It releases nothing.
        if (m_p) {
            L2_ADD_REF(m_p, m_ort);
        }
    }

    CSPointer(CSPointer&& other) noexcept : m_p(other.m_p), m_ort(other.m_ort) {
        other.m_p = nullptr;
        other.m_ort = ObjectRefType::Anonymous;
    }

    ~CSPointer() { Release(); }

    CSPointer& operator=(const CSPointer& other) {
        // Current Cached.exe 0x0047afd8 is the folded copy-assignment wrapper.
        // Its only non-self path forwards the two source fields to SetObject.
        if (this != &other) {
            SetObject(other.m_p, other.m_ort);
        }
        return *this;
    }

    CSPointer& operator=(CSPointer&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Release();
        m_p = other.m_p;
        m_ort = other.m_ort;
        other.m_p = nullptr;
        other.m_ort = ObjectRefType::Anonymous;
        return *this;
    }

    TObject* operator->() const {
        if (!m_p) {
            runtime::process::ReportNullSmartPointerDereference();
        }
        return m_p;
    }

    TObject* get() const noexcept { return m_p; }
    explicit operator bool() const noexcept { return m_p != nullptr; }
    ObjectRefType ReferenceType() const noexcept { return m_ort; }

    void SetObject(TObject* object, ObjectRefType type) {
        // Current Cached.exe 0x004602dc is the folded SetObject body. It
        // publishes and keeps the replacement before releasing the prior
        // pointer at template line 0x4c, including when both pointers match.
        TObject* previous = m_p;
        const ObjectRefType previousType = m_ort;
        m_p = object;
        m_ort = type;
        if (m_p) {
            L2_ADD_REF(m_p, m_ort);
        }
        if (previous) {
            L2_RELEASE(previous, previousType);
        }
    }

    void Release() {
        if (!m_p) {
            return;
        }
        L2_RELEASE(m_p, m_ort);
        m_p = nullptr;
    }

private:
    TObject* m_p = nullptr;
    ObjectRefType m_ort = ObjectRefType::Anonymous;
};

// Main Shared/ThreadSafeLibrary.h owner used by current CacheD for
// warehouses and pet warehouses. The target stores only one pointer in each
// instance and protects pointer replacement/retention with 1023 type-local
// RWLocks selected from the old pointer address.
template <typename TObject>
class CObjectStorage {
public:
    CObjectStorage() = default;

    ~CObjectStorage() {
        if (m_object) {
            L2_RELEASE(m_object, ObjectRefType::ObjectStorage);
        }
    }

    CObjectStorage(const CObjectStorage&) = delete;
    CObjectStorage& operator=(const CObjectStorage&) = delete;

    void SetObject(TObject* object) {
        L2_GUARD_FUNCTION();
        int key = static_cast<int>((reinterpret_cast<std::uintptr_t>(m_object) >> 3) % kLockCount);
        if (key < 0) {
            runtime::process::ReportObjectStorageKeyBelowZero(key, reinterpret_cast<std::intptr_t>(m_object));
            key = 0;
        }
        if (key > static_cast<int>(kLockCount - 1)) {
            runtime::process::ReportObjectStorageKeyAboveMaximum(
                key, reinterpret_cast<std::intptr_t>(m_object), static_cast<int>(kLockCount)
            );
            key = 0;
        }
        RWLock& lock = s_locks[static_cast<std::size_t>(key)];
        L2_WRITE_GUARD(lock);
        if (m_object) {
            L2_RELEASE(m_object, ObjectRefType::ObjectStorage);
        }
        m_object = object;
        if (m_object) {
            L2_ADD_REF(m_object, ObjectRefType::ObjectStorage);
        }
    }

    CSPointer<TObject> GetObjectW() {
        L2_GUARD_FUNCTION();
        int key = static_cast<int>((reinterpret_cast<std::uintptr_t>(m_object) >> 3) % kLockCount);
        if (key < 0) {
            runtime::process::ReportObjectStorageKeyBelowZero(key, reinterpret_cast<std::intptr_t>(m_object));
            key = 0;
        }
        if (key > static_cast<int>(kLockCount - 1)) {
            runtime::process::ReportObjectStorageKeyAboveMaximum(
                key, reinterpret_cast<std::intptr_t>(m_object), static_cast<int>(kLockCount)
            );
            key = 0;
        }
        RWLock& lock = s_locks[static_cast<std::size_t>(key)];
        CSPointer<TObject> result;
        {
            L2_READ_GUARD(lock);
            result.SetObject(m_object, ObjectRefType::ObjectStorage);
        }
        return result;
    }

private:
    static constexpr std::size_t kLockCount = 1023;

    inline static std::array<RWLock, kLockCount> s_locks{};
    TObject* m_object = nullptr;
};


namespace detail {

template <typename TValue>
constexpr auto ObjectMapUnsignedMagnitude(TValue value) {
    static_assert(std::is_integral<TValue>::value);
    using Unsigned = typename std::make_unsigned<TValue>::type;
    Unsigned bits = static_cast<Unsigned>(value);
    if constexpr (std::is_signed<TValue>::value) {
        if (value < 0) {
            bits = Unsigned{0} - bits;
        }
    }
    return bits;
}

template <typename TKey>
constexpr const TKey& ObjectMapBucketValue(const TKey& key, long) {
    return key;
}

template <typename TKey>
constexpr auto ObjectMapBucketValue(const TKey& key, int) -> decltype(key.m_SecondaryKey) {
    return key.m_SecondaryKey;
}

} // namespace detail

#if defined(L2_BUILD_L2SERVER)
class User;
#endif

template <typename TObject, int BucketCount, int ReferenceTypeValue, typename TLock, typename TKey>
class CObjectMap {
    static_assert(
        BucketCount > 0 && (BucketCount & (BucketCount - 1)) == 0, "CObjectMap bucket count must be a power of two"
    );
    static_assert(ReferenceTypeValue >= 0);

    static constexpr ObjectRefType kReferenceType = static_cast<ObjectRefType>(ReferenceTypeValue);

public:
    enum { OrtType = ReferenceTypeValue };
    enum { m_nBucketCount = BucketCount };

    using pointer_type = CSPointer<TObject>;
    using map_type = std::map<TKey, TObject*>;

    CObjectMap() { L2_GUARD_FUNCTION(); }

    ~CObjectMap() {
        L2_GUARD_FUNCTION();
        for (int bucket = 0; bucket < BucketCount; ++bucket) {
            L2_WRITE_GUARD(m_lock[static_cast<std::size_t>(bucket)]);
            for (const auto& entry : m_map[static_cast<std::size_t>(bucket)]) {
                L2_RELEASE(entry.second, kReferenceType);
            }
        }
    }

    CObjectMap(const CObjectMap&) = delete;
    CObjectMap& operator=(const CObjectMap&) = delete;

    int IdToBucket(TKey id) const {
        const auto value = detail::ObjectMapBucketValue(id, 0);
        const auto magnitude = detail::ObjectMapUnsignedMagnitude(value);
        return static_cast<int>(magnitude & static_cast<decltype(magnitude)>(BucketCount - 1));
    }

    bool ExistKey(TKey id) {
        // Current L2Server CUserSocket/User specializations at
        // 0x00546178..0x005465f8 keep the template guard frame and the
        // original line-0x190 read-lock diagnostic.
        L2_GUARD_FUNCTION();
        const std::size_t bucket = static_cast<std::size_t>(IdToBucket(id));
        L2_READ_GUARD(m_lock[bucket]);
        return m_map[bucket].find(id) != m_map[bucket].end();
    }

    bool Insert(TKey id, TObject* object, const char* callerFile = nullptr, int callerLine = -1) {
        L2_GUARD_FUNCTION();
        const std::size_t bucket = static_cast<std::size_t>(IdToBucket(id));
        L2_WRITE_GUARD(m_lock[bucket]);
        auto& objects = m_map[bucket];
        if (objects.find(id) != objects.end()) {
            return false;
        }
        const char* file = callerFile ? callerFile : L2_SOURCE_FILE;
        const int line = callerFile ? callerLine : __LINE__;
        object->AddRef(file, line, kReferenceType);
        objects.emplace(id, object);
        return true;
    }

    pointer_type FindObject(TKey id) {
        // Current CacheD emits the shared one-bucket/int specialization at
        // 0x00409aa4, 1024-bucket/int CItem specialization at 0x004e9104,
        // 1024-bucket/dual-key CPet specialization at 0x004fff90, folded
        // one-bucket/dual-key specialization at 0x0050e91c, folded
        // 128-bucket/dual-key specialization at 0x0051264c, and both
        // 16,384-bucket CUser specializations at 0x00512594 (dual key) and
        // 0x005c2148 (account ID). All take the template line-0x1c6 read
        // lock and keep a hit with the specialization's reference type
        // before unlocking.
        const std::size_t bucket = static_cast<std::size_t>(IdToBucket(id));
        L2_READ_GUARD(m_lock[bucket]);
        const auto& objects = m_map[bucket];
        const auto found = objects.find(id);
        return found == objects.end() ? pointer_type{} : pointer_type{found->second, kReferenceType};
    }

    bool DeleteObject(TKey id, const char* callerFile = nullptr, int callerLine = -1) {
        L2_GUARD_FUNCTION();
        const std::size_t bucket = static_cast<std::size_t>(IdToBucket(id));
        L2_WRITE_GUARD(m_lock[bucket]);
        auto& objects = m_map[bucket];
        const auto found = objects.find(id);
        if (found == objects.end()) {
            return false;
        }
        const char* file = callerFile ? callerFile : L2_SOURCE_FILE;
        const int line = callerFile ? callerLine : __LINE__;
        found->second->Release(file, line, kReferenceType, false);
        objects.erase(found);
        return true;
    }

    int DeleteAllObjects(const char* callerFile = nullptr, int callerLine = -1) {
        L2_GUARD_FUNCTION();
        int deleted = 0;
        for (int bucket = 0; bucket < BucketCount; ++bucket) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            L2_WRITE_GUARD(m_lock[index]);
            auto& objects = m_map[index];
            for (const auto& entry : objects) {
                const char* file = callerFile ? callerFile : L2_SOURCE_FILE;
                const int line = callerFile ? callerLine : __LINE__;
                entry.second->Release(file, line, kReferenceType, false);
                ++deleted;
            }
            objects.clear();
        }
        return deleted;
    }

    int GetSize() {
        L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
        std::optional<GuardHelper> exactCallStack;
        if constexpr (std::is_same_v<TObject, User> && BucketCount == 8192 && ReferenceTypeValue == 5 &&
            std::is_same_v<TLock, RWLock> && std::is_same_v<TKey, int>) {
            exactCallStack.emplace(
                L"int __cdecl CObjectMap<class User,8192,5,class "
                L"RWLock,int>::GetSize(void)",
                CThreadManager::GetThreadID()
            );
        }
#endif
        int size = 0;
        {
            L2_READ_GUARD(m_lock);
            for (const auto& objects : m_map) {
                size = runtime::AddInt32Wrapping(size, static_cast<int>(objects.size()));
            }
        }
        return size;
    }

    // Current CacheD emits this as the standalone
    // CObjectMap<...>::FOR_ALL(CObjectMapFunc&) instantiation. Keeping the
    // base reference is material: each object is dispatched through the
    // functor's sole virtual slot.
    void FOR_ALL(CObjectMapFunc& function) const {
#if defined(L2_BUILD_L2SERVER)
        // Current L2Server emits a guarded standalone specialization and
        // does not run CacheD's per-bucket TimeChecker watchdog.
        L2_GUARD_FUNCTION();
#endif
        bool keepGoing = true;
        for (int bucket = 0; bucket < BucketCount && keepGoing; ++bucket) {
            const std::size_t index = static_cast<std::size_t>(bucket);
#if !defined(L2_BUILD_L2SERVER)
            const std::uint32_t begin = runtime::GetTickCount();
#endif
            {
                L2_READ_GUARD(m_lock[index]);
                for (const auto& entry : m_map[index]) {
                    if (!function.Do(entry.second)) {
                        keepGoing = false;
                        break;
                    }
                }
            }
#if !defined(L2_BUILD_L2SERVER)
            runtime::process::ReportObjectMapForAllDuration(
                runtime::GetTickCount() - begin, bucket, static_cast<int>(m_map[index].size())
            );
#endif
        }
    }

    template <typename TFunction>
    void FOR_ALL(TFunction* function) const {
        bool keepGoing = true;
        for (int bucket = 0; bucket < BucketCount && keepGoing; ++bucket) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            const std::uint32_t begin = runtime::GetTickCount();
            {
                L2_READ_GUARD(m_lock[index]);
                for (const auto& entry : m_map[index]) {
                    if (!function->Do(entry.second)) {
                        keepGoing = false;
                        break;
                    }
                }
            }
            runtime::process::ReportObjectMapForAllDuration(
                runtime::GetTickCount() - begin, bucket, static_cast<int>(m_map[index].size())
            );
        }
    }

    // Retain the caller-facing overload. Portable guard macros intentionally
    // record their expansion site instead of explicit historical coordinates.
    template <typename TFunction>
    void FOR_ALL(TFunction* function, [[maybe_unused]] const wchar_t* callerFile, [[maybe_unused]] int callerLine) const {
        bool keepGoing = true;
        for (int bucket = 0; bucket < BucketCount && keepGoing; ++bucket) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            L2_READ_GUARD(m_lock[index]);
            for (const auto& entry : m_map[index]) {
#if defined(L2_BUILD_L2SERVER)
                GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
#endif
                if (!function->Do(entry.second)) {
                    keepGoing = false;
                    break;
                }
            }
        }
    }

    template <typename TFunction>
    void FOR_ALL_EX(TFunction function) {
        L2_GUARD_FUNCTION();
        bool keepGoing = true;
        for (int bucket = 0; bucket < BucketCount && keepGoing; ++bucket) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            L2_READ_GUARD(m_lock[index]);
            for (const auto& entry : m_map[index]) {
                if (!function(entry.second)) {
                    keepGoing = false;
                    break;
                }
            }
        }
    }

#if defined(L2_BUILD_L2SERVER)
    void FOR_ALL_DO_AND_CLEAR(CObjectMapFunc& function, const char* callerFile = nullptr, int callerLine = -1) {
        L2_GUARD_FUNCTION();
#else
    template <typename TFunction>
    void FOR_ALL_DO_AND_CLEAR(TFunction* function, const char* callerFile = nullptr, int callerLine = -1) {
#endif
        for (int bucket = 0; bucket < BucketCount; ++bucket) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            L2_WRITE_GUARD(m_lock[index]);
            auto& objects = m_map[index];
            auto current = objects.begin();
            while (current != objects.end()) {
#if defined(L2_BUILD_L2SERVER)
                if (!function.Do(current->second)) {
#else
                if (!function->Do(current->second)) {
#endif
                    // Current L2Server.exe 0x00a17630 and Cached.exe
                    // 0x005d5734 both retry the same entry until the callback
                    // succeeds. Neither advances nor clears a failed entry.
                    continue;
                }
                current->second->Release(callerFile, callerLine, kReferenceType, false);
                current = objects.erase(current);
            }
        }
    }

    // The target's TypeChecker and compile-time-error markers occupy eight
    // bytes before the map array. Keeping one neutral carrier here keeps
    // that layout without duplicating the Loki metaprogram in each process.
    std::array<std::byte, 8> m_compileTimeMarkers;
    std::array<map_type, BucketCount> m_map;
    mutable std::array<TLock, BucketCount> m_lock;
};

// The shipping FOR_ALL_OBJECT_IN_OBJECT_MAP construct is expanded in its
// caller: it owns the prefixed call-stack frame, locks one bucket at a time,
// enters the per-object "in_code" frame and executes the caller's statements
// directly. Keep it as a statement macro so continue/break/return retain
// caller/loop meaning instead of being hidden behind an invented visitor.
//
// __WFILE__, __LINE__ and the compiler function signature come
// from the invocation site. Recovered source line numbers need not equal the
// historical NCSoft source lines embedded in the pinned shipping image.
#define FOR_ALL_OBJECT_IN_OBJECT_MAP(objectMapExpression, objectVariable, ...)                                              \
    do {                                                                                                                    \
        L2_GUARD_PREFIXED_FUNCTION(                                                                                         \
            L"FOR_ALL_OBJECT_IN_OBJECT_MAP <=== ", l2ForAllObjectMapFunctionName, l2ForAllObjectMapGuard                    \
        );                                                                                                                  \
        auto& l2ForAllObjectMap = (objectMapExpression);                                                                    \
        using L2ForAllObjectMapType = std::remove_reference_t<decltype(l2ForAllObjectMap)>;                                 \
        for (int l2ForAllObjectMapBucket = 0; l2ForAllObjectMapBucket < L2ForAllObjectMapType::m_nBucketCount;              \
            ++l2ForAllObjectMapBucket) {                                                                                    \
            const std::size_t l2ForAllObjectMapIndex = static_cast<std::size_t>(l2ForAllObjectMapBucket);                   \
            L2_READ_GUARD(l2ForAllObjectMap.m_lock[l2ForAllObjectMapIndex]);                                                \
            for (const auto& l2ForAllObjectMapEntry : l2ForAllObjectMap.m_map[l2ForAllObjectMapIndex]) {                    \
                GuardHelper l2ForAllObjectMapInCodeGuard(L"in_code", CThreadManager::GetThreadID());                        \
                auto* const objectVariable = l2ForAllObjectMapEntry.second;                                                 \
                __VA_ARGS__                                                                                                 \
            }                                                                                                               \
        }                                                                                                                   \
    } while (false)

// Current CacheD instantiates this Shared owner for CUser, CPledge and
// BossInfo. The target object is a 0x20-byte xmap followed by a 0x38-byte
// RWLock. Object references are kept as ObjectNameMap (numeric ORT 6).
// Insert leaves an existing key untouched, matching the current
// CObjectStringMap<CUser>::Insert body at 0x005c4954.
template <typename TObject>
class CObjectStringMap {
public:
    using pointer_type = CSPointer<TObject>;
    using map_type = std::map<std::u16string, TObject*, std::less<>>;

    CObjectStringMap() {
        // Current L2Server CAlliance/CPledge/User instantiations at
        // 0x0069d3f4/0x009880bc/0x00b64a20 enter their own guard only after
        // the map and RWLock members have been constructed.
        L2_GUARD_FUNCTION();
    }

    ~CObjectStringMap() {
        L2_WRITE_GUARD(m_lock);
        for (const auto& entry : m_map) {
            L2_RELEASE(entry.second, ObjectRefType::ObjectNameMap);
        }
    }

    CObjectStringMap(const CObjectStringMap&) = delete;
    CObjectStringMap& operator=(const CObjectStringMap&) = delete;

    bool ExistKey(std::u16string_view name) {
        // Current L2Server CObjectStringMap<User>::ExistKey at 0x00546c38
        // takes the line-0x2c8 read lock. Portable text is a borrowed view.
        L2_GUARD_FUNCTION();
        L2_READ_GUARD(m_lock);
        return m_map.find(name) != m_map.end();
    }

    void Insert(std::u16string_view name, TObject* object, const char* callerFile = nullptr, int callerLine = -1) {
        L2_WRITE_GUARD(m_lock);
        if (m_map.find(name) != m_map.end()) {
            return;
        }

        const char* file = callerFile ? callerFile : L2_SOURCE_FILE;
        const int line = callerFile ? callerLine : __LINE__;
        object->AddRef(file, line, ObjectRefType::ObjectNameMap);
        m_map.emplace(std::u16string(name), object);
    }

    pointer_type FindObject(std::u16string_view name) {
        // Portable text API borrows UTF-16 during lookup. Object keep and
        // lock boundaries are unchanged. Only insertion owns a key copy.
        L2_READ_GUARD(m_lock);
        const auto found = m_map.find(name);
        return found == m_map.end() ? pointer_type{} : pointer_type{found->second, ObjectRefType::ObjectNameMap};
    }

    void DeleteObject(std::u16string_view name, const char* callerFile = nullptr, int callerLine = -1) {
        L2_WRITE_GUARD(m_lock);
        const auto found = m_map.find(name);
        if (found == m_map.end()) {
            return;
        }

        const char* file = callerFile ? callerFile : L2_SOURCE_FILE;
        const int line = callerFile ? callerLine : __LINE__;
        found->second->Release(file, line, ObjectRefType::ObjectNameMap, false);
        m_map.erase(found);
    }

    int GetSize() {
        L2_READ_GUARD(m_lock);
        return static_cast<int>(m_map.size());
    }

private:
    map_type m_map;
    RWLock m_lock;
};


// Shared/ThreadSafeLibrary.h owner used by the current CacheD name indexes.
// The outer integer key selects a world and the inner string map keeps the
// object as ObjectNameMap. Current CUser instantiations at 0x005c51f0,
// 0x005c4ca8 and 0x005c5ba0 confirm the RWLock + xmap layout and lock lines.
template <typename TObject>
class CObjectByIntKeyStringMap {
public:
    using pointer_type = CSPointer<TObject>;
    using string_map_type = CObjectStringMap<TObject>;
    using map_type = std::map<int, string_map_type*>;

    CObjectByIntKeyStringMap() = default;
    // Current Cached.exe 0x00516af0 is the folded CPledge/CUser
    // specialization: construct the leading RWLock, then create and
    // self-link the outer xmap<int,CObjectStringMap<T>*> sentinel.

    ~CObjectByIntKeyStringMap() {
        // Current CPledge specialization 0x005176bc takes the line-0x37f
        // write lock, deletes every inner CObjectStringMap, clears/releases
        // the outer tree and finally destroys the leading RWLock.
        L2_WRITE_GUARD(m_lock);
        for (const auto& entry : m_map) {
            delete entry.second;
        }
        m_map.clear();
    }

    CObjectByIntKeyStringMap(const CObjectByIntKeyStringMap&) = delete;
    CObjectByIntKeyStringMap& operator=(const CObjectByIntKeyStringMap&) = delete;

    bool ExistKey(int key, std::u16string_view id) {
        // Current L2Server CObjectByIntKeyStringMap<User>::ExistKey at
        // 0x005582e0 takes the outer write lock while the
        // inner string map takes its own read lock.
        L2_WRITE_GUARD(m_lock);
        const auto found = m_map.find(key);
        return found != m_map.end() && found->second->ExistKey(id);
    }

    pointer_type FindObject(int key, std::u16string_view id) {
        // The shipping template takes the outer write lock even
        // though this operation does not mutate either map.
        L2_WRITE_GUARD(m_lock);
        const auto found = m_map.find(key);
        return found == m_map.end() ? pointer_type{} : found->second->FindObject(id);
    }

    void Insert(int key, std::u16string_view id, TObject* object, const char* callerFile = nullptr, int callerLine = -1) {
        L2_WRITE_GUARD(m_lock);
        const auto found = m_map.find(key);
        if (found != m_map.end()) {
            found->second->Insert(id, object, callerFile, callerLine);
            return;
        }

        auto* const strings = new string_map_type;
        // Current code does not forward the caller metadata when it creates
        // a new per-key map. CObjectStringMap therefore uses its defaults.
        strings->Insert(id, object);
        m_map.emplace(key, strings);
    }

    void DeleteObject(int key, std::u16string_view id, const char* callerFile = nullptr, int callerLine = -1) {
        L2_WRITE_GUARD(m_lock);
        const auto found = m_map.find(key);
        if (found != m_map.end()) {
            found->second->DeleteObject(id, callerFile, callerLine);
        }
    }

private:
    RWLock m_lock;
    map_type m_map;
};


#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <typeinfo>


class CCreature;
class CCreatureController;
class CFishingController;
class CObject;
class CNPC;
class NpcMakerEx;
class CStaticObject;
#if defined(L2_BUILD_L2SERVER)
struct CTL_WorldMapNode2;
#endif
using std::CDualKey;

namespace thread_safe_detail {

// Out-of-line only to avoid the log.h -> logcli.h -> ThreadSafeLibrary.h
// include cycle. The shipping template spells this diagnostic inline.
void ReportNullSmartPointerDereference();
void ReportMwsrQueueLowMemory(const char* allocationName);
void ReportNewMwsrQueueNull(const wchar_t* allocationName);
void RecordMwsrQueueAllocation(std::size_t byteCount);
void CrashNewMwsrQueueInvariant(int reason, std::source_location location = std::source_location::current());
void CrashMwmrQueueOverflow(std::source_location location = std::source_location::current());

template <typename TOwner>
constexpr const wchar_t* ExecutionSerializerAddExecutableGuardName() {
#if defined(L2_BUILD_L2SERVER)
    if constexpr (std::is_same_v<TOwner, CCreatureController>) {
        return L"void __cdecl CExecutionSerializer<class "
               L"CCreatureController,64>::AddExecutable(class MemoryObject "
               L"*,void (__cdecl MemoryObject::* )(class "
               L"CExecutionArgumentPad *),class CExecutionArgument *,const "
               L"wchar_t *)";
    } else if constexpr (std::is_same_v<TOwner, CFishingController>) {
        return L"void __cdecl CExecutionSerializer<class "
               L"CFishingController,64>::AddExecutable(class MemoryObject "
               L"*,void (__cdecl MemoryObject::* )(class "
               L"CExecutionArgumentPad *),class CExecutionArgument *,const "
               L"wchar_t *)";
    } else if constexpr (std::is_same_v<TOwner, CCreature>) {
        return L"void __cdecl CExecutionSerializer<class CCreature,32>::"
               L"AddExecutable(class MemoryObject *,void (__cdecl "
               L"MemoryObject::* )(class CExecutionArgumentPad *),class "
               L"CExecutionArgument *,const wchar_t *)";
    } else
#endif
        if constexpr (std::is_same_v<TOwner, CNPC>) {
        return L"void __cdecl CExecutionSerializer<class CNPC,32>::"
               L"AddExecutable(class MemoryObject *,void (__cdecl "
               L"MemoryObject::* )(class CExecutionArgumentPad *),class "
               L"CExecutionArgument *,const wchar_t *)";
    } else if constexpr (std::is_same_v<TOwner, NpcMakerEx>) {
        return L"void __cdecl CExecutionSerializer<class NpcMakerEx,32>::"
               L"AddExecutable(class MemoryObject *,void (__cdecl "
               L"MemoryObject::* )(class CExecutionArgumentPad *),class "
               L"CExecutionArgument *,const wchar_t *)";
    } else {
        return L"void __cdecl CExecutionSerializer::AddExecutable("
               L"class MemoryObject *,void (__cdecl MemoryObject::* )("
               L"class CExecutionArgumentPad *),class CExecutionArgument *,"
               L"const wchar_t *)";
    }
}

} // namespace thread_safe_detail

// Fixed-storage multi-writer/multi-reader ring used by current L2Server's
// CThreadLocalTimer. The template is inline: N objects, N
// pointers, a four-byte spin lock, two volatile 64-bit cursors and a
// four-byte interlocked size. Current 0x00aeb00c/0x00aeb1a4 confirm the
// low-water crash, lock cadence, cursor order and unsigned ring reduction.
template <typename TObject, std::size_t MaximumCount>
class CMwmrQueue {
    // Portable guard access. The spin-lock methods keep their original visibility.
    friend struct scoped_lock_detail::OwnerAccess;

public:
    CMwmrQueue() {
        static_assert(
            std::is_trivially_copyable_v<TObject>, "the current fixed CMwmrQueue pool is zero-initialized storage"
        );
        std::memset(static_cast<void*>(StaticPool), 0, sizeof(StaticPool));
        for (std::size_t index = 0; index < MaximumCount; ++index) {
            Pool[index] = &StaticPool[index];
        }
        SpinLock = 0;
        First = 0;
        Last = static_cast<std::int64_t>(MaximumCount);
        Size = static_cast<std::int32_t>(MaximumCount);
    }

    TObject* Pop() {
        TObject* object;
        {
            L2_GUARD(*this);
            if (runtime::SubtractInt64Wrapping(Last, First) < 0x10) {
                thread_safe_detail::CrashMwmrQueueOverflow();
            }

            First = runtime::AddInt64Wrapping(First, 1);
            const std::size_t index = static_cast<std::size_t>(runtime::Win64UnsignedRemainder(
                static_cast<std::uint64_t>(First), static_cast<std::uint64_t>(MaximumCount)
            ));
            object = const_cast<TObject*>(Pool[index]);
        }
        runtime::raw_atomic::DecrementI32(&Size);
        return object;
    }

    void Push(TObject* object) {
        {
            L2_GUARD(*this);
            Last = runtime::AddInt64Wrapping(Last, 1);
            const std::size_t index = static_cast<std::size_t>(runtime::Win64UnsignedRemainder(
                static_cast<std::uint64_t>(Last), static_cast<std::uint64_t>(MaximumCount)
            ));
            Pool[index] = object;
        }
        runtime::raw_atomic::IncrementI32(&Size);
    }

private:
    void Lock() {
        int retry = 0;
        while (runtime::raw_atomic::CompareExchangeI32(&SpinLock, 1, 0) != 0) {
            ++retry;
            if (retry % 0x14 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void Unlock() { runtime::raw_atomic::StoreI32Relaxed(&SpinLock, 0); }

    TObject StaticPool[MaximumCount];
    volatile TObject* Pool[MaximumCount];
    volatile std::int32_t SpinLock;
    volatile std::int64_t First;
    volatile std::int64_t Last;
    volatile std::int32_t Size;
};

// Exact shared/ThreadSafeLibrary.h multi-writer/single-reader pool used by
// CIOBufferEx. Current L2NPC.exe 0x004553d4 and current Cached.exe
// 0x004543bc confirm the complete 0x28-byte layout and the 0x5038-byte
// CIOBuffer specialization. L2NPC Pop at 0x0041fa28, Cached Pop at
// 0x00413334 and the inlined Push bodies confirm the counters,
// reserved-low-water wait and ring indexing.
template <typename TObject>
class CMwsrQueue {
public:
    explicit CMwsrQueue(int nMaxCount) : MaxCount(nMaxCount) {
        // The three current CIOBuffer specializations (0x00455204,
        // 0x004552ec and 0x004553d4) use the CRT allocator directly. In
        // particular, there is no portable-only MaxCount <= 0 rejection:
        // the signed count is widened and multiplied with x64 wrapping
        // before malloc observes the resulting size_t.
        const std::size_t objectBytes = static_cast<std::size_t>(
            static_cast<std::uint64_t>(static_cast<std::int64_t>(MaxCount)) * static_cast<std::uint64_t>(sizeof(TObject))
        );
        StaticPool = static_cast<TObject*>(std::malloc(objectBytes));
        if (!StaticPool) {
            thread_safe_detail::ReportMwsrQueueLowMemory("StaticPool");
            return;
        }

        const std::size_t pointerBytes =
            static_cast<std::size_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(MaxCount)) * sizeof(TObject*));
        Pool = static_cast<TObject**>(std::malloc(pointerBytes));
        if (!Pool) {
            thread_safe_detail::ReportMwsrQueueLowMemory("Pool");
            return;
        }

        thread_safe_detail::RecordMwsrQueueAllocation(objectBytes + pointerBytes);
        std::memset(static_cast<void*>(StaticPool), 0, objectBytes);
        for (int index = 0; index < MaxCount; ++index) {
            // The target only zeroes raw CIOBuffer storage. Placement
            // construction starts the C++17 host lifetime of its atomic
            // fields. The recovered default constructor has no nonzero or
            // externally visible effect, so the resulting bytes stay the
            // same as the target memset.
            auto* const storage =
                reinterpret_cast<std::byte*>(StaticPool) + static_cast<std::size_t>(index) * sizeof(TObject);
            Pool[index] = ::new (static_cast<void*>(storage)) TObject;
        }
        First.store(0, std::memory_order_relaxed);
        Size.store(MaxCount, std::memory_order_relaxed);
        Last.store(MaxCount, std::memory_order_relaxed);
    }

    TObject* Pop() {
        Size.fetch_sub(1, std::memory_order_seq_cst);
        for (int retry = 0; Size.load(std::memory_order_relaxed) < 0x10 && retry < 0x14; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const std::int64_t first = First.fetch_add(1, std::memory_order_relaxed) + 1;
        return Pool[static_cast<std::size_t>(runtime::Win64UnsignedRemainder(
            static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(static_cast<std::int64_t>(MaxCount))
        ))];
    }

    void Push(TObject* object) {
        const std::int64_t last = Last.fetch_add(1, std::memory_order_seq_cst) + 1;
        if (Pool && MaxCount > 0) {
            Pool[static_cast<std::size_t>(last % static_cast<std::int64_t>(MaxCount))] = object;
        }
        Size.fetch_add(1, std::memory_order_seq_cst);
    }

    int InstantSize() const { return Size.load(std::memory_order_relaxed); }

    int GetMaxSize() const { return MaxCount; }

private:
    TObject* StaticPool; // +0x00
    TObject** Pool; // +0x08
    std::atomic<std::int64_t> First; // +0x10
    std::atomic<std::int64_t> Last; // +0x18
    std::atomic<std::int32_t> Size; // +0x20
    std::int32_t MaxCount; // +0x24
};


// Exact current-L2Server CNewMwsrQueue owner. Unlike the CMwsrQueue used by
// current Cached/L2NPC, this generation keeps one spin-serialized producer
// position and one single-reader pop position. Current L2Server constructors
// at 0x0042bb00/0x00775d18/0x007cb044, Push at
// 0x00419c54/0x004a92f0/0x0043f838, and the inlined Pop/size bodies close the
// complete 0x28-byte layout and ring contract.
template <typename TObject>
class CNewMwsrQueue {
    // Portable guard access. The spin-lock methods keep their original visibility.
    friend struct scoped_lock_detail::OwnerAccess;

public:
    explicit CNewMwsrQueue(int maximumCount) {
        m_nMaxCount = maximumCount;
        SpinLock.store(0, std::memory_order_relaxed);
        PushPosition.store(0, std::memory_order_relaxed);
        PopPosition.store(0, std::memory_order_relaxed);

        const std::size_t objectBytes = WrappedAllocationSize(maximumCount, sizeof(TObject));
        m_QueueItems = static_cast<TObject*>(std::malloc(objectBytes));
        if (m_QueueItems == nullptr) {
            thread_safe_detail::ReportNewMwsrQueueNull(L"m_QueueItems");
            return;
        }

        const std::size_t pointerBytes = WrappedAllocationSize(maximumCount, sizeof(TObject*));
        m_pQueueItems = static_cast<TObject**>(std::malloc(pointerBytes));
        if (m_pQueueItems == nullptr) {
            thread_safe_detail::ReportNewMwsrQueueNull(L"m_pQueueItems");
            return;
        }

        std::memset(static_cast<void*>(m_QueueItems), 0, objectBytes);
        std::memset(static_cast<void*>(m_pQueueItems), 0, pointerBytes);
        thread_safe_detail::RecordMwsrQueueAllocation(objectBytes + pointerBytes);

        for (int index = 0; index < m_nMaxCount; ++index) {
            auto* const storage =
                reinterpret_cast<std::byte*>(m_QueueItems) + static_cast<std::size_t>(index) * sizeof(TObject);
            TObject* const object = ::new (static_cast<void*>(storage)) TObject;
            m_pQueueItems[index] = object;
        }
    }

    TObject* Pop() {
        // Recycled slots are published before PushPosition advances in the
        // Windows target. Its x86-64 memory model makes that ordinary store
        // visible before a later consumer reads the slot. Preserve the same
        // edge on weaker portable hosts.
        PushPosition.load(std::memory_order_acquire);
        const std::int64_t popPosition = PopPosition.load(std::memory_order_relaxed);
        const std::int64_t slot = popPosition / static_cast<std::int64_t>(m_nMaxCount);
        const std::int64_t index = popPosition - slot * static_cast<std::int64_t>(m_nMaxCount);
        TObject* const object = m_pQueueItems[index];
        m_pQueueItems[index] = nullptr;
        PopPosition.store(IncrementWrapping(popPosition), std::memory_order_release);
        return object;
    }

    void Push(TObject* object) {
        {
            L2_GUARD(*this);
            const std::int64_t pushPosition = PushPosition.load(std::memory_order_relaxed);
            const std::int64_t popPosition = PopPosition.load(std::memory_order_acquire);
            const std::int64_t index = pushPosition % static_cast<std::int64_t>(m_nMaxCount);
            if (pushPosition < popPosition && m_pQueueItems[index] == nullptr) {
                m_pQueueItems[index] = object;
                PushPosition.store(IncrementWrapping(pushPosition), std::memory_order_release);
            } else {
                thread_safe_detail::CrashNewMwsrQueueInvariant(9);
            }
        }
    }

    void UnLock() {
        if (SpinLock.load(std::memory_order_relaxed) == 0) {
            thread_safe_detail::CrashNewMwsrQueueInvariant(6);
            return;
        }
        SpinLock.store(0, std::memory_order_release);
    }

    int InstantSize() const {
        const std::uint32_t freeBits = static_cast<std::uint32_t>(PushPosition.load(std::memory_order_relaxed)) +
            static_cast<std::uint32_t>(m_nMaxCount) -
            static_cast<std::uint32_t>(PopPosition.load(std::memory_order_relaxed));
        std::int32_t freeCount = 0;
        std::memcpy(&freeCount, &freeBits, sizeof(freeCount));
        return freeCount;
    }

    int GetMaxSize() const { return m_nMaxCount; }

private:
    static std::size_t WrappedAllocationSize(int count, std::size_t elementSize) {
        const std::uint32_t byteBits = static_cast<std::uint32_t>(count) * static_cast<std::uint32_t>(elementSize);
        std::int32_t signedBytes = 0;
        std::memcpy(&signedBytes, &byteBits, sizeof(signedBytes));
        return static_cast<std::size_t>(static_cast<std::int64_t>(signedBytes));
    }

    static std::int64_t IncrementWrapping(std::int64_t value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        ++bits;
        std::int64_t result = 0;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    void Lock() {
        for (;;) {
            while (SpinLock.load(std::memory_order_acquire) != 0) {
            }
            std::int32_t expected = 0;
            if (SpinLock.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    std::atomic<std::int32_t> SpinLock; // +0x00
    std::int32_t m_nMaxCount; // +0x04
    TObject* m_QueueItems; // +0x08
    TObject** m_pQueueItems; // +0x10
    std::atomic<std::int64_t> PushPosition; // +0x18
    std::atomic<std::int64_t> PopPosition; // +0x20
};


// Shared-memory-safe object handle recovered from the same header. Unlike
// CSPointer it owns no process-local pointer and therefore has a stable
// eight-byte representation in CSharedCreatureData.
template <typename TObject>
struct CSafePointer2 {
    int id;
    int index;

    CSafePointer2() : id(-1), index(-1) {}
    explicit CSafePointer2(const TObject* object);
    CSafePointer2(const CSafePointer2& other);

    CSafePointer2& operator=(const TObject* object);
    CSafePointer2& operator=(const CSafePointer2& other);
    CSPointer<TObject> FindObjectSP();
};

template <typename TObject>
CSafePointer2<TObject>::CSafePointer2(const TObject* object) {
    static_assert(
        std::is_same_v<TObject, CCreature> || std::is_same_v<TObject, CStaticObject>,
        "only the current CCreature/CStaticObject pointer constructors "
        "are recovered"
    );
    L2_GUARD_FUNCTION();
    id = static_cast<int>(object->m_nId);
    index = object->m_data->index;
}

template <typename TObject>
CSafePointer2<TObject>::CSafePointer2(const CSafePointer2& other) {
    static_assert(
        std::is_same_v<TObject, CCreature> || std::is_same_v<TObject, CStaticObject>,
        "only the current CCreature/CStaticObject copy constructors "
        "are recovered"
    );
    L2_GUARD_FUNCTION();
    id = other.id;
    index = other.index;
}

template <typename TObject>
CSafePointer2<TObject>& CSafePointer2<TObject>::operator=(const CSafePointer2& other) {
    static_assert(
        std::is_same_v<TObject, CCreature> || std::is_same_v<TObject, CStaticObject>,
        "only the current CCreature/CStaticObject copy assignments "
        "are recovered"
    );
    L2_GUARD_FUNCTION();
    id = other.id;
    index = other.index;
    return *this;
}

template <typename TObject>
CSPointer<TObject> CSafePointer2<TObject>::FindObjectSP() {
    L2_GUARD_FUNCTION();
    CSPointer<TObject> result;
    if (id > 0) {
        result = TObject::FindObjectSP(static_cast<std::uint32_t>(id));
    }
    return result;
}


struct NullType {};

template <typename THead, typename TTail>
struct Typelist {
    using Head = THead;
    using Tail = TTail;
};

namespace thread_safe_detail {

template <typename TList, typename TCandidate>
struct TypeListIndex;

template <typename TCandidate>
struct TypeListIndex<NullType, TCandidate> {
    static constexpr int value = -1;
};

template <typename TTail, typename TCandidate>
struct TypeListIndex<Typelist<TCandidate, TTail>, TCandidate> {
    static constexpr int value = 0;
};

template <typename THead, typename TTail, typename TCandidate>
struct TypeListIndex<Typelist<THead, TTail>, TCandidate> {
private:
    static constexpr int kTail = TypeListIndex<TTail, TCandidate>::value;

public:
    static constexpr int value = kTail < 0 ? -1 : kTail + 1;
};

} // namespace thread_safe_detail

template <typename TList, typename TCandidate>
struct IndexOf {
    static constexpr int value = thread_safe_detail::TypeListIndex<TList, TCandidate>::value;
    Loki::CompileTimeError<(value >= 0) ? 1 : 0> ERROR_UNAVAILABLE_TYPE;
};

namespace thread_safe_detail {

#if defined(L2_BUILD_L2SERVER)
void FailInvalidPipeThread(int threadId, const wchar_t* functionName);
void FailPipeOverflow(const wchar_t* functionName);
void FailPipeReadOverflow(const wchar_t* functionName);
#else
void FailInvalidPipeThread(int threadId);
void FailPipeOverflow();
void FailPipeReadOverflow();
#endif
void ReportPipeIndexOrder(std::int32_t firstIndex, std::int32_t lastIndex);

#if defined(L2_BUILD_L2SERVER)
template <typename TValue, std::size_t MaximumRecords, std::size_t MaximumThreads>
struct PipeNames;

#define L2_DEFINE_PIPE_NAMES(ValueType, ValueText, Records, RecordsText, Threads, ThreadsText)                                                                       \
    template <>                                                                                                                                                      \
    struct PipeNames<ValueType, Records, Threads> {                                                                                                                  \
        static constexpr const wchar_t* ThreadWrite() noexcept {                                                                                                     \
            return L"void __cdecl "                                                                                                                                  \
                   L"CTLPipeBase"                                                                                                                                    \
                   L"<" ValueText L"," RecordsText L"," ThreadsText L">::CThreadPipe::Write(const " ValueText L" &)";                                                \
        }                                                                                                                                                            \
        static constexpr const wchar_t* ThreadRead() noexcept {                                                                                                      \
            return L"void __cdecl CTLPipeBase<" ValueText L"," RecordsText L"," ThreadsText L">::CThreadPipe::Read(class "                                           \
                   L"CTLPipeBase<" ValueText L"," RecordsText L"," ThreadsText L">::CReadCallback *)";                                                               \
        }                                                                                                                                                            \
        static constexpr const wchar_t* Write() noexcept {                                                                                                           \
            return L"void __cdecl "                                                                                                                                  \
                   L"CTLPipeBase<" ValueText L"," RecordsText L"," ThreadsText L">::Write(int,const " ValueText L" &)";                                              \
        }                                                                                                                                                            \
        static constexpr const wchar_t* Read() noexcept {                                                                                                            \
            return L"void __cdecl CTLPipeBase<" ValueText L"," RecordsText L"," ThreadsText L">::Read(class "                                                        \
                                                                                            L"CTLPipeBase"                                                           \
                                                                                            L"<" ValueText L"," RecordsText L"," ThreadsText L">::CReadCallback *)"; \
        }                                                                                                                                                            \
    }

L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 1020000, L"1020000", 16, L"16");
L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 120000, L"120000", 16, L"16");
L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 1000, L"1000", 16, L"16");
L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 900000, L"900000", 16, L"16");
L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 10001, L"10001", 16, L"16");
L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 10003, L"10003", 16, L"16");
L2_DEFINE_PIPE_NAMES(std::uint32_t, L"unsigned int", 10002, L"10002", 16, L"16");

L2_DEFINE_PIPE_NAMES(CTL_WorldMapNode2, L"struct CTL_WorldMapNode2", 600000, L"600000", 16, L"16");
L2_DEFINE_PIPE_NAMES(CTL_WorldMapNode2, L"struct CTL_WorldMapNode2", 50000, L"50000", 16, L"16");
L2_DEFINE_PIPE_NAMES(CTL_WorldMapNode2, L"struct CTL_WorldMapNode2", 112500, L"112500", 16, L"16");
L2_DEFINE_PIPE_NAMES(CTL_WorldMapNode2, L"struct CTL_WorldMapNode2", 3000, L"3000", 16, L"16");
L2_DEFINE_PIPE_NAMES(CTL_WorldMapNode2, L"struct CTL_WorldMapNode2", 20002, L"20002", 16, L"16");

#undef L2_DEFINE_PIPE_NAMES
#endif

void ReportLookupOutsideIoThread(int threadId);
// pass object identity only to the diagnostic adapter so
// known service NPCs can be identified without accepting invalid SmartIDs.
// void ReportSmartIdRegisterInvalidClass(
//     std::uint32_t objectClass,
//     std::uint32_t smartId,
//     int sourceLine)
void ReportSmartIdRegisterInvalidClass(
    std::uint32_t objectClass, std::uint32_t smartId, int sourceLine, const void* object = nullptr
);
void ReportSmartIdRegisterInvalidIndex(std::uint32_t index, std::uint32_t smartId);
void ReportSmartIdDeleteInvalidIndex(std::uint32_t index, std::uint32_t smartId);
void ReportSmartIdReadInvalidIndex(std::uint32_t index);
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
);
void ReportExecutionSerializerAbnormalRunner(
    int threadId,
    int runningThreadId,
    const char16_t* ownerName,
    const char* ownerTypeName,
    std::size_t queueSize,
    int x,
    int y,
    int z
);
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
);

void ReportExecutionSerializerFastOverflow(int stage, int threadId, const wchar_t* executableName, int queueSize);

} // namespace thread_safe_detail

// Recovered from shared/ThreadSafeLibrary.h. Current L2NPC.exe
// 0x0041f044/0x0041f0e8 and the symbol-complete NASC64_2015 donor agree on
// every field and on the synchronous single-runner ring protocol.
//
// MSVC's single-inheritance MemoryObject member-function pointer occupies
// eight bytes. Other 64-bit C++ ABIs may use a wider representation, so the
// portable build stores an equally narrow static trampoline with the same
// (owner, argument) call contract.
class CExecutionSerializerFast {
public:
    using Executable = void (*)(MemoryObject* owner, void* argument);

    explicit CExecutionSerializerFast(int maxQueueSize);
    ~CExecutionSerializerFast();

    CExecutionSerializerFast(const CExecutionSerializerFast&) = delete;
    CExecutionSerializerFast& operator=(const CExecutionSerializerFast&) = delete;

    int AddExecutable(MemoryObject* owner, Executable executable, void* argument, const wchar_t* executableName);

private:
    // Exact target offsets +0x00..+0x2f.
    std::atomic<std::int64_t> First{0};
    std::atomic<std::int64_t> Last{0};
    std::atomic<std::int32_t> Count{0};
    std::atomic<std::int32_t> RunningThreadID{-1};
    std::atomic<std::int32_t> _RunThreadCount{0};
    std::int32_t MaxQueueSize = 0;
    std::atomic<void*>* Args = nullptr;
    std::atomic<Executable>* Funcs = nullptr;
};


// Exact fixed argument storage used by CExecutionSerializer in the target.
// The first queued pad starts at serializer +0x1c and is therefore not
// guaranteed to be naturally aligned for every value it contains. Argument
// implementations must use memcpy instead of typed unaligned loads/stores.
struct CExecutionArgumentPad {
    char Buffer[80];
};


// The target vtable contains one pure CopyToPad slot and no virtual
// destructor. Concrete async-argument objects are compiler-generated stack
// values and are destroyed through their concrete type.
class CExecutionArgument {
public:
    virtual void CopyToPad(CExecutionArgumentPad* destination) const = 0;

protected:
    ~CExecutionArgument() = default;
};

namespace thread_safe_detail {

// These helpers express only the byte lanes proved for one concrete async
// carrier. They cannot clear a pad or copy a complete object.
// Pointer-to-member input keeps the physical displacement owned by the real
// class declaration instead of publishing a raw-offset access interface.
#if defined(_MSC_VER)
#define L2_ASYNC_ALWAYS_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define L2_ASYNC_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define L2_ASYNC_ALWAYS_INLINE inline
#endif

template <typename TArgument>
L2_ASYNC_ALWAYS_INLINE void CopyExecutionArgumentIdentity(
    CExecutionArgumentPad& destination, const TArgument& source
) noexcept {
    static_assert(
        std::is_base_of_v<CExecutionArgument, TArgument>, "async carrier must retain CExecutionArgument as its base"
    );
    static_assert(
        std::is_trivially_destructible_v<TArgument>, "fieldwise async copy cannot publish an owned-lifetime carrier"
    );
    static_assert(
        sizeof(TArgument) <= sizeof(CExecutionArgumentPad), "async carrier exceeds the current 80-byte argument pad"
    );
    std::memcpy(destination.Buffer, static_cast<const void*>(&source), sizeof(void*));
}

template <typename TArgument, typename TMember>
L2_ASYNC_ALWAYS_INLINE void CopyExecutionArgumentMemberBytes(
    CExecutionArgumentPad& destination, const TArgument& source, TMember TArgument::* member
) noexcept {
    static_assert(
        std::is_trivially_destructible_v<TMember>, "fieldwise async copy requires an independently audited value lane"
    );
    const char* const sourceBytes = reinterpret_cast<const char*>(&source);
    const char* const memberBytes = reinterpret_cast<const char*>(&(source.*member));
    std::memcpy(destination.Buffer + (memberBytes - sourceBytes), memberBytes, sizeof(TMember));
}

template <typename TArgument, typename... TMember>
L2_ASYNC_ALWAYS_INLINE void CopyExecutionArgumentFields(
    CExecutionArgumentPad& destination, const TArgument& source, TMember TArgument::*... members
) noexcept {
    CopyExecutionArgumentIdentity(destination, source);
    (CopyExecutionArgumentMemberBytes(destination, source, members), ...);
}

#undef L2_ASYNC_ALWAYS_INLINE

} // namespace thread_safe_detail

// TotalChecker PDB/BSC evidence proves a historical async macro family but
// does not keep its complete body. The pinned current L2Server image does
// keep two exact guard strings spelling
// ASYNCHRONOUS_SERIALIZE_IMPL1(Owner, Method, Type, argument). Keep the
// current-proved diagnostic grammar separate from argument decoding: the
// latter remains at each audited machine-code read boundary instead of being
// moved to function entry by a source-shape refactor.
#define L2_ASYNC_GUARD_WIDEN_IMPL(Value) L##Value
#define L2_ASYNC_GUARD_WIDEN(Value) L2_ASYNC_GUARD_WIDEN_IMPL(Value)
#define L2_ASYNC_IMPL1_GUARD(Owner, Method, T1, A1)                                                                         \
    static const wchar_t __FUNC_NAME__[] = L"ASYNCHRONOUS_SERIALIZE_IMPL1(" L2_ASYNC_GUARD_WIDEN(                           \
        #Owner                                                                                                              \
    ) L", " L2_ASYNC_GUARD_WIDEN(#Method) L", " L2_ASYNC_GUARD_WIDEN(#T1) L", " L2_ASYNC_GUARD_WIDEN(#A1) L")";             \
    L2_GUARD_PROFILE(__FUNC_NAME__);                                                                                        \
    GuardHelper __stack_var__(__FUNC_NAME__, CThreadManager::GetThreadID())

// These honestly named declaration/copy macros keep explicit constructor
// surfaces and separate class declaration from the out-of-line key function.
// Only a current-image-audited cohort may use a definition macro.
#define L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)                                                                                \
    static_assert(sizeof(Name) <= sizeof(CExecutionArgumentPad), #Name " exceeds the current 80-byte argument pad")

#define L2_ASYNC_DECLARE_EMPTY_1_SLOT(Name)                                                                                 \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name();                                                                                                             \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_EMPTY_2_SLOT(Name)                                                                                 \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name();                                                                                                             \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        virtual void CopyFromPad(CExecutionArgumentPad* source) const;                                                      \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

// These variants encode the distinct constructor/finality surfaces proved
// for current carriers. They are separate instead of
// normalizing implicit, defaulted and out-of-line constructors.
#define L2_ASYNC_DECLARE_IMPLICIT_EMPTY_2_SLOT(Name)                                                                        \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        virtual void CopyFromPad(CExecutionArgumentPad* source) const;                                                      \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_DEFAULTED_EMPTY_2_SLOT(Name)                                                                       \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name() = default;                                                                                                   \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        virtual void CopyFromPad(CExecutionArgumentPad* source) const;                                                      \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(Name)                                                                           \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        virtual void CopyFromPad(CExecutionArgumentPad* source) const;                                                      \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FIELDS_1(Name, T1)                                                                                 \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(Name* other);                                                                                                  \
        Name();                                                                                                             \
        explicit Name(T1 a1);                                                                                               \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FIELDS_2(Name, T1, T2)                                                                             \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(Name* other);                                                                                                  \
        Name();                                                                                                             \
        Name(T1 a1, T2 a2);                                                                                                 \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FIELDS_3(Name, T1, T2, T3)                                                                         \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(Name* other);                                                                                                  \
        Name();                                                                                                             \
        Name(T1 a1, T2 a2, T3 a3);                                                                                          \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FIELDS_4(Name, T1, T2, T3, T4)                                                                     \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(Name* other);                                                                                                  \
        Name();                                                                                                             \
        Name(T1 a1, T2 a2, T3 a3, T4 a4);                                                                                   \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
        T4 __a4;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FIELDS_5(Name, T1, T2, T3, T4, T5)                                                                 \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(Name* other);                                                                                                  \
        Name();                                                                                                             \
        Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5);                                                                            \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
        T4 __a4;                                                                                                            \
        T5 __a5;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FIELDS_6(Name, T1, T2, T3, T4, T5, T6)                                                             \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(Name* other);                                                                                                  \
        Name();                                                                                                             \
        Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6);                                                                     \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
        T4 __a4;                                                                                                            \
        T5 __a5;                                                                                                            \
        T6 __a6;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_IMPLICIT_FIELDS_1(Name, T1)                                                                        \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_IMPLICIT_FIELDS_3(Name, T1, T2, T3)                                                                \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_OUT_OF_LINE_CONST_REF_FIELDS_1(Name, T1, P1)                                                       \
    class Name : public CExecutionArgument {                                                                                \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name();                                                                                                             \
        explicit Name(const T1& P1);                                                                                        \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_OUT_OF_LINE_FIELDS_1(Name, FieldT1, ParamT1, P1)                                             \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name();                                                                                                             \
        explicit Name(ParamT1 P1);                                                                                          \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        FieldT1 __a1;                                                                                                       \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_OUT_OF_LINE_FIELDS_2(Name, T1, P1, T2, P2)                                                   \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name();                                                                                                             \
        Name(T1 P1, T2 P2);                                                                                                 \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(Name, T1)                                                                    \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        explicit Name(T1 a1) : __a1(a1) {}                                                                                  \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_2(Name, T1, T2)                                                                \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(T1 a1, T2 a2) : __a1(a1), __a2(a2) {}                                                                          \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_3(Name, T1, T2, T3)                                                            \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(T1 a1, T2 a2, T3 a3) : __a1(a1), __a2(a2), __a3(a3) {}                                                         \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_4(Name, T1, T2, T3, T4)                                                        \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(T1 a1, T2 a2, T3 a3, T4 a4) : __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}                                        \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
        T4 __a4;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_5(Name, T1, T2, T3, T4, T5)                                                    \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) : __a1(a1), __a2(a2), __a3(a3), __a4(a4), __a5(a5) {}                       \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
        T4 __a4;                                                                                                            \
        T5 __a5;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_6(Name, T1, T2, T3, T4, T5, T6)                                                \
    class Name final : public CExecutionArgument {                                                                          \
    public:                                                                                                                 \
        using thistype = Name;                                                                                              \
        Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) : __a1(a1), __a2(a2), __a3(a3), __a4(a4), __a5(a5), __a6(a6) {}      \
        void CopyToPad(CExecutionArgumentPad* destination) const override;                                                  \
        T1 __a1;                                                                                                            \
        T2 __a2;                                                                                                            \
        T3 __a3;                                                                                                            \
        T4 __a4;                                                                                                            \
        T5 __a5;                                                                                                            \
        T6 __a6;                                                                                                            \
    };                                                                                                                      \
    L2_ASYNC_DETAIL_CAPACITY_GUARD(Name)

#define L2_ASYNC_DEFINE_EMPTY_1_SLOT_COPY(Owner, Name)                                                                      \
    void Owner::Name::CopyToPad(CExecutionArgumentPad*) const {}

#define L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(Owner, Name)                                                                      \
    void Owner::Name::CopyToPad(CExecutionArgumentPad*) const {}                                                            \
    void Owner::Name::CopyFromPad(CExecutionArgumentPad*) const {}

#define L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(Owner, Name, ...)                                                             \
    void Owner::Name::CopyToPad(CExecutionArgumentPad* destination) const {                                                 \
        if (destination == nullptr) {                                                                                       \
            return;                                                                                                         \
        }                                                                                                                   \
        ::thread_safe_detail::CopyExecutionArgumentFields(*destination, *this, __VA_ARGS__);                                \
    }

#define L2_ASYNC_DEFINE_FIELDWISE_COPY_1(Owner, Name) L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(Owner, Name, &Owner::Name::__a1)

#define L2_ASYNC_DEFINE_FIELDWISE_COPY_2(Owner, Name)                                                                       \
    L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(Owner, Name, &Owner::Name::__a1, &Owner::Name::__a2)

#define L2_ASYNC_DEFINE_FIELDWISE_COPY_3(Owner, Name)                                                                       \
    L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(Owner, Name, &Owner::Name::__a1, &Owner::Name::__a2, &Owner::Name::__a3)

#define L2_ASYNC_DEFINE_FIELDWISE_COPY_4(Owner, Name)                                                                       \
    L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(                                                                                  \
        Owner, Name, &Owner::Name::__a1, &Owner::Name::__a2, &Owner::Name::__a3, &Owner::Name::__a4                         \
    )

#define L2_ASYNC_DEFINE_FIELDWISE_COPY_5(Owner, Name)                                                                       \
    L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(                                                                                  \
        Owner, Name, &Owner::Name::__a1, &Owner::Name::__a2, &Owner::Name::__a3, &Owner::Name::__a4, &Owner::Name::__a5     \
    )

#define L2_ASYNC_DEFINE_FIELDWISE_COPY_6(Owner, Name)                                                                       \
    L2_ASYNC_DETAIL_DEFINE_FIELDWISE_COPY(                                                                                  \
        Owner, Name, &Owner::Name::__a1, &Owner::Name::__a2, &Owner::Name::__a3, &Owner::Name::__a4, &Owner::Name::__a5,    \
        &Owner::Name::__a6                                                                                                  \
    )

#if defined(_MSC_VER)
#define L2_THREAD_SAFE_ALWAYS_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define L2_THREAD_SAFE_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define L2_THREAD_SAFE_ALWAYS_INLINE inline
#endif

template <typename TCreature>
class CCreaturePosLogger {
public:
    static L2_THREAD_SAFE_ALWAYS_INLINE FVector LogPosition(MemoryObject*) { return FVector{}; }
};

#undef L2_THREAD_SAFE_ALWAYS_INLINE

#if defined(L2_BUILD_L2SERVER)
template <>
FVector CCreaturePosLogger<CCreature>::LogPosition(MemoryObject* object);
template <>
FVector CCreaturePosLogger<CCreatureController>::LogPosition(MemoryObject* object);
template <>
FVector CCreaturePosLogger<CFishingController>::LogPosition(MemoryObject* object);
#endif

namespace thread_safe_detail {

// This one-byte field occupies the target's ERROR_NotPowerOf2 slot at +0xb20
// and leaves the following owner pointer naturally aligned at +0xb28.
struct ExecutionSerializerPowerOfTwoCheck {};

#if defined(L2_BUILD_L2SERVER)
inline constexpr std::size_t kExecutionSerializerProducerReserve = 16;
#else
inline constexpr std::size_t kExecutionSerializerProducerReserve = 8;
#endif

} // namespace thread_safe_detail

// MSVC x64 stores this member-function pointer in eight bytes.
// Other platforms may need a wider pointer. Use a static wrapper instead
// so each queue slot still holds one owner and an 80-byte argument buffer.
// No owner or event name is stored in a queue slot.
template <typename TOwner, std::size_t QueueSize, typename TInvocationOwner = TOwner>
class CExecutionSerializer {
    static_assert(
        QueueSize > thread_safe_detail::kExecutionSerializerProducerReserve,
        "CExecutionSerializer queue must exceed its product reserve"
    );
    static_assert((QueueSize & (QueueSize - 1)) == 0, "CExecutionSerializer queue size must be a power of two");

public:
    using Executable = void (*)(TInvocationOwner* owner, CExecutionArgumentPad* argument);

    CExecutionSerializer() :
        First(0),
        Last(0),
        Count(0),
        RunningThreadID(-1),
        _RunThreadCount(0),
        m_pOwnerName(u"NoName"),
        m_pMasterName(u"NoName"),
        m_nRecursiveCount(0) {
        // The target initializes Funcs only. ArgPad bytes remain
        // unspecified until the producer's virtual CopyToPad writes a slot.
        for (auto& function : Funcs) {
            function.store(nullptr, std::memory_order_relaxed);
        }
    }

    CExecutionSerializer(const CExecutionSerializer&) = delete;
    CExecutionSerializer& operator=(const CExecutionSerializer&) = delete;

    void SetOwnerName(const char16_t* ownerName) { m_pOwnerName = ownerName; }

    const char16_t* GetOwnerName() const { return m_pOwnerName; }

    void SetMasterName(const char16_t* masterName) { m_pMasterName = masterName; }

    const char16_t* GetMasterName() const { return m_pMasterName; }

    void AddExecutable(
        TInvocationOwner* owner, Executable function, CExecutionArgument* argument, const wchar_t* executableName
    ) {
        L2_GUARD_FUNCTION();
        const int threadId = CThreadManager::GetThreadID();

        // A nested call from the active logical TLS worker executes directly
        // on a stack pad. It must not reserve or modify a ring slot.
        if (RunningThreadID.load(std::memory_order_acquire) == threadId) {
            CExecutionArgumentPad localPad;
            argument->CopyToPad(&localPad);
            ++m_nRecursiveCount;
            if (m_nRecursiveCount > 4) {
                const FVector position = CCreaturePosLogger<TOwner>::LogPosition(static_cast<MemoryObject*>(owner));
                const int z = runtime::Win64CvttDoubleToInt32(position.z);
                const int y = runtime::Win64CvttDoubleToInt32(position.y);
                const int x = runtime::Win64CvttDoubleToInt32(position.x);
                thread_safe_detail::ReportExecutionSerializerRecursive(
                    threadId, m_pOwnerName, executableName, typeid(TOwner).name(), QueueSize, m_nRecursiveCount, x, y, z
                );
            }
            function(owner, &localPad);
            --m_nRecursiveCount;
            return;
        }

        // Producers which passed this non-CAS precheck together consume a
        // product-fixed reserve: 16 slots in current L2Server, eight in
        // current L2NPC and the unchanged non-L2Server portable products.
        if (Count.load(std::memory_order_acquire) >=
            static_cast<std::int32_t>(QueueSize - thread_safe_detail::kExecutionSerializerProducerReserve)) {
            const FVector position = CCreaturePosLogger<TOwner>::LogPosition(static_cast<MemoryObject*>(owner));
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            thread_safe_detail::ReportExecutionSerializerOverflow(
                1, threadId, RunningThreadID.load(std::memory_order_acquire), m_pMasterName, m_pOwnerName, executableName,
                typeid(TOwner).name(), QueueSize, x, y, z
            );
            return;
        }

        const std::int64_t reserved = Last.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::size_t producerSlot = static_cast<std::size_t>(static_cast<std::uint64_t>(reserved) & (QueueSize - 1));
        argument->CopyToPad(&ArgPad[producerSlot]);
        // Funcs is both the executable and the target's commit marker.
        Funcs[producerSlot].store(function, std::memory_order_release);

        const std::int32_t queued = Count.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (queued >= static_cast<std::int32_t>(QueueSize)) {
            // The item remains committed. An already-active runner can still
            // consume it after the diagnostic, exactly as in the executable.
            const FVector position = CCreaturePosLogger<TOwner>::LogPosition(static_cast<MemoryObject*>(owner));
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            thread_safe_detail::ReportExecutionSerializerOverflow(
                2, threadId, RunningThreadID.load(std::memory_order_acquire), nullptr, m_pOwnerName, executableName,
                typeid(TOwner).name(), QueueSize, x, y, z
            );
            return;
        }
        if (queued != 1) {
            return;
        }

        // The producer which changes Count from zero to one becomes the
        // synchronous drain runner. There is no background worker.
        do {
            const std::int32_t running = _RunThreadCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (running > 1) {
                const FVector position = CCreaturePosLogger<TOwner>::LogPosition(static_cast<MemoryObject*>(owner));
                const int z = runtime::Win64CvttDoubleToInt32(position.z);
                const int y = runtime::Win64CvttDoubleToInt32(position.y);
                const int x = runtime::Win64CvttDoubleToInt32(position.x);
                thread_safe_detail::ReportExecutionSerializerAbnormalRunner(
                    CThreadManager::GetThreadID(), RunningThreadID.load(std::memory_order_acquire), m_pOwnerName,
                    typeid(TOwner).name(), QueueSize, x, y, z
                );
            }

            RunningThreadID.store(CThreadManager::GetThreadID(), std::memory_order_release);
            const std::int64_t claimed = First.fetch_add(1, std::memory_order_acq_rel) + 1;
            const std::size_t consumerSlot = static_cast<std::size_t>(static_cast<std::uint64_t>(claimed) & (QueueSize - 1));

            int spinCount = 0;
            Executable queuedFunction = nullptr;
            while ((queuedFunction = Funcs[consumerSlot].load(std::memory_order_acquire)) == nullptr) {
                ++spinCount;
                if (spinCount > 1000) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    spinCount = 0;
                }
            }

            queuedFunction(owner, &ArgPad[consumerSlot]);
            Funcs[consumerSlot].store(nullptr, std::memory_order_release);
            RunningThreadID.store(-1, std::memory_order_release);
            _RunThreadCount.fetch_sub(1, std::memory_order_acq_rel);
        } while (Count.fetch_sub(1, std::memory_order_acq_rel) - 1 != 0);
    }

private:
    std::atomic<std::int64_t> First;
    std::atomic<std::int64_t> Last;
    std::atomic<std::int32_t> Count;
    std::atomic<std::int32_t> RunningThreadID;
    std::atomic<std::int32_t> _RunThreadCount;
    std::array<CExecutionArgumentPad, QueueSize> ArgPad;
    std::array<std::atomic<Executable>, QueueSize> Funcs;
    thread_safe_detail::ExecutionSerializerPowerOfTwoCheck ERROR_NotPowerOf2;
    // Windows wchar_t is one UTF-16 code unit. char16_t keeps the target
    // pointer contract on hosts where wchar_t is 32 bits.
    const char16_t* m_pOwnerName;
    const char16_t* m_pMasterName;

public:
    // Public in the recovered class and non-atomic: only the
    // active TLS runner enters the direct-recursion branch. It is declared
    // last so its physical offset remains the target's +0xb38.
    int m_nRecursiveCount;
};


// Recovered template from shared/ThreadSafeLibrary.h. The original embeds
// every fixed-size array inline. The portable representation keeps those
// arrays inline too: global CSmartIdManager storage therefore keeps the
// current PE's offsets as well as its counters, slot numbering and
// reserve/data/commit protocol.
template <typename TValue, std::size_t MaximumRecords, std::size_t MaximumThreads>
class CTLPipeBase {
    static_assert(MaximumRecords > 0);
    static_assert(MaximumThreads > 0);
    static_assert(std::is_copy_assignable<TValue>::value, "CTLPipeBase values must be copy assignable");
#if defined(L2_BUILD_L2SERVER)
    static_assert(
        std::is_trivially_copyable<TValue>::value, "the current L2Server pipe copies the complete value representation"
    );
#endif

public:
    class CReadCallback {
    public:
        virtual void OnRead(const TValue& value) = 0;
    };

    class CThreadPipe {
    public:
        CThreadPipe() { Initialize(); }

        void Initialize() {
            m_nFirstIndex.store(0, std::memory_order_relaxed);
            m_nLastIndex.store(0, std::memory_order_relaxed);
#if defined(L2_BUILD_L2SERVER)
            // Current L2Server clears the data block in one pass before the
            // separate commit-byte loop. In particular, this initializes the
            // three representation-padding bytes in CTL_WorldMapNode2.
            std::memset(m_data.data(), 0, sizeof(m_data));
            for (std::size_t index = 0; index < MaximumRecords; ++index) {
                m_bCommit[index].store(false, std::memory_order_relaxed);
            }
#else
            for (std::size_t index = 0; index < MaximumRecords; ++index) {
                m_data[index] = TValue{};
                m_bCommit[index].store(false, std::memory_order_relaxed);
            }
#endif
        }

        void Write(const TValue& value) {
#if defined(L2_BUILD_L2SERVER)
            const wchar_t* const functionName =
                thread_safe_detail::PipeNames<TValue, MaximumRecords, MaximumThreads>::ThreadWrite();
#endif
            L2_GUARD_FUNCTION();
            // InterlockedIncrement returns the incremented value in the PE.
            // The decompiler exposes the equivalent fetch-add old value.
            const std::int32_t oldLast = m_nLastIndex.fetch_add(1, std::memory_order_acq_rel);
            const std::uint32_t next = static_cast<std::uint32_t>(oldLast) + 1U;
            const std::uint32_t first = static_cast<std::uint32_t>(m_nFirstIndex.load(std::memory_order_acquire));
            {
                GuardHelper crashScope(L"Write_crash", CThreadManager::GetThreadID());
                if (next - first > MaximumRecords) {
#if defined(L2_BUILD_L2SERVER)
                    thread_safe_detail::FailPipeOverflow(functionName);
#else
                    thread_safe_detail::FailPipeOverflow();
#endif
                }
            }

            {
                GuardHelper normalScope(L"Write_normal", CThreadManager::GetThreadID());
                const std::size_t slot = next % MaximumRecords;
#if defined(L2_BUILD_L2SERVER)
                // The current Node2 specialization emits three qword moves,
                // publishing the source padding as well.
                std::memcpy(&m_data[slot], &value, sizeof(TValue));
#else
                m_data[slot] = value;
#endif
                m_bCommit[slot].store(true, std::memory_order_release);
            }
        }

        void Read(CReadCallback* callback) {
#if defined(L2_BUILD_L2SERVER)
            const wchar_t* const functionName =
                thread_safe_detail::PipeNames<TValue, MaximumRecords, MaximumThreads>::ThreadRead();
#endif
            L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
            const std::int32_t emptyFirst = m_nFirstIndex.load(std::memory_order_acquire);
            const std::int32_t emptyLast = m_nLastIndex.load(std::memory_order_acquire);
            if (emptyFirst == emptyLast) {
                return;
            }

            const std::int32_t orderFirst = m_nFirstIndex.load(std::memory_order_acquire);
            const std::int32_t orderLast = m_nLastIndex.load(std::memory_order_acquire);
            if (orderLast < orderFirst) {
                thread_safe_detail::ReportPipeIndexOrder(orderFirst, orderLast);
            }

            std::int32_t readCount = 0;
            while (m_nFirstIndex.load(std::memory_order_acquire) < m_nLastIndex.load(std::memory_order_acquire)) {
                readCount = runtime::AddInt32Wrapping(readCount, 1);
                {
                    GuardHelper crashScope(L"Read_crash", CThreadManager::GetThreadID());
                    if (readCount > static_cast<std::int32_t>(MaximumRecords)) {
#if defined(L2_BUILD_L2SERVER)
                        thread_safe_detail::FailPipeReadOverflow(functionName);
#else
                        thread_safe_detail::FailPipeReadOverflow();
#endif
                    }
                }

                const std::int32_t firstForSlot = m_nFirstIndex.load(std::memory_order_acquire);
                const std::size_t slot = (static_cast<std::uint32_t>(firstForSlot) + 1U) % MaximumRecords;
                if (!m_bCommit[slot].load(std::memory_order_acquire)) {
                    break;
                }

                callback->OnRead(m_data[slot]);
                m_bCommit[slot].store(false, std::memory_order_release);
                const std::int32_t firstAfterCallback = m_nFirstIndex.load(std::memory_order_acquire);
                m_nFirstIndex.store(runtime::AddInt32Wrapping(firstAfterCallback, 1), std::memory_order_release);
            }
#else
            std::int32_t first = m_nFirstIndex.load(std::memory_order_acquire);
            std::int32_t last = m_nLastIndex.load(std::memory_order_acquire);
            if (first == last) {
                return;
            }
            if (last < first) {
                thread_safe_detail::ReportPipeIndexOrder(first, last);
            }

            std::size_t reads = 0;
            while (first < last) {
                ++reads;
                {
                    GuardHelper crashScope(L"Read_crash", CThreadManager::GetThreadID());
                    if (reads > MaximumRecords) {
                        thread_safe_detail::FailPipeReadOverflow();
                    }
                }

                const std::size_t slot = (static_cast<std::uint32_t>(first) + 1U) % MaximumRecords;
                if (!m_bCommit[slot].load(std::memory_order_acquire)) {
                    break;
                }

                callback->OnRead(m_data[slot]);
                m_bCommit[slot].store(false, std::memory_order_release);
                first = static_cast<std::int32_t>(static_cast<std::uint32_t>(first) + 1U);
                m_nFirstIndex.store(first, std::memory_order_release);
                last = m_nLastIndex.load(std::memory_order_acquire);
            }
#endif
        }

#if !defined(L2_BUILD_L2SERVER)
    private:
#endif
#if defined(L2_BUILD_L2SERVER)
        std::atomic<std::int32_t> m_nFirstIndex;
        std::atomic<std::int32_t> m_nLastIndex;
        std::array<std::atomic<bool>, MaximumRecords> m_bCommit;
        std::array<TValue, MaximumRecords> m_data;
#else
        std::atomic<std::int32_t> m_nFirstIndex{0};
        std::atomic<std::int32_t> m_nLastIndex{0};
        std::array<std::atomic<bool>, MaximumRecords> m_bCommit{};
        std::array<TValue, MaximumRecords> m_data{};
#endif
    };

    CTLPipeBase() = default;

    void Write(int threadId, const TValue& value) {
#if defined(L2_BUILD_L2SERVER)
        const wchar_t* const functionName = thread_safe_detail::PipeNames<TValue, MaximumRecords, MaximumThreads>::Write();
#endif
        L2_GUARD_FUNCTION();
        const bool validThread = static_cast<std::uint32_t>(threadId) < static_cast<std::uint32_t>(MaximumThreads);
        {
            GuardHelper validationScope(L"write_what", CThreadManager::GetThreadID());
            if (!validThread) {
#if defined(L2_BUILD_L2SERVER)
                thread_safe_detail::FailInvalidPipeThread(threadId, functionName);
#else
                thread_safe_detail::FailInvalidPipeThread(threadId);
#endif
            }
        }
        if (!validThread) {
            return;
        }
        m_Pipe[static_cast<std::size_t>(threadId)].Write(value);
    }

    void Read(CReadCallback* callback) {
        L2_GUARD_FUNCTION();
        const int threadId = CThreadManager::GetThreadID();
        // Current L2NPC.exe 0x004560ec indexes the current logical thread
        // directly. Unlike Write, this wrapper has no validation branch.
        m_Pipe[static_cast<std::size_t>(threadId)].Read(callback);
    }

#if !defined(L2_BUILD_L2SERVER)
private:
#endif
    std::array<CThreadPipe, MaximumThreads> m_Pipe;
};

#if defined(L2_BUILD_L2SERVER)

class CIOObject;

// Physical Shared/ThreadSafeLibrary.h owner used by current L2Server. The
// current constructor at 0x00aec380 closes the complete member
// offsets recorded below. TotalChecker2015 supplies the original member
// vocabulary and access structure only.
class CThreadLocalTimer {
public:
    enum {
        TIME_SLICE = 50,
        RESERVED_SLOT_TIME = 10000,
        MAX_SLOT = 200,
        nMaxObjectCount = 469000,
        nMaxIOThread = 32,
        nCountPerSlot = 1000,
    };

    class CSlot {
    public:
        int GetCount() const { return nCount; }

        void Init() {
            nCount = 0;
            Next = nullptr;
        }

        bool AddTimer(CIOObject* object, const int id, const std::int64_t uid);
        void Dispatch();

        void Release() {
            nCount = 0;
            Next = nullptr;
        }

    private:
        CIOObject* Object[nCountPerSlot];
        int Id[nCountPerSlot];
        std::int64_t m_Uid[nCountPerSlot];
        int nCount;

    public:
        CSlot* Next;

    private:
        friend class CThreadLocalTimer;
    };

    class CTimerWrapper {
    public:
        CTimerWrapper() = default;

        CTimerWrapper(const std::int64_t time, CIOObject* object, int id, std::int64_t uid) :
            m_dwTime(time), m_pObject(object), m_id(id), m_Uid(uid) {}

        bool operator<(const CTimerWrapper& other) const noexcept { return m_dwTime > other.m_dwTime; }

        std::int64_t m_dwTime;
        CIOObject* m_pObject;
        int m_id;
        std::int64_t m_Uid;
    };

    using TimerQueueType = runtime::PriorityQueue<CTimerWrapper, std::vector<CTimerWrapper>, std::less<CTimerWrapper>>;

    CThreadLocalTimer();

    void AddTimer(CIOObject* object, const std::int64_t time, const int id, const std::int64_t uid);
    void Dispatch();
    void DispatchAndClearAll();
    void ClearAll();
    void GetTimerCount(int& timerCount, int& shortTimeCount);

    // Embedded-host teardown adapter. The current server has no public
    // cancellation entry point because timer owners normally live to process
    // exit. This method adds no storage and is never used by shipping paths.
    std::size_t CancelOwnerForPortableShutdown(CIOObject* owner, std::size_t* nonzeroUidCount);

private:
    void Dispatch(CSlot* slot, std::int64_t slotIndex);
    void Release(CSlot* slot, std::int64_t slotIndex);

    CMwmrQueue<CSlot, 6869> m_SlotQueue;
    CSlot* m_ReservedSlot[nMaxIOThread][MAX_SLOT];
    std::int64_t m_CurrentTime[nMaxIOThread];
    std::int64_t m_CurrentSlotBase[nMaxIOThread];
    TimerQueueType m_ExtTimer[nMaxIOThread];
};


extern CThreadLocalTimer g_ThreadLocalTimer;

#endif

#if defined(L2_BUILD_L2SERVER)

#include "Shared/src/Runtime/ProcessRuntime.h"


class CAlliance;
class CAirShip;
class CCreature;
class CGeneralObject;
class CItem;
class CParty;
class CPledge;
class CShuttle;
class CStaticObject;
class CVehicle;

namespace l2server_smart_id_detail {

[[noreturn]] void FailRegisterInvalidClass(std::uint32_t objectClass, const wchar_t* functionName);
[[noreturn]] void FailRegisterInvalidIndex(std::uint32_t index, const wchar_t* functionName);
void ReportNullRegistration();
void ReportLookupOutsideIoThread(int threadId);
void ReportDeleteInvalidIndex(std::uint32_t index, std::uint32_t smartId);
void ReportReadInvalidIndex(std::uint32_t index);

template <typename TObject, std::size_t MaximumObjects, std::size_t MaximumThreads>
struct ManagerNames {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager::CreateSmartID("
               L"unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager::RegisterObject("
               L"unsigned int,unsigned int,CObject *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager::DeleteObject("
               L"unsigned int)";
    }
};

template <>
struct ManagerNames<CGeneralObject, 1020000, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CGeneralObject,1020000,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CGeneralObject,1020000,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CGeneralObject *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CGeneralObject,"
               L"1020000,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CCreature, 120000, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CCreature,120000,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CCreature,120000,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CCreature *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CCreature,"
               L"120000,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CAirShip, 1000, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CAirShip,1000,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CAirShip,1000,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CAirShip *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CAirShip,"
               L"1000,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CShuttle, 1000, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CShuttle,1000,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CShuttle,1000,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CShuttle *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CShuttle,"
               L"1000,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CItem, 900000, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CItem,900000,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CItem,900000,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CItem *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CItem,"
               L"900000,16>::DeleteObject(unsigned int)";
    }
};

// Every current L2Server manager specialization stores a zero-displacement
// CObject family pointer. Calling the inherited MemoryObject slots through
// that proven primary-base address keeps the original virtual transport and
// also permits publication of a manager before its very large concrete
// object envelope is complete.
template <typename TObject>
MemoryObject* ManagedMemoryObject(TObject* object) noexcept {
    return reinterpret_cast<MemoryObject*>(object);
}

template <>
struct ManagerNames<CStaticObject, 10001, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CStaticObject,10001,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CStaticObject,10001,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CStaticObject *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CStaticObject,"
               L"10001,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CVehicle, 100, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CVehicle,100,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CVehicle,100,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CVehicle *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CVehicle,100,16>::"
               L"DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CAlliance, 5000, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CAlliance,5000,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CAlliance,5000,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CAlliance *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CAlliance,"
               L"5000,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CParty, 10003, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CParty,10003,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CParty,10003,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CParty *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CParty,"
               L"10003,16>::DeleteObject(unsigned int)";
    }
};

template <>
struct ManagerNames<CPledge, 10002, 16> {
    static constexpr const wchar_t* CreateSmartID() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CPledge,10002,16>::CreateSmartID(unsigned int,int)";
    }
    static constexpr const wchar_t* RegisterObject() noexcept {
        return L"unsigned int __cdecl CSmartIdManager<class "
               L"CPledge,10002,16>::RegisterObject(unsigned int,"
               L"unsigned int,class CPledge *)";
    }
    static constexpr const wchar_t* DeleteObject() noexcept {
        return L"void __cdecl CSmartIdManager<class CPledge,"
               L"10002,16>::DeleteObject(unsigned int)";
    }
};

} // namespace l2server_smart_id_detail

#endif // L2_BUILD_L2SERVER

// Shared ID geometry/storage. Registration and failure contracts remain
// product-specific. No additional base or recovered object fields are added.
#if defined(L2_BUILD_L2SERVER)
template <typename TObject, std::size_t MaximumObjects, std::size_t MaximumThreads>
#else
template <typename TObject, std::size_t MaximumObjects = 120000, std::size_t MaximumThreads = 8>
#endif
class CSmartIdManager : private CTLPipeBase<std::uint32_t, MaximumObjects, MaximumThreads>::CReadCallback,
                        private CTLPipeBase<std::uint32_t, MaximumObjects, MaximumThreads> {
public:
    static constexpr std::uint32_t kIndexMask = 0x000fffffU;
    static constexpr std::uint32_t kValidatorMask = 0x07f00000U;
    static constexpr unsigned kValidatorShift = 20;
    // static constexpr unsigned kClassShift = 27.
    static constexpr unsigned kClassShift = CLASS_SHIFT;

    CSmartIdManager() {
        for (std::size_t index = 0; index < MaximumObjects; ++index) {
            Validator[index].store(0, std::memory_order_relaxed);
            Object[index].store(nullptr, std::memory_order_relaxed);
            for (std::size_t thread = 0; thread < MaximumThreads; ++thread) {
                Usable[UsableOffset(index, thread)].store(false, std::memory_order_relaxed);
            }
        }
#if defined(L2_BUILD_L2SERVER)
        ERROR_MaxIndexOverflow.store(false, std::memory_order_relaxed);
#endif
    }

    static constexpr std::size_t Index(std::uint32_t smartId)
#if defined(L2_BUILD_L2SERVER)
        noexcept
#endif
    {
        return smartId & kIndexMask;
    }

    static constexpr std::uint8_t GetValidatorFromSmartID(std::uint32_t smartId)
#if defined(L2_BUILD_L2SERVER)
        noexcept
#endif
    {
        return static_cast<std::uint8_t>((smartId & kValidatorMask) >> kValidatorShift);
    }

    static constexpr std::uint32_t ObjectClass(std::uint32_t smartId)
#if defined(L2_BUILD_L2SERVER)
        noexcept
#endif
    {
        return smartId >> kClassShift;
    }

#if defined(L2_BUILD_L2SERVER)
    std::uint32_t CreateSmartID(std::uint32_t objectClass, int index) {
        L2_GUARD_FUNCTION();
        const std::size_t slot = static_cast<std::size_t>(index);
        const std::uint8_t validator =
            static_cast<std::uint8_t>((Validator[slot].load(std::memory_order_relaxed) + 1U) & 0x7fU);
        Validator[slot].store(validator, std::memory_order_release);
        return ((
                    static_cast<std::uint32_t>(validator) |
                    // (objectClass << 7U))
                    (objectClass << (CLASS_SHIFT - kValidatorShift))
                ) << kValidatorShift) |
            (static_cast<std::uint32_t>(index) & kIndexMask);
    }
#endif

#if defined(L2_BUILD_L2SERVER)
    std::uint32_t RegisterObject(std::uint32_t objectClass, std::uint32_t index, TObject* object) {
        L2_GUARD_FUNCTION();
        const int currentThreadId = ::CThreadManager::GetThreadID();
        if (currentThreadId >= 0) {
            static_cast<void>(::CThreadManager::GetThreadCount());
        }
        // if (objectClass > 15U) {
        if (objectClass > (SCLS_MAX - 1)) {
            l2server_smart_id_detail::FailRegisterInvalidClass(
                objectClass,
                l2server_smart_id_detail::ManagerNames<TObject, MaximumObjects, MaximumThreads>::RegisterObject()
            );
        }
        if (index >= MaximumObjects) {
            l2server_smart_id_detail::FailRegisterInvalidIndex(
                index, l2server_smart_id_detail::ManagerNames<TObject, MaximumObjects, MaximumThreads>::RegisterObject()
            );
        }
        if (!object) {
            l2server_smart_id_detail::ReportNullRegistration();
            return 0;
        }

        const std::uint32_t smartId = CreateSmartID(objectClass, static_cast<int>(index));
        int thread = 0;
        int threadCount = ::CThreadManager::GetThreadCount();
        while (thread < threadCount) {
            Usable[UsableOffset(index, static_cast<std::size_t>(thread))].store(true, std::memory_order_release);
            L2_ADD_REF(l2server_smart_id_detail::ManagedMemoryObject(object), ObjectRefType::SmartId);
            ++thread;
            threadCount = ::CThreadManager::GetThreadCount();
        }
        Object[index].store(object, std::memory_order_release);
        return smartId;
    }
#else
    bool RegisterObject(std::uint32_t smartId, TObject* object) {
        L2_GUARD_FUNCTION();
        const std::uint32_t objectClass = ObjectClass(smartId);
        if (objectClass == 0U) {
            // enrich the diagnostic only. Registration
            // still returns false without changing the object registry.
            // thread_safe_detail::
            //     ReportSmartIdRegisterInvalidClass(
            //         objectClass,
            //         smartId,
            //         0x701)
            thread_safe_detail::ReportSmartIdRegisterInvalidClass(objectClass, smartId, 0x701, object);
            return false;
        }
        // if (objectClass != 5U &&
        // objectClass != 9U) {
        if (objectClass != SCLS_FAKE_NPC && objectClass != SCLS_CREATURE) {
            thread_safe_detail::ReportSmartIdRegisterInvalidClass(objectClass, smartId, 0x706);
            return false;
        }
        const std::size_t index = Index(smartId);
        if (index >= MaximumObjects) {
            thread_safe_detail::ReportSmartIdRegisterInvalidIndex(static_cast<std::uint32_t>(index), smartId);
            return false;
        }
        SetValidator(static_cast<int>(index), GetValidatorFromSmartID(smartId));
        Object[index].store(object, std::memory_order_release);
        for (int thread = 0; thread < ActiveThreadCount(); ++thread) {
            Usable[UsableOffset(index, static_cast<std::size_t>(thread))].store(true, std::memory_order_release);
            L2_ADD_REF(object, ObjectRefType::SmartId);
        }
        return true;
    }
#endif

#if defined(L2_BUILD_L2SERVER)
    TObject* GetObjectW(std::uint32_t smartId) const {
        const int threadId = ::CThreadManager::GetThreadID();
        if (threadId >= 0 && threadId < ::CThreadManager::GetThreadCount()) {
            const std::size_t index = Index(smartId);
            if (index >= MaximumObjects) {
                return nullptr;
            }
            if (Validator[index].load(std::memory_order_acquire) != GetValidatorFromSmartID(smartId)) {
                return nullptr;
            }
            if (!Usable[UsableOffset(index, static_cast<std::size_t>(threadId))].load(std::memory_order_acquire)) {
                return nullptr;
            }
            return Object[index].load(std::memory_order_acquire);
        }
        l2server_smart_id_detail::ReportLookupOutsideIoThread(threadId);
        return nullptr;
    }
#else
    TObject* GetObjectW(std::uint32_t smartId) const {
        const int threadId = CThreadManager::GetThreadID();
        if (threadId < 0) {
            thread_safe_detail::ReportLookupOutsideIoThread(threadId);
            return nullptr;
        }
        const int threadCount = CThreadManager::GetThreadCount();
        if (threadId >= threadCount) {
            thread_safe_detail::ReportLookupOutsideIoThread(threadId);
            return nullptr;
        }
        return GetObjectWForRegisteredThread(smartId, static_cast<std::size_t>(threadId));
    }
#endif

#if defined(L2_BUILD_L2SERVER)
    void DeleteObject(std::uint32_t smartId) {
        L2_GUARD_FUNCTION();
        const std::size_t index = Index(smartId);
        if (index >= MaximumObjects) {
            l2server_smart_id_detail::ReportDeleteInvalidIndex(static_cast<std::uint32_t>(index), smartId);
            return;
        }

        const std::uint32_t value = static_cast<std::uint32_t>(index);
        int thread = 0;
        int threadCount = ::CThreadManager::GetThreadCount();
        while (thread < threadCount) {
            PipeBase().Write(thread, value);
            ++thread;
            threadCount = ::CThreadManager::GetThreadCount();
        }
    }
#else
    void DeleteObject(std::uint32_t smartId) {
        L2_GUARD_FUNCTION();
        const std::size_t index = Index(smartId);
        if (index >= MaximumObjects) {
            thread_safe_detail::ReportSmartIdDeleteInvalidIndex(static_cast<std::uint32_t>(index), smartId);
            return;
        }

        const auto value = static_cast<std::uint32_t>(index);
        for (int thread = 0; thread < ActiveThreadCount(); ++thread) {
            PipeBase().Write(thread, value);
        }
    }
#endif

    void GarbageCollect() { PipeBase().Read(this); }

#if defined(L2_BUILD_L2SERVER)
    static constexpr std::size_t MaximumObjectCount() noexcept { return MaximumObjects; }

    static constexpr std::size_t MaximumThreadCount() noexcept { return MaximumThreads; }


#endif

private:
    using Pipe = CTLPipeBase<std::uint32_t, MaximumObjects, MaximumThreads>;

    static constexpr std::size_t UsableOffset(std::size_t index, std::size_t threadId)
#if defined(L2_BUILD_L2SERVER)
        noexcept
#endif
    {
        return index * MaximumThreads + threadId;
    }

    Pipe& PipeBase()
#if defined(L2_BUILD_L2SERVER)
        noexcept
#endif
    {
        return static_cast<Pipe&>(*this);
    }

    void OnRead(const std::uint32_t& value) override {
        const volatile std::uint32_t& liveValue = value;
        const std::size_t index = liveValue;
        if (index >= MaximumObjects) {
#if defined(L2_BUILD_L2SERVER)
            l2server_smart_id_detail::ReportReadInvalidIndex(value);
#else
            thread_safe_detail::ReportSmartIdReadInvalidIndex(value);
#endif
            return;
        }
        const int threadId = ::CThreadManager::GetThreadID();
        Usable[UsableOffset(index, static_cast<std::size_t>(threadId))].store(false, std::memory_order_release);
        TObject* const object = Object[static_cast<std::size_t>(liveValue)].load(std::memory_order_acquire);
        // Keep the second volatile queue read and unconditional Release.
#if defined(L2_BUILD_L2SERVER)
        L2_RELEASE(l2server_smart_id_detail::ManagedMemoryObject(object), ObjectRefType::SmartId);
#else
        L2_RELEASE(object, ObjectRefType::SmartId);
#endif
    }


#if !defined(L2_BUILD_L2SERVER)
    static constexpr bool ValidIndex(std::uint32_t smartId) { return Index(smartId) < MaximumObjects; }

    TObject* GetObjectWForRegisteredThread(std::uint32_t smartId, std::size_t threadId) const {
        const std::size_t index = Index(smartId);
        if (index >= MaximumObjects) {
            return nullptr;
        }
        if (Validator[index].load(std::memory_order_acquire) != GetValidatorFromSmartID(smartId)) {
            return nullptr;
        }
        if (!Usable[UsableOffset(index, threadId)].load(std::memory_order_acquire)) {
            return nullptr;
        }
        return Object[index].load(std::memory_order_acquire);
    }

    void SetValidator(int index, int validator) {
        L2_GUARD_FUNCTION();
        Validator[static_cast<std::size_t>(index)].store(static_cast<std::uint8_t>(validator), std::memory_order_release);
    }

    static int ActiveThreadCount() { return CThreadManager::GetThreadCount(); }
#endif

    // No "{}" here: for millions of elements MSVC expands the initializer
    // element by element and needs tens of gigabytes of compiler memory.
    // The managers are global objects, so their storage starts zeroed.
    std::array<std::atomic<bool>, MaximumObjects * MaximumThreads> Usable;
    std::array<std::atomic<std::uint8_t>, MaximumObjects> Validator;
    std::array<std::atomic<TObject*>, MaximumObjects> Object;
    std::atomic<bool> ERROR_MaxIndexOverflow{false};
};

#if defined(L2_BUILD_L2SERVER)


using CCreatureSmartIdManager = CSmartIdManager<CCreature, 120000, 16>;
using CItemSmartIdManager = CSmartIdManager<CItem, 900000, 16>;
using CAirShipSmartIdManager = CSmartIdManager<CAirShip, 1000, 16>;
using CShuttleSmartIdManager = CSmartIdManager<CShuttle, 1000, 16>;
using CVehicleSmartIdManager = CSmartIdManager<CVehicle, 100, 16>;

// Current global storage begins at 02387690. The historical spelling is
// kept only after current RTTI, code references and the exact extent
// prove this single process owner.
extern CCreatureSmartIdManager g_CreatureSmartIdManager;

// Current accessor 007d0de4 returns the process-global storage beginning at
// 1024a8b0. The identifier spellings come from the adjacent donor only.
// specialization, body, base and exact extent are all current-image facts.
extern CItemSmartIdManager g_ItemSmartIdManager;
CItemSmartIdManager* ItemSmartIdManager() noexcept;

// The complete CAirShip owner is not yet published with its static member.
// CShuttle now carries its current physical static owner in Shuttle.h/.cpp.
extern CAirShipSmartIdManager g_AirShipSmartIdManager;
extern CVehicleSmartIdManager g_VehicleSmartIdManager;


#endif // L2_BUILD_L2SERVER

#if !defined(L2_BUILD_L2SERVER)


// PDB global defined in l2npc/Creature.cpp. Every current L2NPC.exe lookup,
// registration, retirement and IO-thread pipe drain addresses this one
// process object directly.
extern CSmartIdManager<CCreature, 120000, 8> g_CreatureSmartIdManager;

// Recovered four-byte value wrapper from shared/ThreadSafeLibrary.cpp.
class CSmartID {
public:
    CSmartID() = default;
    explicit CSmartID(CObject* object);

    CSmartID& operator=(const CSmartID& other);
    CSmartID& operator=(CObject* object);

    CCreature* GetCreature() const;
    std::uint32_t GetId() const;
    CObject* GetObjectW() const;
    bool IsValid() const;
    std::uint32_t Sid() const;

private:
    std::uint32_t sid_ = 0;
};

#endif
