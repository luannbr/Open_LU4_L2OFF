#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/ScopedLockGuards.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/FixedSharedMemoryTransport.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadManager.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

// Exact process global compiled by Shared/TemplateHelper.cpp. Every
// Singleton<T>::inst specialization serializes its first construction
// through this recursive critical section.
extern CIOCriticalSection g_SingletonLock;

// inst() returns a reference. It creates the object under the singleton
// lock on first use. The object lives until process exit.
// Keep the caller's file and line in the original guard macro.
template <typename T>
class Singleton {
public:
    static T& inst() {
        if (!mpInst) {
            L2_GUARD(g_SingletonLock);
            if (!mpInst) {
                mpInst = new T;
            }
        }
        return *mpInst;
    }

protected:
    Singleton() = default;
    virtual ~Singleton() = default;

    inline static T* mpInst = nullptr;
};

// Shared/TemplateHelper.h:897-1012. The ring stores opaque owning pointers.
// AtomicJob adds the diagnostic map side effects through its exact traits
// specialization in AtomicJob.h without coupling this common header to that
// owner.
template <typename TObject>
struct ObjectCacheTraits {
    static void OnPush(int, TObject*) {}
    static void OnPop(int) {}
};

template <typename TObject, int Capacity>
class ObjectCache {
    static_assert(Capacity > 0);

public:
    ObjectCache() : mFreeList{}, mIn(0), mOut(0), mCount(0), mPadding(0), mObjectCache{} {
        for (int index = 0; index < Capacity; ++index) {
            mFreeList[index] = index + 1;
        }
    }

    int Push(TObject* object) {
        if (object == nullptr) {
            return 0;
        }

        L2_GUARD(mLock);
        const int freeId = mFreeList[mOut];
        ObjectCacheTraits<TObject>::OnPush(mOut, object);
        if (mObjectCache[freeId] == nullptr) {
            mObjectCache[freeId] = object;
            mOut = (mOut + 1) % Capacity;
            ++mCount;
            return freeId;
        }
        return 0;
    }

    TObject* Pop(int id) {
        if (id < 1 || id > Capacity) {
            return nullptr;
        }

        L2_GUARD(mLock);
        ObjectCacheTraits<TObject>::OnPop(mIn);
        TObject* const object = mObjectCache[id];
        if (object != nullptr) {
            mObjectCache[id] = nullptr;
            mFreeList[mIn] = id;
            --mCount;
            mIn = (mIn + 1) % Capacity;
        }
        return object;
    }

private:
    CIOCriticalSection mLock; // +0x00000
    std::int32_t mFreeList[Capacity]; // +0x00028
    std::int32_t mIn; // +0x0a028 (Capacity=10240)
    std::int32_t mOut; // +0x0a02c
    std::int32_t mCount; // +0x0a030
    std::int32_t mPadding; // +0x0a034
    TObject* mObjectCache[Capacity + 1]; // +0x0a038
};

// Shared/TemplateHelper.h:519-599. The current L2NPC image independently
// confirms Register and its 0x20-byte MSVC std::map member. The matching NASC
// source image additionally emits CreateObject from the same shared header.
template <typename AbstractProduct, typename IdentifierType, typename ProductCreator>
class GenericFactory {
public:
    bool Register(const IdentifierType& id, ProductCreator creator) {
        L2_GUARD_FUNCTION();
        return mIdToProductMap.emplace(id, creator).second;
    }

    // Declared by the PDB template surface, but not compiled by either matching
    // executable. Keep it declaration-only until its exact body is observed.
    bool Unregister(const IdentifierType& id);

    AbstractProduct* CreateObject(const IdentifierType& id) const {
        L2_GUARD_FUNCTION();
        const auto found = mIdToProductMap.find(id);
        if (found == mIdToProductMap.end()) {
            return nullptr;
        }
        return found->second();
    }

private:
    using MAP_TYPE = std::map<IdentifierType, ProductCreator>;

    MAP_TYPE mIdToProductMap;
};

