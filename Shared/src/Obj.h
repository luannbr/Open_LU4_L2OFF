#pragma once

#include "Shared/src/Runtime/SourceFile.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#define L2_MEMORY_WIDEN_IMPL(value) L##value
#define L2_MEMORY_WIDEN(value) L2_MEMORY_WIDEN_IMPL(value)

// Portable registration: IDs belong to this process's initialization order,
// not to the shipping executable's fixed number table. The function-local
// value also handles an object used by another translation unit's initializer
// before its explicit registration runs. It never publishes an ID of zero.
namespace runtime {
std::int32_t RegisterMemoryClass(const char* name, std::int32_t size) noexcept;

template <typename T>
std::int32_t MemoryClassIdFor(const char* name) noexcept {
    static const std::int32_t id = RegisterMemoryClass(name, static_cast<std::int32_t>(sizeof(T)));
    return id;
}

template <typename T>
struct MemoryClassRegistration {
    explicit MemoryClassRegistration(const char* name) noexcept { (void)MemoryClassIdFor<T>(name); }
};
} // namespace runtime

#define L2_MEMORY_CLASS_ID(...) (::runtime::MemoryClassIdFor<__VA_ARGS__>(#__VA_ARGS__))
// Keep an inline registration even if no caller reads its compatibility field.
// The portable Clang/GCC targets otherwise may omit an unreferenced initializer.
#if defined(__GNUC__)
#define L2_MEMORY_REGISTRATION_USED __attribute__((used))
#else
#define L2_MEMORY_REGISTRATION_USED
#endif
// Registration has no second ID field: MemoryClassIdFor<T> owns the only value.
#define L2_DECLARE_OBJECT_REGISTRATION(ClassToken)                                                                          \
    L2_MEMORY_REGISTRATION_USED inline static const ::runtime::MemoryClassRegistration<ClassToken>                       \
        l2_memory_registration_##ClassToken{#ClassToken};

// Declarations only. Implementations (including custom lifetime policies) stay
// in the original .cpp. These macros neither add an override nor change its body.
#define L2_DECLARE_OBJECT_CLASS_ACCESSORS()                                                                                 \
    static int __GetClassID() noexcept;                                                                                     \
    static const wchar_t* __GetClassName() noexcept;
#define L2_DECLARE_OBJECT_ALLOCATORS()                                                                                      \
    static void* operator new(std::size_t requestedSize, bool objectAllocation);                                            \
    static void* operator new(std::size_t requestedSize);
#define L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()                                                                             \
    std::int32_t Release(const char* callerFile, int callerLine, ::ObjectRefType type, bool releaseFromTimer) override;
#define L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()                                                                             \
    ::MemoryObject* AddRef(const char* callerFile, int callerLine, ::ObjectRefType type) override;
