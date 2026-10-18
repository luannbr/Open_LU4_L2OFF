#include "Item.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "User.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <cerrno>
#include <cstdio>

namespace {


constexpr const char* kItemOrtReportFile = "Item_sub.cpp";

template <typename ItemType>
void* AllocateDerivedItem(std::size_t requestedSize, const wchar_t* wideName, const char* narrowName) {
    return runtime::process::AllocateMemoryObject(
        requestedSize, true, runtime::MemoryClassIdFor<ItemType>(narrowName), wideName, narrowName,
        static_cast<std::int32_t>(sizeof(ItemType))
    );
}

// void ReportItemOrt(
//     const RefHelpers& refHelpers,
//     int reportTick,
//     const char* callerFile,
//     int callerLine) {
void ReportItemOrt(
    const RefHelpers& refHelpers,
    int reportTick,
    const char* callerFile,
    int callerLine,
    const char* itemType,
    std::uint32_t objectId
) {
    const int threadId = CThreadManager::GetThreadID();
    int sequence = 1;
    if (callerFile == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin "
        //     "---------------------------",
        //     threadId,
        //     reportTick,
        //     sequence)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin "
        //     "---------------------------",
        //     threadId,
        //     reportTick,
        //     sequence)
        L2_LOG_ERROR(
            Log, "%s object %u: reference report %d begins (worker %d, entry %d); caller unknown.", itemType,
            static_cast<unsigned>(objectId), reportTick, threadId, sequence
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     L"file[%S], line[%d] -------------",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        // L2_LOG_ERROR(Log,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     L"file[%S], line[%d] -------------",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        L2_LOG_ERROR(
            Log, "%s object %u: reference report %d begins (worker %d, entry %d), requested at %s:%d.", itemType,
            static_cast<unsigned>(objectId), reportTick, threadId, sequence,
            runtime::logging::SourceFileName(callerFile).data(), callerLine
        );
    }

    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = refHelpers.m_RefData[index];
        if (helper.m_pAddCaller == nullptr || helper.m_pReleaseCaller == nullptr) {
            continue;
        }
        const std::int32_t referenceCount = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (referenceCount == 0) {
            continue;
        }

        ++sequence;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"[RefCount:%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     referenceCount,
        //     ObjectRefTypeName(
        //         static_cast<ObjectRefType>(index)),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"[RefCount:%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     referenceCount,
        //     ObjectRefTypeName(
        //         static_cast<ObjectRefType>(index)),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "%s object %u: reference report %d, entry %d (worker %d): category '%s' has %d references; last add %s:%d, last "
            "release %s:%d.",
            itemType, static_cast<unsigned>(objectId), reportTick, sequence, threadId,
            WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index))).c_str(), referenceCount,
            runtime::logging::SourceFileName(helper.m_pAddCaller).data(), helper.m_nAddLine,
            runtime::logging::SourceFileName(helper.m_pReleaseCaller).data(), helper.m_nReleaseLine
        );
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] end "
    //     "-----------------------------",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    // L2_LOG_ERROR(Log,
    //     "[ORT Report][tid:%d][tick:%d][%d] end "
    //     "-----------------------------",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    L2_LOG_ERROR(
        Log, "%s object %u: reference report %d ends (worker %d, entry %lld).", itemType, static_cast<unsigned>(objectId),
        reportTick, threadId, static_cast<long long>(sequence) + 1
    );
}

} // namespace

CQuestItemDB g_QuestItemDB;
volatile bool g_bItemOrtType = false;

#define L2_DEFINE_DERIVED_ITEM_ALLOCATOR(ClassToken)                                                                        \
    void* ClassToken::operator new(std::size_t requestedSize) {                                                             \
        return AllocateDerivedItem<ClassToken>(requestedSize, L2_MEMORY_WIDEN(#ClassToken), #ClassToken);                   \
    }

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CAccessary)

CAccessary::CAccessary(bool createFail) : CItem(createFail) {}

CAccessary::CAccessary(const CAccessary& other) : CItem(other) {}

L2_DEFINE_OBJECT_RELEASE(CAccessary)

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CAccessary_ORT)

CAccessary_ORT::CAccessary_ORT(const CAccessary& other) : CAccessary(other) {}