// TotalChecker line records prove that several factory families were compiled
// from one-line header expansions. Their exact macro name is not kept.
// These portable macros keep creator and registration symbols explicit and
// let each product keep its established once-only .cpp initialization
// order instead of recreating MSVC header-COMDAT duplicates.
#define L2_DEFINE_FACTORY_CREATOR(AbstractProduct, CreatorName, ConcreteProduct)                                            \
    AbstractProduct* CreatorName() { return new ConcreteProduct; }

#define L2_REGISTER_FACTORY_CREATOR(RegistrationName, FactoryType, Identifier, Creator)                                     \
    [[maybe_unused]] const bool RegistrationName = Singleton<FactoryType>::inst().Register(Identifier, Creator)

#define L2_DEFINE_AND_REGISTER_FACTORY_TYPE(                                                                                \
    AbstractProduct, ConcreteProduct, CreatorName, RegistrationName, FactoryType, Identifier                                \
)                                                                                                                           \
    L2_DEFINE_FACTORY_CREATOR(AbstractProduct, CreatorName, ConcreteProduct)                                                \
    L2_REGISTER_FACTORY_CREATOR(RegistrationName, FactoryType, Identifier, CreatorName)

// Shared/TemplateHelper.h:785-834. On Win64 this object owns a named
// file-mapping handle and its mapped T. Portable builds keep the mapping
// owner in the same handle-sized slot and keep the exact direct pointer.
template <typename T>
class SharedType {
public:
    explicit SharedType(const wchar_t* name) : mHandle(reinterpret_cast<void*>(~std::uintptr_t{0})), mpData(nullptr) {
        L2_GUARD_FUNCTION();

        std::size_t index = 0;
        for (; index < 260 && name[index] != L'\0'; ++index) {
            mName[index] = static_cast<char16_t>(name[index]);
        }
        mName[index] = u'\0';

        const portable::FixedSharedMemoryBinding binding =
#if defined(L2_BUILD_L2SERVER)
            portable::CreateFixedSharedMemory(
#else
            portable::OpenFixedSharedMemory(
#endif
                name, sizeof(T)
            );
        mHandle = binding.owner;
        mpData = static_cast<T*>(binding.records);
#if defined(L2_BUILD_L2SERVER)
        // Current L2Server.exe SharedType<SharedInstantAgitIndex>
        // constructor 0x00a57868 creates the mapping and clears the complete
        // T image. L2NPC keeps the separately proved open-only consumer
        // path selected above.
        std::memset(mpData, 0, sizeof(T));
#endif
    }

    ~SharedType() {
        L2_GUARD_FUNCTION();
        portable::CloseFixedSharedMemory(mHandle);
    }

#if defined(_WIN32)
    const wchar_t* GetName() const { return reinterpret_cast<const wchar_t*>(mName); }
#else
    const char16_t* GetName() const { return mName; }
#endif

    const T* operator->() const { return mpData; }

    T* operator->() { return mpData; }

    T& operator*() { return *mpData; }

    operator T*() { return mpData; }

private:
    char16_t mName[261];
    void* mHandle;
    T* mpData;
};


// Shared/TemplateHelper.h:1090-1101. The adjacent symbol-complete image
// records the exact { object, file, line } carrier and its destructor. The
// current L2NPC image emits the same carrier in both CIOSocketEx write paths.
// The legacy PDB spells fileName as char*. Const qualification changes no ABI
// and lets portable C++17 consume the same string literals without an unsafe
// cast.
template <typename TObject, ObjectRefType ReferenceType>
class ReleaseHelper {
public:
    ReleaseHelper(TObject* object, const char* sourceFile, int sourceLine) :
        pObject(object), fileName(sourceFile), lineNumber(sourceLine) {}

    ~ReleaseHelper() {
        if (pObject) {
            pObject->Release(fileName, lineNumber, ReferenceType, false);
        }
    }

private:
    TObject* pObject;
    const char* fileName;
    int lineNumber;
};

// Shared/TemplateHelper.h:1026-1048. L2NPC.pdb exposes the exact public
// Initialize/Alloc surface and the private {int mCurrentIndex. T mSlot[N].}
// layout. Alloc reinitializes the selected script object before returning it.
// exhaustion returns nullptr instead of wrapping.
template <typename T, std::size_t Capacity>
class TempObjectPool {
public:
    // The target constructor default-constructs every mSlot element before
    // its body calls Initialize() to set mCurrentIndex to zero. Keep that
    // instruction order. Clang-tidy cannot infer the member write performed
    // by the helper call.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    TempObjectPool() { Initialize(); }