#define L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()                                                                            \
    L2_DECLARE_OBJECT_CLASS_ACCESSORS()                                                                                     \
    L2_DECLARE_OBJECT_ALLOCATORS()                                                                                          \
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
#define L2_DETAIL_MEMORY_JOIN_IMPL(a, b) a##b
#define L2_DETAIL_MEMORY_JOIN(a, b) L2_DETAIL_MEMORY_JOIN_IMPL(a, b)
// Namespace scope in the physical .cpp owner. __COUNTER__ names the registrar,
// never the memory class ID. Repeated registrations of one type share one ID.
#define L2_REGISTER_OBJECT_TYPE(...)                                                                                        \
    namespace {                                                                                                             \
    [[maybe_unused]] const ::runtime::MemoryClassRegistration<                                                           \
        __VA_ARGS__> L2_DETAIL_MEMORY_JOIN(l2_memory_registration_, __COUNTER__)(#__VA_ARGS__);                             \
    }

// Preserve the original mutable pointer and its inline/out-of-line storage.
#define L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(ClassToken)                                                                    \
    inline static const wchar_t* m_pClassName_##ClassToken = L2_MEMORY_WIDEN(#ClassToken);
#define L2_DEFINE_OBJECT_CLASS_NAME_STORAGE(ClassToken)                                                                     \
    const wchar_t* ClassToken::m_pClassName_##ClassToken = L2_MEMORY_WIDEN(#ClassToken);

#define L2_DECLARE_OBJECT_CLASS_NAME_LITERAL(ClassToken)                                                                    \
    static const wchar_t* __GetClassName() noexcept { return L2_MEMORY_WIDEN(#ClassToken); }

// Portable convenience macros. These names are not recovered donor macros.
// Capture the invocation site, evaluate each argument once and keep the
// underlying virtual call and its result. Use direct calls when forwarding
// existing caller coordinates. ReleaseAs has separate caller/report sites.
#define L2_ADD_REF(pointer, type) ((pointer)->AddRef(L2_SOURCE_FILE, __LINE__, (type)))

#define L2_DETAIL_RELEASE_3(pointer, type, releaseFromTimer)                                                                \
    ((pointer)->Release(L2_SOURCE_FILE, __LINE__, (type), (releaseFromTimer)))
#define L2_DETAIL_RELEASE_2(pointer, type) L2_DETAIL_RELEASE_3(pointer, type, false)
#define L2_DETAIL_RELEASE_SELECT(_1, _2, _3, selected, ...) selected
// L2_RELEASE(pointer, type) or L2_RELEASE(pointer, type, releaseFromTimer).
// Supply false explicitly: derived Release overrides need not have a default.
#define L2_RELEASE(...) L2_DETAIL_RELEASE_SELECT(__VA_ARGS__, L2_DETAIL_RELEASE_3, L2_DETAIL_RELEASE_2, unused)(__VA_ARGS__)

// NASC64 browse data keeps the donor macro spellings DECLARE_ORT_LIST,
// DECLARE_ORT_TYPE and DECLARE_ORT_TYPE_FIRST, but not their definitions.
// Keep an honestly named local list as the single source for both the enum
// and its diagnostic name table. Numeric values are the shipping Shared/obj.h
// reference-accounting contract. Current CacheD and L2NPC machine code
// use the same values.
#define L2_OBJECT_REF_TYPE_LIST(X)                                                                                          \
    X(Anonymous, 0, L"ORT_ANONYMOUS")                                                                                       \
    X(CreateDelete, 1, L"ORT_CREATE_DELETE")                                                                                \
    X(Timer, 2, L"ORT_TIMER")                                                                                               \
    X(IoRetain, 3, L"ORT_IO_RETAIN")                                                                                        \
    X(IocpMessageRetain, 4, L"ORT_IOCP_MESSAGE_RETAIN")                                                                     \
    X(ObjectMap, 5, L"ORT_OBJECT_MAP")                                                                                      \
    X(ObjectNameMap, 6, L"ORT_OBJECT_NAME_MAP")                                                                             \
    X(ObjectIndex, 7, L"ORT_OBJECT_INDEX")                                                                                  \
    X(ObjectStorage, 8, L"ORT_OBJECT_STORAGE")                                                                              \
    X(FindObject, 9, L"ORT_FIND_OBJECT")                                                                                    \
    X(ThreadLocalMap, 10, L"ORT_TL_MAP")                                                                                    \
    X(SharedContainer, 11, L"ORT_SHARED_CONTAINER")                                                                         \
    X(SmartId, 12, L"ORT_SMART_ID")                                                                                         \
    X(SerializedWorldThread, 13, L"ORT_SERIALIZED_WORLD_THREAD")                                                            \
    X(TemporaryUse, 14, L"ORT_TEMP_USE")                                                                                    \
    X(Cached, 15, L"ORT_CACHED")                                                                                            \
    X(FunctionLocal, 16, L"ORT_FUNCTION_LOCAL")                                                                             \
    X(FunctionChain, 17, L"ORT_FUNCTION_CHAIN")                                                                             \
    X(EventRetain, 18, L"ORT_EVENT_RETAIN")                                                                                 \
    X(InventoryChangeMap, 19, L"ORT_INVENTORY_CHANGE_MAP")                                                                  \
    X(IoSocketWrite, 20, L"ORT_IO_SOCKET_WRITE")                                                                            \
    X(NpcRelated, 21, L"ORT_NPC_RELATED")                                                                                   \
    X(VerifierRetain, 22, L"ORT_VERIFIER_RETAIN")                                                                           \
    X(ClassMember, 23, L"ORT_CLASS_MEMBER")                                                                                 \
    X(SocketMap, 24, L"ORT_SOCKET_MAP")                                                                                     \
    X(AccountMap, 25, L"ORT_ACCOUNT_MAP")                                                                                   \
    X(UserSocketMap, 26, L"ORT_USER_SOCKET_MAP")                                                                            \
    X(UserSocketPlayMap, 27, L"ORT_USER_SOCKET_PLAY_MAP")                                                                   \
    X(UserSocketCharacterMap, 28, L"ORT_USER_SOCKET_CHAR_MAP")                                                              \
    X(SerializedMessage, 29, L"ORT_SERIALIZED_MESSAGE")                                                                     \
    X(TddUse, 30, L"ORT_TDD_USE")                                                                                           \
    X(DynamicQuestSerializer, 31, L"ORT_DYNAMIC_QUEST_SERIALIZER")                                                          \
    X(EventCampaignSerializer, 32, L"ORT_EVENT_CAMPAIGN_SERIALIZER")                                                        \
    X(Post, 33, L"ORT_POST")                                                                                                \
    X(RelayServerSocketMap, 34, L"ORT_RELAY_SERVER_SOCKET_MAP")                                                             \
    X(AccountServerSocketMap, 35, L"ORT_ACCOUNT_SERVER_SOCKET_MAP")

enum class ObjectRefType : std::uint32_t {
#define L2_DECLARE_OBJECT_REF_TYPE(Name, Value, DiagnosticName) Name = Value,
    L2_OBJECT_REF_TYPE_LIST(L2_DECLARE_OBJECT_REF_TYPE)
#undef L2_DECLARE_OBJECT_REF_TYPE
        Maximum = 36,
};

const wchar_t* ObjectRefTypeName(ObjectRefType type);

struct RefHelperStruct {
    static constexpr const char* s_Uninitialized = "Uninitilized Func";

    const char* m_pAddCaller = s_Uninitialized;
    const char* m_pReleaseCaller = s_Uninitialized;
    std::atomic<std::int32_t> m_nRefCount{0};
    std::int32_t m_nAddLine = 0;
    std::int32_t m_nReleaseLine = 0;
};

struct RefHelpers {
    static constexpr std::size_t kCount = static_cast<std::size_t>(ObjectRefType::Maximum);

    RefHelpers();

    std::array<RefHelperStruct, kCount> m_RefData;
};

// Both current executables have the same four-slot vtable and 0x18-byte
// layout. Allocation and diagnostics are delegated at the process boundary.
class MemoryObject {
private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(MemoryObject)
    inline static std::int32_t m_nMemoryObjectCount = 0;

public:
    MemoryObject();
    MemoryObject(const MemoryObject&);

    static int __GetClassID() noexcept;
    static const wchar_t* __GetClassName() noexcept;
    static void* operator new(std::size_t requestedSize, bool objectAllocation);
    static void* operator new(std::size_t requestedSize);
    static void operator delete(void* allocation, bool objectAllocation) noexcept;
    static void operator delete(void* allocation) noexcept;
    static void operator delete(MemoryObject* object, std::destroying_delete_t) noexcept;

    virtual MemoryObject* AddRef(const char* callerFile, int callerLine, ObjectRefType type);
    virtual std::int32_t Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer = false);
    virtual ~MemoryObject();
    virtual void ReportOrt(const char* callerFile, int callerLine);

    std::int32_t RefCount() const;
    std::int32_t MemoryObjectID() const;
    std::int32_t IncreaseTLRef();
    std::int32_t DecreaseTLRef();
    bool IsInTimer() const;
    void SetInTimer(bool value);

protected:
    std::int32_t ReleaseAs(
        int classId, const char* className, const char* reportFile, int reportLine, const char* callerFile, int callerLine
    );

    template <typename TClassId, typename = std::enable_if_t<std::is_enum_v<TClassId>>>
    std::int32_t ReleaseAs(
        TClassId classId,
        const char* className,
        const char* reportFile,
        int reportLine,
        const char* callerFile,
        int callerLine
    ) = delete; // Portable IDs are registered integers, not shipping enums.

    void DestroyReleasedObject();
    static void ReleaseClassInstance(int classId) noexcept;

    template <typename TClassId, typename = std::enable_if_t<std::is_enum_v<TClassId>>>
    static void ReleaseClassInstance(TClassId classId) noexcept = delete;

    volatile std::int32_t m_nRefCount;
    volatile std::int32_t __is_in_timer;

private:
    volatile std::int32_t m_nTLRef;
    std::int32_t m_nMemoryObjectID;
};