MemoryObject* CAccessary_ORT::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_nAddLine = callerLine;
    helper.m_pAddCaller = callerFile;
    return this;
}

std::int32_t CAccessary_ORT::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "CAccessary object %u: reference category '%s' (%d) fell to %d at %s:%d; reporting references before "
            "total-reference release.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(type),
            subtypeRemaining, callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "CAccessary object %u: category '%s' has %d references; previous add %s:%d, previous release %s:%d.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            helper.m_pAddCaller ? runtime::logging::SourceFileName(helper.m_pAddCaller).data() : "<unknown>",
            helper.m_nAddLine,
            helper.m_pReleaseCaller ? runtime::logging::SourceFileName(helper.m_pReleaseCaller).data() : "<unknown>",
            helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kItemOrtReportFile, 0x0e);
    } else {
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
    }

    return ReleaseAs(L2_MEMORY_CLASS_ID(CAccessary_ORT), "CAccessary_ORT", kItemOrtReportFile, 0x0e, callerFile, callerLine);
}

void CAccessary_ORT::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local int totalCall = 0;
    ++totalCall;
    // ReportItemOrt(
    //     m_RefHelpers,
    //     totalCall,
    //     callerFile,
    //     callerLine)
    ReportItemOrt(m_RefHelpers, totalCall, callerFile, callerLine, "CAccessary", m_nId);
}

CObject* CAccessary::Copy() {
    L2_GUARD_FUNCTION();
    CAccessary* const item = !g_bItemOrtType ? new CAccessary(*this) : new CAccessary_ORT(*this);
    item->OnCreate();
    return item;
}

bool CAccessary::IsAccessary() const { return true; }

bool CAccessary::IsArtifact() const {
    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & m_data->m_Slot) == 0) {
        ++slot;
    }
    return slot >= ST_ARTIFACT_A1 && slot <= ST_ARTIFACT_D3;
}

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CAsset)

CAsset::CAsset(bool createFail) : CItem(createFail) {}

CAsset::CAsset(const CAsset& other) : CItem(other) {}

L2_DEFINE_OBJECT_RELEASE(CAsset)

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CAsset_ORT)

CAsset_ORT::CAsset_ORT(const CAsset& other) : CAsset(other) {}

MemoryObject* CAsset_ORT::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_nAddLine = callerLine;
    helper.m_pAddCaller = callerFile;
    return this;
}

std::int32_t CAsset_ORT::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "CAsset object %u: reference category '%s' (%d) fell to %d at %s:%d; reporting references before "
            "total-reference release.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(type),
            subtypeRemaining, callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "CAsset object %u: category '%s' has %d references; previous add %s:%d, previous release %s:%d.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            helper.m_pAddCaller ? runtime::logging::SourceFileName(helper.m_pAddCaller).data() : "<unknown>",
            helper.m_nAddLine,
            helper.m_pReleaseCaller ? runtime::logging::SourceFileName(helper.m_pReleaseCaller).data() : "<unknown>",
            helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kItemOrtReportFile, 0x0f);
    } else {
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
    }

    return ReleaseAs(L2_MEMORY_CLASS_ID(CAsset_ORT), "CAsset_ORT", kItemOrtReportFile, 0x0f, callerFile, callerLine);
}

void CAsset_ORT::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local int totalCall = 0;
    ++totalCall;
    // ReportItemOrt(
    //     m_RefHelpers,
    //     totalCall,
    //     callerFile,
    //     callerLine)
    ReportItemOrt(m_RefHelpers, totalCall, callerFile, callerLine, "CAsset", m_nId);
}

CObject* CAsset::Copy() {
    L2_GUARD_FUNCTION();
    CAsset* const item = !g_bItemOrtType ? new CAsset(*this) : new CAsset_ORT(*this);
    item->OnCreate();
    return item;
}

bool CAsset::IsAsset() const { return true; }

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CArmor)

CArmor::CArmor(bool createFail) : CItem(createFail) {}

CArmor::CArmor(const CArmor& other) : CItem(other) {}

L2_DEFINE_OBJECT_RELEASE(CArmor)

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CArmor_ORT)

CArmor_ORT::CArmor_ORT(const CArmor& other) : CArmor(other) {}

