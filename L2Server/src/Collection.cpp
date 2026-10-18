#include "Collection.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

constexpr const char* kCollectionSource = "Collection.cpp";
constexpr int kCollectionReportLine = 4;

} // namespace

CCollection::CCollection() : m_lBeingCollected(0), m_alignment3d7cTo3d80{} {}

CCollection::CCollection(const CCollection& other) : CNPC(other), m_lBeingCollected(0), m_alignment3d7cTo3d80{} {
    L2_GUARD_FUNCTION();
}

CCollection::~CCollection() = default;

L2_DEFINE_OBJECT_CLASS_ID(CCollection)

L2_DEFINE_OBJECT_CLASS_NAME(CCollection, m_pClassName_CCollection)

L2_DEFINE_OBJECT_ALLOCATOR(CCollection, __GetClassName())

L2_DEFINE_OBJECT_NEW(CCollection)

std::int32_t CCollection::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    helper.m_pReleaseCaller = callerFile;
    helper.m_nReleaseLine = callerLine;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] "
        //     "is below zero!",
        //     callerFile ? callerFile : "",
        //     callerLine,
        //     typeIndex,
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] "
        //     "is below zero!",
        //     callerFile ? callerFile : "",
        //     callerLine,
        //     typeIndex,
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log, "Collection object %u: reference category %zu fell to %d at %s:%d; total-reference release continues.",
            static_cast<unsigned>(m_nId), typeIndex, subtypeRemaining,
            callerFile != nullptr ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kCollectionSource, kCollectionReportLine);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CCollection();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CCollection", callerFile, callerLine);
            ReportOrt(kCollectionSource, kCollectionReportLine);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CCollection));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CCollection", MemoryObjectID(), callerFile, callerLine);
        ReportOrt(kCollectionSource, kCollectionReportLine);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CCollection::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();

    const int threadId = CThreadManager::GetThreadID();
    static thread_local std::int32_t reportTick = 0;
    ++reportTick;
    int sequence = 1;
    if (callerFile) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        L2_LOG_ERROR(
            Log, "Collection object %u: reference report %d begins (worker %d, entry %d), requested at %s:%d.",
            static_cast<unsigned>(m_nId), reportTick, threadId, sequence,
            runtime::logging::SourceFileName(callerFile).data(), callerLine
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId,
        //     reportTick,
        //     sequence)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId,
        //     reportTick,
        //     sequence)
        L2_LOG_ERROR(
            Log, "Collection object %u: reference report %d begins (worker %d, entry %d); caller unknown.",
            static_cast<unsigned>(m_nId), reportTick, threadId, sequence
        );
    }

    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        const std::int32_t count = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (!helper.m_pAddCaller || !helper.m_pReleaseCaller || count == 0) {
            continue;
        }
        ++sequence;
        const std::string typeName = WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index)));
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] "
        //     "[RefCount:%d]-[%s] last_add[%s][%d] "
        //     "last_release[%s][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     count,
        //     typeName.c_str(),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] "
        //     "[RefCount:%d]-[%s] last_add[%s][%d] "
        //     "last_release[%s][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     count,
        //     typeName.c_str(),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "Collection object %u: reference report %d, entry %d (worker %d): category '%s' has %d references; last add "
            "%s:%d, last release %s:%d.",
            static_cast<unsigned>(m_nId), reportTick, sequence, threadId, typeName.c_str(), count,
            runtime::logging::SourceFileName(helper.m_pAddCaller).data(), helper.m_nAddLine,
            runtime::logging::SourceFileName(helper.m_pReleaseCaller).data(), helper.m_nReleaseLine
        );
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    // L2_LOG_ERROR(Log,
    //     "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    L2_LOG_ERROR(
        Log, "Collection object %u: reference report %d ends (worker %d, entry %d).", static_cast<unsigned>(m_nId),
        reportTick, threadId, sequence + 1
    );
}

CObject* CCollection::Copy() {
    L2_GUARD_FUNCTION();
    CCollection* const collection = new CCollection(*this);
    collection->OnCreate();
    return collection;
}

bool CCollection::IsCollection() const { return true; }

CCollection* CCollection::CastCollection() { return this; }

bool CCollection::SetBeingCollected() { return runtime::raw_atomic::CompareExchangeI32(&m_lBeingCollected, 1, 0) == 0; }

void CCollection::ResetBeingCollected() { runtime::raw_atomic::ExchangeI32(&m_lBeingCollected, 0); }

bool CCollection::CanBeCollected() const { return runtime::raw_atomic::LoadI32Relaxed(&m_lBeingCollected) == 0; }