    void Initialize() { mCurrentIndex = 0; }

    T* Alloc() {
        if (mCurrentIndex >= static_cast<int>(Capacity)) {
            return nullptr;
        }
        T* const result = &mSlot[static_cast<std::size_t>(mCurrentIndex++)];
        result->Init();
        return result;
    }

private:
    // The target constructs every slot first and only then initializes the
    // cursor in the constructor body. Keep these as ordinary data members so
    // no portable-only pre-construction store or value initialization is
    // introduced.
    int mCurrentIndex;
    T mSlot[Capacity];
};

// Exact PDB template:
//   ThreadLocalStorage<T,N> { T mTLObjects[N]. }
// operator->, operator* and the conversion operator select the logical
// worker through the target TLS thread id. Script execution is confined to
// the eight I/O workers, matching every shipping instantiation here.
template <typename T, std::size_t Capacity>
class ThreadLocalStorage {
public:
    ThreadLocalStorage() = default;
    ~ThreadLocalStorage() = default;

    T* operator->() { return Current(); }

    T& operator*() { return *Current(); }

    operator T*() { return Current(); }

private:
    T* Current() { return &mTLObjects[static_cast<std::size_t>(CThreadManager::GetThreadID())]; }

    T mTLObjects[Capacity]{};
};


namespace template_helper_detail {

// Non-ABI transport boundary for the target CLog call made by
// FixedSizeCallbackMap::TestRegistration. Keeping it out of the template
// avoids the Shared/log.h -> Shared/ioc.h -> TemplateHelper.h include cycle.
void ReportMissingCallbackRegistration(const char16_t* tableName, int id);
void ReportCallbackAddressRange(
    int minimumIndex, std::uint64_t minimumAddress, int maximumIndex, std::uint64_t maximumAddress
);

} // namespace template_helper_detail

// Shared/TemplateHelper.h:622-709. NASC64_2015.pdb supplies the source type
// that matches the current L2NPC.exe capacities (200 L2Server callbacks and
// eight CacheD callbacks), while the current image confirms
// every offset below. The adjacent older L2NPC.pdb predates mPacketName and
// therefore describes only the obsolete 182/7 specializations.
template <std::size_t Capacity, typename Callback>
class FixedSizeCallbackMap {
public:
    enum : int {
        SIZE = static_cast<int>(Capacity),
    };
    using value_type = Callback;

    FixedSizeCallbackMap(Callback defaultFunc, const char16_t* name) : mCount(0), mDefaultFunc(defaultFunc), mName(name) {
        // Current Cached.exe constructor specializations:
        //   AQ(191)    0x00432094, RQ(235)    0x00483c54,
        //   DX(116)    0x004af69c, GIP(3)     0x004dc690,
        //   ND(19)     0x004fb158, Query(4)   0x00523524,
        //   RQ_EX(461) 0x0054ab14.
        // Every shipping call site supplies a non-null UTF-16 name. The GIP
        // owner supplies the empty string.
        for (int index = 0; index < SIZE; ++index) {
            mCallbackArray[index] = mDefaultFunc;
            mRegistered[index] = false;
            mPacketName[index][0] = u'\0';
        }
    }

    bool Bind(int id, Callback callback) {
        // Current Cached.exe specializations:
        //   AQ(191)    0x0040fc00, RQ(235)    0x0045c6c4,
        //   DX(116)    0x004ac2b0, GIP(3)     0x004da71c,
        //   ND(19)     0x004fa60c, Query(4)   0x00523358,
        //   RQ_EX(461) 0x0052e548.
        // Matching L2NPC.exe specializations are 0x0041f55c/0x00521840.
        L2_GUARD_FUNCTION();
        if (static_cast<unsigned int>(id) >= Capacity) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(id);
        mCallbackArray[index] = callback;
        mRegistered[index] = true;
        mPacketName[index][0] = u'\0';
        return true;
    }