MemoryObject* CArmor_ORT::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_nAddLine = callerLine;
    helper.m_pAddCaller = callerFile;
    return this;
}

std::int32_t CArmor_ORT::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "CArmor object %u: reference category '%s' (%d) fell to %d at %s:%d; reporting references before "
            "total-reference release.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(type),
            subtypeRemaining, callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "CArmor object %u: category '%s' has %d references; previous add %s:%d, previous release %s:%d.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            helper.m_pAddCaller ? runtime::logging::SourceFileName(helper.m_pAddCaller).data() : "<unknown>",
            helper.m_nAddLine,
            helper.m_pReleaseCaller ? runtime::logging::SourceFileName(helper.m_pReleaseCaller).data() : "<unknown>",
            helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kItemOrtReportFile, 0x10);
    } else {
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
    }

    return ReleaseAs(L2_MEMORY_CLASS_ID(CArmor_ORT), "CArmor_ORT", kItemOrtReportFile, 0x10, callerFile, callerLine);
}

void CArmor_ORT::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local int totalCall = 0;
    ++totalCall;
    // ReportItemOrt(
    //     m_RefHelpers,
    //     totalCall,
    //     callerFile,
    //     callerLine)
    ReportItemOrt(m_RefHelpers, totalCall, callerFile, callerLine, "CArmor", m_nId);
}

CObject* CArmor::Copy() {
    L2_GUARD_FUNCTION();
    CArmor* const item = !g_bItemOrtType ? new CArmor(*this) : new CArmor_ORT(*this);
    item->OnCreate();
    return item;
}

bool CArmor::IsArmor() const { return true; }

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CEtcItem)

CEtcItem::CEtcItem(bool createFail) : CItem(createFail), m_bPetCollarTradable(true), m_targetExtentPadding{} {}

CEtcItem::CEtcItem(const CEtcItem& other) :
    CItem(other), m_bPetCollarTradable(other.m_bPetCollarTradable), m_targetExtentPadding{} {}

L2_DEFINE_OBJECT_RELEASE(CEtcItem)

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CEtcItem_ORT)

CEtcItem_ORT::CEtcItem_ORT(const CEtcItem& other) : CEtcItem(other) {}

MemoryObject* CEtcItem_ORT::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_pAddCaller = callerFile;
    helper.m_nAddLine = callerLine;
    return this;
}

std::int32_t CEtcItem_ORT::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "CEtcItem object %u: reference category '%s' (%d) fell to %d at %s:%d; reporting references before "
            "total-reference release.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(type),
            subtypeRemaining, callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "CEtcItem object %u: category '%s' has %d references; previous add %s:%d, previous release %s:%d.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            helper.m_pAddCaller ? runtime::logging::SourceFileName(helper.m_pAddCaller).data() : "<unknown>",
            helper.m_nAddLine,
            helper.m_pReleaseCaller ? runtime::logging::SourceFileName(helper.m_pReleaseCaller).data() : "<unknown>",
            helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kItemOrtReportFile, 0x11);
    } else {
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
    }

    return ReleaseAs(L2_MEMORY_CLASS_ID(CEtcItem_ORT), "CEtcItem_ORT", kItemOrtReportFile, 0x11, callerFile, callerLine);
}

void CEtcItem_ORT::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local int totalCall = 0;
    ++totalCall;
    // ReportItemOrt(
    //     m_RefHelpers,
    //     totalCall,
    //     callerFile,
    //     callerLine)
    ReportItemOrt(m_RefHelpers, totalCall, callerFile, callerLine, "CEtcItem", m_nId);
}

CObject* CEtcItem::Copy() {
    L2_GUARD_FUNCTION();
    CEtcItem* const item = !g_bItemOrtType ? new CEtcItem(*this) : new CEtcItem_ORT(*this);
    item->OnCreate();
    return item;
}

bool CEtcItem::IsEtcItem() const { return true; }

CEtcItem* CEtcItem::CastEtcItem() { return this; }

bool CEtcItem::IsAttribution() { return m_const->m_nAttribution > 0 && m_bPetCollarTradable; }

bool CEtcItem::IsNPCTrade() { return m_const->m_bIsNPCTrade && m_bPetCollarTradable; }

