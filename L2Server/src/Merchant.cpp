#include "Merchant.h"
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

constexpr const char* kMerchantSource = "merchant.cpp";
constexpr int kMerchantReportLine = 9;

} // namespace

CMerchant::CMerchant() : m_bIsSeller(false), m_alignment3d79To3d80{} {}

CMerchant::CMerchant(const CMerchant& other) :
    CNPC(other),
    m_bIsSeller(other.m_bIsSeller),
    m_alignment3d79To3d80{},
    m_SellingGoods(other.m_SellingGoods),
    m_BuyingGoods(other.m_BuyingGoods) {
    L2_GUARD_FUNCTION();
}

CMerchant::~CMerchant() = default;

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CMerchant)

std::int32_t CMerchant::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
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
            Log,
            "Merchant object %u: reference category %zu fell to %d at %s:%d; reporting references before total-reference "
            "release.",
            static_cast<unsigned>(m_nId), typeIndex, subtypeRemaining,
            callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kMerchantSource, kMerchantReportLine);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CMerchant();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CMerchant", callerFile, callerLine);
            ReportOrt(kMerchantSource, kMerchantReportLine);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CMerchant));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CMerchant", MemoryObjectID(), callerFile, callerLine);
        ReportOrt(kMerchantSource, kMerchantReportLine);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CMerchant::ReportOrt(const char* callerFile, int callerLine) {
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
            Log, "Merchant object %u: reference report %d begins (worker %d, entry %d), requested at %s:%d.",
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
            Log, "Merchant object %u: reference report %d begins (worker %d, entry %d); caller unknown.",
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
            "Merchant object %u: reference report %d, entry %d (worker %d): category '%s' has %d references; last add "
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
        Log, "Merchant object %u: reference report %d ends (worker %d, entry %lld).", static_cast<unsigned>(m_nId),
        reportTick, threadId, static_cast<long long>(sequence) + 1
    );
}

CObject* CMerchant::Copy() {
    L2_GUARD_FUNCTION();
    CMerchant* const merchant = new CMerchant(*this);
    merchant->OnCreate();
    return merchant;
}

bool CMerchant::IsMerchant() { return true; }

CMerchant* CMerchant::CastMerchant() { return this; }

void CMerchant::AddSellingGood(GoodStruct* good) {
    L2_GUARD_FUNCTION();
    m_SellingGoods.push_back(good);
}

void CMerchant::AddBuyingGood(GoodStruct* good) {
    L2_GUARD_FUNCTION();
    m_BuyingGoods.push_back(good);
}
