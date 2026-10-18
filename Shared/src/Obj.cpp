#include "Shared/src/Obj.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"

namespace {

constexpr const wchar_t* kObjectRefTypeNames[] = {
#define L2_DECLARE_OBJECT_REF_TYPE_NAME(Name, Value, DiagnosticName) DiagnosticName,
    L2_OBJECT_REF_TYPE_LIST(L2_DECLARE_OBJECT_REF_TYPE_NAME)
#undef L2_DECLARE_OBJECT_REF_TYPE_NAME
};

static_assert(sizeof(kObjectRefTypeNames) / sizeof(kObjectRefTypeNames[0]) == RefHelpers::kCount);

} // namespace

const wchar_t* ObjectRefTypeName(ObjectRefType type) {
    const auto index = static_cast<std::size_t>(type);
    return index < RefHelpers::kCount ? kObjectRefTypeNames[index] : L"ORT_INVALID";
}

RefHelpers::RefHelpers() {
    RefHelperStruct& owner = m_RefData[static_cast<std::size_t>(ObjectRefType::CreateDelete)];
    owner.m_pAddCaller = "Internal";
    owner.m_nRefCount.store(1, std::memory_order_relaxed);
}

MemoryObject::MemoryObject() {
    L2_GUARD_FUNCTION();
    __is_in_timer = 0;
    m_nRefCount = 0;
    m_nMemoryObjectID = runtime::raw_atomic::AddI32(&m_nMemoryObjectCount, 1);
    m_nTLRef = 0;
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
}

MemoryObject::MemoryObject(const MemoryObject&) {
    L2_GUARD_FUNCTION();
    __is_in_timer = 0;
    m_nRefCount = 0;
    m_nMemoryObjectID = runtime::raw_atomic::AddI32(&m_nMemoryObjectCount, 1);
    // Current L2NPC.exe 0x00507080 leaves +0x10 unwritten.
    // m_nTLRef is raw volatile storage for that reason. Initializing it here
    // would change the target copy-constructor contract.
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
}

MemoryObject::~MemoryObject() = default;

L2_DEFINE_OBJECT_CLASS_ID(MemoryObject)

L2_DEFINE_OBJECT_CLASS_NAME(MemoryObject, m_pClassName_MemoryObject)

L2_DEFINE_OBJECT_ALLOCATOR(MemoryObject, __GetClassName())

L2_DEFINE_OBJECT_NEW(MemoryObject)

void MemoryObject::operator delete(void* allocation, bool) noexcept {
    FreeFailedMemoryClass(allocation);
}

void MemoryObject::operator delete(void* allocation) noexcept {
    FreeFailedMemoryClass(allocation);
}

void MemoryObject::operator delete(MemoryObject*, std::destroying_delete_t) noexcept {
    // Delete expressions use this overload. Constructor failure uses void*.
    L2_GUARD_FUNCTION();
    runtime::process::ReportForbiddenDelete(L"MemoryObject", __WFILE__, __LINE__);
}

L2_DEFINE_OBJECT_ADD_REF(MemoryObject)

L2_DEFINE_OBJECT_RELEASE(MemoryObject)

std::int32_t MemoryObject::ReleaseAs(
    int classId, const char* className, const char* reportFile, int reportLine, const char* callerFile, int callerLine
) {
    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            DestroyReleasedObject();
        } else {
            runtime::process::ReportDeletingInTimer(className, callerFile, callerLine);
            ReportOrt(reportFile, reportLine);
        }
        ReleaseClassInstance(classId);
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, className, MemoryObjectID(), callerFile, callerLine);
        ReportOrt(reportFile, reportLine);
        // The shipping Release body records the object's virtual ORT report
        // before collecting the process call stack (LOG_ERROR, sendMail).
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void MemoryObject::DestroyReleasedObject() {
    // Release in the current image calls the virtual scalar-deleting
    // destructor with flag zero, then calls Memory::Free itself. An
    // unqualified explicit call to a virtual destructor provides that exact
    // language-level operation without routing through operator delete.
    void* const allocation = this;
    this->~MemoryObject();
    runtime::process::FreeMemoryObject(allocation);
}

void MemoryObject::ReleaseClassInstance(int classId) noexcept {
    runtime::process::DecrementMemoryClassReference(classId);
}

void MemoryObject::ReportOrt(const char*, int) {}

std::int32_t MemoryObject::RefCount() const { return runtime::raw_atomic::LoadI32Relaxed(&m_nRefCount); }

std::int32_t MemoryObject::MemoryObjectID() const { return m_nMemoryObjectID; }

std::int32_t MemoryObject::IncreaseTLRef() { return runtime::raw_atomic::AddI32(&m_nTLRef, 1); }

std::int32_t MemoryObject::DecreaseTLRef() { return runtime::raw_atomic::AddI32(&m_nTLRef, -1); }

bool MemoryObject::IsInTimer() const { return runtime::raw_atomic::LoadI32Relaxed(&__is_in_timer) != 0; }

void MemoryObject::SetInTimer(bool value) {
    // CIOObject::OnTimer (0x00406084) and AddTimer2 (0x00456d10)
    // both compile this assignment to xchg, i.e. InterlockedExchange.
    runtime::raw_atomic::ExchangeI32(&__is_in_timer, value ? 1 : 0);
}