bool CEtcItem::IsTrade() { return m_const->m_bIsTrade && m_bPetCollarTradable; }

bool CEtcItem::IsDrop() { return m_const->m_bIsDrop && m_bPetCollarTradable; }

bool CEtcItem::IsDestruct() { return m_const->m_bIsDestruct && m_bPetCollarTradable; }

bool CEtcItem::IsPetCollarItem() { return m_const != nullptr && m_const->m_EtcitemType == 7; }

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CQuestItem)

CQuestItem::CQuestItem(bool createFail) : CItem(createFail) {}

CQuestItem::CQuestItem(const CQuestItem& other) : CItem(other) {}

L2_DEFINE_OBJECT_RELEASE(CQuestItem)

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CQuestItem_ORT)

CQuestItem_ORT::CQuestItem_ORT(const CQuestItem& other) : CQuestItem(other) {}

MemoryObject* CQuestItem_ORT::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_nAddLine = callerLine;
    helper.m_pAddCaller = callerFile;
    return this;
}

std::int32_t CQuestItem_ORT::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "CQuestItem object %u: reference category '%s' (%d) fell to %d at %s:%d; reporting references before "
            "total-reference release.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(type),
            subtypeRemaining, callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "CQuestItem object %u: category '%s' has %d references; previous add %s:%d, previous release %s:%d.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            helper.m_pAddCaller ? runtime::logging::SourceFileName(helper.m_pAddCaller).data() : "<unknown>",
            helper.m_nAddLine,
            helper.m_pReleaseCaller ? runtime::logging::SourceFileName(helper.m_pReleaseCaller).data() : "<unknown>",
            helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kItemOrtReportFile, 0x12);
    } else {
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
    }

    return ReleaseAs(L2_MEMORY_CLASS_ID(CQuestItem_ORT), "CQuestItem_ORT", kItemOrtReportFile, 0x12, callerFile, callerLine);
}

void CQuestItem_ORT::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local int totalCall = 0;
    ++totalCall;
    // ReportItemOrt(
    //     m_RefHelpers,
    //     totalCall,
    //     callerFile,
    //     callerLine)
    ReportItemOrt(m_RefHelpers, totalCall, callerFile, callerLine, "CQuestItem", m_nId);
}

CObject* CQuestItem::Copy() {
    L2_GUARD_FUNCTION();
    CQuestItem* const item = !g_bItemOrtType ? new CQuestItem(*this) : new CQuestItem_ORT(*this);
    item->OnCreate();
    return item;
}

bool CQuestItem::IsQuestItem() const { return true; }

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CWeapon)

CWeapon::CWeapon(bool createFail) : CItem(createFail) {}

CWeapon::CWeapon(const CWeapon& other) : CItem(other) {}

L2_DEFINE_OBJECT_RELEASE(CWeapon)

L2_DEFINE_DERIVED_ITEM_ALLOCATOR(CWeapon_ORT)

CWeapon_ORT::CWeapon_ORT(const CWeapon& other) : CWeapon(other) {}

MemoryObject* CWeapon_ORT::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_nAddLine = callerLine;
    helper.m_pAddCaller = callerFile;
    return this;
}

std::int32_t CWeapon_ORT::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "CWeapon object %u: reference category '%s' (%d) fell to %d at %s:%d; reporting references before "
            "total-reference release.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(type),
            subtypeRemaining, callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "CWeapon object %u: category '%s' has %d references; previous add %s:%d, previous release %s:%d.",
            static_cast<unsigned>(m_nId), WideToUtf8(ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            helper.m_pAddCaller ? runtime::logging::SourceFileName(helper.m_pAddCaller).data() : "<unknown>",
            helper.m_nAddLine,
            helper.m_pReleaseCaller ? runtime::logging::SourceFileName(helper.m_pReleaseCaller).data() : "<unknown>",
            helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kItemOrtReportFile, 0x13);
    } else {
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
    }

    return ReleaseAs(L2_MEMORY_CLASS_ID(CWeapon_ORT), "CWeapon_ORT", kItemOrtReportFile, 0x13, callerFile, callerLine);
}