    bool Bind(int id, const char16_t* packetName, Callback callback) {
        // Current L2Server specializations:
        //   C_PROTOCOL(209)    0x00c118d0
        //   C_EX_PROTOCOL(352) 0x00be94ac
        // Both use the same target layout and validation order: unsigned
        // range check, non-null/non-empty packet name, callback store,
        // registration flag, then wcscpy_s(..., 0x40, packetName).
        L2_GUARD_FUNCTION();
        if (static_cast<unsigned int>(id) >= Capacity || packetName == nullptr || packetName[0] == u'\0') {
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(id);
        mCallbackArray[index] = callback;
        mRegistered[index] = true;

        std::size_t length = 0;
        while (length < MAX_PACKET_NAME_LENGTH && packetName[length] != u'\0') {
            ++length;
        }
        if (length >= MAX_PACKET_NAME_LENGTH) {
            // MSVC wcscpy_s clears the destination on a range failure. The
            // Bind result remains true because the current body sets it before
            // making the CRT call.
            mPacketName[index][0] = u'\0';
            errno = ERANGE;
            runtime::process::HandleInvalidParameter();
            return true;
        }
        std::memcpy(mPacketName[index], packetName, (length + 1U) * sizeof(char16_t));
        return true;
    }

    const char16_t* GetPacketName(int id) { return mPacketName[id]; }

    Callback GetCallback(int id) { return mCallbackArray[id]; }

    constexpr Callback* GetData() { return mCallbackArray; }

    bool TestRegistration() {
        bool result = true;
        for (int index = 0; index < SIZE; ++index) {
            if (!mRegistered[index]) {
                template_helper_detail::ReportMissingCallbackRegistration(mName.c_str(), index);
                result = false;
            }
        }
        return result;
    }

    void LogFuncAddrRange() {
        // Current L2Server.exe 0x007f6744, compiled for the 308-entry
        // RQ_EX specialization. Slot zero seeds both extrema. Equal callback
        // addresses keep the first matching index.
        static_assert(Capacity > 0);
        std::uint64_t minimumAddress = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(mCallbackArray[0]));
        std::uint64_t maximumAddress = minimumAddress;
        int minimumIndex = 0;
        int maximumIndex = 0;
        for (int index = 0; index < SIZE; ++index) {
            const std::uint64_t address = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(mCallbackArray[static_cast<std::size_t>(index)])
            );
            if (address < minimumAddress) {
                minimumAddress = address;
                minimumIndex = index;
            }
            if (maximumAddress < address) {
                maximumAddress = address;
                maximumIndex = index;
            }
        }
        template_helper_detail::ReportCallbackAddressRange(minimumIndex, minimumAddress, maximumIndex, maximumAddress);
    }

private:
    static constexpr std::size_t MAX_PACKET_NAME_LENGTH = 64;

    int mCount;
    Callback mDefaultFunc;
    Callback mCallbackArray[Capacity];
    bool mRegistered[Capacity];
    char16_t mPacketName[Capacity][MAX_PACKET_NAME_LENGTH];
    // PDB source type: std::wstring. libc++'s host wstring has the wrong
    // character width and layout, so use the exact 0x20 MSVC UTF-16 carrier.
    std::u16string mName;
};

// The NASC64 browse database keeps the historical BIND_PACKET spelling,
// and TotalChecker2015 keeps the resulting IsRegistered_* globals and
// their individual dynamic initializers. Neither donor keeps the macro's
// definition, so keep an honest portable name and make the registration
// object explicit. RegistrationName is supplied by the owner:
// some current L2Server boundaries have exact donor names while others only
// have current-machine synthetic names recorded by their locked recovery.
#define L2_BIND_PACKET(RegistrationName, Table, ...) [[maybe_unused]] const bool RegistrationName = (Table).Bind(__VA_ARGS__)

#define L2_PACKET_REGISTRATION_NAME_IMPL(Packet) IsRegistered_##Packet
#define L2_PACKET_REGISTRATION_NAME(Packet) L2_PACKET_REGISTRATION_NAME_IMPL(Packet)
#define L2_BIND_PACKET_TOKEN(Table, Packet, Callback)                                                                       \
    L2_BIND_PACKET(L2_PACKET_REGISTRATION_NAME(Packet), Table, Packet, Callback)