void CWeapon_ORT::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local int totalCall = 0;
    ++totalCall;
    // ReportItemOrt(
    //     m_RefHelpers,
    //     totalCall,
    //     callerFile,
    //     callerLine)
    ReportItemOrt(m_RefHelpers, totalCall, callerFile, callerLine, "CWeapon", m_nId);
}

CObject* CWeapon::Copy() {
    L2_GUARD_FUNCTION();
    CWeapon* const item = !g_bItemOrtType ? new CWeapon(*this) : new CWeapon_ORT(*this);
    item->OnCreate();
    return item;
}

bool CWeapon::IsWeapon() const { return true; }

void CQuestItemDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\quest_pch2.txt";
    std::FILE* input = nullptr;
    const errno_t openError = _wfopen_s(&input, runtime::ResolveResourcePath(kFileName).wstring().c_str(), L"rb");
#else
    constexpr const wchar_t* kFileName = L"../script/quest_pch2.txt";
    errno = 0;
    std::FILE* input = std::fopen(runtime::ResolveResourcePath("../script/quest_pch2.txt").string().c_str(), "rb");
    const int openError = input ? 0 : errno;
#endif

    // Log.Add(LOG_NORMAL, L"Load QuestItem")
    // L2_LOG_INFO(Log, L"Load QuestItem")
    L2_LOG_INFO(Log, "Loading quest item mappings from quest_pch2.txt.");
    if (!input || openError != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't find %s errno[%d]",
        //     kFileName,
        //     openError)
        // L2_LOG_ERROR(Log,
        //     L"Can't find %s errno[%d]",
        //     kFileName,
        //     openError)
        L2_LOG_ERROR(Log, "Cannot open %s (errno %d); quest item loading stops.", WideToUtf8(kFileName).c_str(), openError);
        return;
    }

    GetChar(input);
    while (std::feof(input) == 0) {
        const int questId = GetIntEx(input);
        const int itemCount = GetIntEx(input);
        if (std::feof(input) != 0) {
            break;
        }

        std::vector<int> itemIds;
        for (int index = 0; index < itemCount; ++index) {
            const int itemId = GetIntEx(input);
            if (std::feof(input) != 0) {
                break;
            }
            itemIds.push_back(itemId);
        }
        m_mapQuestItem[questId] = itemIds;
    }

    std::fclose(input);
}

bool CQuestItemDB::DestroyQuestItem(User* const user, const int questId, const bool force) {
    L2_GUARD_FUNCTION();

    const auto quest = m_mapQuestItem.find(questId);
    if (quest == m_mapQuestItem.end()) {
        return true;
    }

    for (const int itemClassId : quest->second) {
        ItemPointerList items;
        items.reserve(12);
        {
            L2_READ_GUARD(user->m_inv);
            user->m_inv.GetItemListByClassID(items, itemClassId, -1, -1, true);
        }

        if (!items.empty() && items[0]->IsPetCollarItem()) {
            if (items[0]->IsPetCollarItem() && !items.empty()) {
                for (const CSPointer<CItem>& item : items) {
                    if (!force) {
                        CEtcItem* const petCollar = item->CastEtcItem();
                        if (!petCollar->m_bPetCollarTradable) {
                            user->SendSystemMessage(SystemMessageId::CANNOT_DESTROY_SUMMONING_ITEM_WHILE_PET_SUMMONED);
                            return false;
                        }
                    }
                }
            }
        }
    }

    for (const int itemClassId : quest->second) {
        ItemPointerList items;
        items.reserve(12);
        {
            L2_READ_GUARD(user->m_inv);
            user->m_inv.GetItemListByClassID(items, itemClassId, -1, -1, true);
        }

        if (items.empty()) {
            continue;
        }

        if (!force && items[0]->IsPetCollarItem()) {
            for (const CSPointer<CItem>& item : items) {
                CPet::DestroyPet(user, item->CastEtcItem());
            }
            continue;
        }

        CItem* const first = items[0].get();
        const std::int64_t count =
            first->m_data->IsStackable() ? first->m_data->m_nCount : static_cast<std::int64_t>(items.size());
        user->DeleteItemInInventory(first->m_nClassId, count, nullptr, true, true);
    }

    return true;
}

void LoadQuestItem() { g_QuestItemDB.Load(); }

#undef L2_DEFINE_DERIVED_ITEM_ALLOCATOR
