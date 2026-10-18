#include "SequenceJob.h"

#include "DB.h"
#include "DBSocket.h"
#include "Portable/ProcessRuntime.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/LegacyCrtRandom.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace {

// constexpr wchar_t kSequenceJobSourceWide[] = L"SequenceJob.cpp"

std::wstring QueueNameForLog(const std::u16string& name) {
    return ::Utf16ToWide(std::u16string_view(name.data(), name.size()));
}


} // namespace

CSequenceJobQueue::CSequenceJobQueue(const char16_t* const name, const int queueSize) :
    strName_(name),
    nQueueSize_(static_cast<std::uint64_t>(static_cast<std::int64_t>(queueSize))),
    nFirst_(0),
    nLast_(0),
    nCount_(0),
    nDoingJob_(0),
    nReciveCurrentCount_(0),
    pJobArray_(nullptr) {
    if (nQueueSize_ != 0) {
        const std::size_t allocationSize = static_cast<std::size_t>(nQueueSize_ << 3U);
        pJobArray_ = static_cast<CSequenceJobObject**>(g_Memory.Malloc(allocationSize, true, nullptr, 0));
        std::memset(pJobArray_, 0, allocationSize);
        nJobKey_ = Singleton<CSequenceJobManager>::inst().Insert(this);
        if (nJobKey_ == 0) {
            runtime::raw_atomic::ExchangeI32(&nDoingJob_, -1);
        }
    }
}

CSequenceJobQueue::~CSequenceJobQueue() {
    L2_GUARD_FUNCTION();

    if (nQueueSize_ != 0) {
        const std::int32_t previousDoingJob = runtime::raw_atomic::ExchangeI32(&nDoingJob_, -1);
        for (std::int32_t index = 0; static_cast<std::uint64_t>(static_cast<std::int64_t>(index)) < nQueueSize_; ++index) {
            CSequenceJobObject* const job = pJobArray_[static_cast<std::size_t>(index)];
            if (job != nullptr) {
                L2_RELEASE(job, ObjectRefType::CreateDelete);
            }
        }
        g_Memory.Free(pJobArray_, 0);

        CSequenceJobManager& manager = Singleton<CSequenceJobManager>::inst();
        if (previousDoingJob == 1) {
            manager.Locked(nJobKey_);
        } else {
            manager.Remove(nJobKey_);
        }
    }
}

void CSequenceJobQueue::DoJob() {
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::CompareExchangeI32(&nDoingJob_, 1, 0) != 0) {
        return;
    }

    const std::int64_t first = runtime::raw_atomic::AddI64(&nFirst_, 1);
    const std::uint64_t index = (nQueueSize_ - 1U) & static_cast<std::uint64_t>(first);
    CSequenceJobObject* const job = pJobArray_[static_cast<std::size_t>(index)];
    if (job->nTimeoutMs_ != -1) {
        AddTimer(static_cast<std::int64_t>(job->nTimeoutMs_), static_cast<std::int32_t>(static_cast<std::uint32_t>(index)));
    }
    runtime::raw_atomic::ExchangeI32(&nReciveCurrentCount_, 0);

    runtime::process::SetCurrentWorldId(CThreadManager::GetThreadID() < 16 ? job->nWorldId_ : g_nWorldId);
    CDB::CWorldSetter worldSetter;
    db.m_pDBSockets[::LegacyCrtRand() & 1]->Send("chdb", 0xea, 0x1c4, nJobKey_, job->nBufLen_, job->Buffer_);
}

void CSequenceJobQueue::Receive(CDBSocket* const socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    const std::uint64_t index =
        (nQueueSize_ - 1U) & static_cast<std::uint64_t>(runtime::raw_atomic::LoadI64Relaxed(&nFirst_));
    if (packet == nullptr) {
        std::uint16_t requestProtocol = 0;
        ::Disassemble(
            reinterpret_cast<const unsigned char*>(pJobArray_[static_cast<std::size_t>(index)]->Buffer_), "h",
            &requestProtocol
        );
        const std::wstring queueName = QueueNameForLog(strName_);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CSequenceJobQueue[%s] RQ_EX[%d] Timeout",
        //     kSequenceJobSourceWide,
        //     0x9e,
        //     queueName.c_str(),
        //     static_cast<int>(requestProtocol))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CSequenceJobQueue[%s] RQ_EX[%d] Timeout",
        //     kSequenceJobSourceWide,
        //     0x9e,
        //     queueName.c_str(),
        //     static_cast<int>(requestProtocol))
        L2_LOG_ERROR(
            Log,
            "Database sequence queue '%s' (key %d) timed out on request opcode %u; the current job is released and the "
            "queue advances.",
            WideToUtf8(queueName).c_str(), nJobKey_, static_cast<unsigned>(requestProtocol)
        );
        runtime::raw_atomic::ExchangeI32(&nDoingJob_, 0);
    } else {
        std::int32_t totalReplyCount = 0;
        std::int32_t packetCount = 0;
        const unsigned char* cursor = ::Disassemble(packet, "d", &totalReplyCount);
        cursor = ::Disassemble(cursor, "d", &packetCount);

        for (std::int32_t packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
            std::int32_t packetSize = 0;
            cursor = ::Disassemble(cursor, "d", &packetSize);
            if (packetSize != 0) {
                std::uint16_t protocolLane = 0;
                cursor = ::Disassemble(cursor, "h", &protocolLane);
                const std::uint8_t protocol = static_cast<std::uint8_t>(protocolLane);
                if (protocol < CDBCallbackMap::SIZE) {
                    CDBCallback const callback = gPacketTableFromCache.GetCallback(protocol);
                    if (callback(socket, cursor)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] RP_Protocol[%d] callback return true!!",
                        //     kSequenceJobSourceWide,
                        //     0xbe,
                        //     static_cast<int>(protocol))
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d] RP_Protocol[%d] callback return true!!",
                        //     kSequenceJobSourceWide,
                        //     0xbe,
                        //     static_cast<int>(protocol))
                        L2_LOG_ERROR(
                            Log,
                            "Database sequence reply opcode %u returned true from its callback at packet %lld of %d; "
                            "remaining callbacks in this reply are skipped.",
                            static_cast<unsigned>(protocol), static_cast<long long>(packetIndex) + 1, packetCount
                        );
                        break;
                    }
                }
                cursor += static_cast<std::ptrdiff_t>(packetSize) - 1;
            }
        }

        const std::int32_t receivedCount = runtime::raw_atomic::AddI32(&nReciveCurrentCount_, packetCount);
        if (receivedCount == totalReplyCount) {
            runtime::raw_atomic::ExchangeI32(&nDoingJob_, 0);
        }
    }

    if (runtime::raw_atomic::LoadI32Relaxed(&nDoingJob_) == 0) {
        CSequenceJobObject* const job = pJobArray_[static_cast<std::size_t>(index)];
        L2_RELEASE(job, ObjectRefType::CreateDelete);
        pJobArray_[static_cast<std::size_t>(index)] = nullptr;

        const std::int32_t count = runtime::raw_atomic::DecrementI32(&nCount_);
        if (count < 1) {
            if (count < 0) {
                const std::wstring queueName = QueueNameForLog(strName_);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CSequenceJobQueue[%s] Under count [%d]",
                //     kSequenceJobSourceWide,
                //     0xd3,
                //     queueName.c_str(),
                //     count)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] CSequenceJobQueue[%s] Under count [%d]",
                //     kSequenceJobSourceWide,
                //     0xd3,
                //     queueName.c_str(),
                //     count)
                L2_LOG_ERROR(
                    Log,
                    "Database sequence queue '%s' (key %d) has negative job count %d after release; no next job is started.",
                    WideToUtf8(queueName).c_str(), nJobKey_, count
                );
            }
        } else {
            DoJob();
        }
    }
}

void CSequenceJobQueue::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::LoadI32Relaxed(&nDoingJob_) == 1 &&
        static_cast<std::int64_t>(id) == runtime::raw_atomic::LoadI64Relaxed(&nFirst_)) {
        Receive(nullptr, nullptr);
    }
}

CSequenceJobManager::CSequenceJobManager(const int arraySize) :
    nArraySize_(arraySize),
    nCount_(0),
    nFirst_(0),
    nLast_(0),
    pFreeIndexArray_(nullptr),
    pQueueArray_(nullptr) {
    if (nArraySize_ > 0) {
        pQueueArray_ = static_cast<CSequenceJobQueue* volatile*>(
            g_Memory.Malloc(static_cast<std::size_t>(nArraySize_) * sizeof(CSequenceJobQueue*), true, nullptr, 0)
        );
        pFreeIndexArray_ = static_cast<volatile std::int32_t*>(
            g_Memory.Malloc(static_cast<std::size_t>(nArraySize_) * sizeof(std::int32_t), true, nullptr, 0)
        );
        for (std::int32_t index = 0; index < nArraySize_; ++index) {
            runtime::raw_atomic::ExchangeI32(&pFreeIndexArray_[index], index);
            runtime::raw_atomic::ExchangePointer(&pQueueArray_[index], static_cast<CSequenceJobQueue*>(nullptr));
        }
        runtime::raw_atomic::ExchangeI64(&nFirst_, 0);
        runtime::raw_atomic::ExchangeI64(&nLast_, static_cast<std::int64_t>(nArraySize_ - 1));
        pLockedQueue_ = new CSequenceJobQueue(u"LockedQueue", 0);
    }
}

CSequenceJobManager::~CSequenceJobManager() {
    if (nArraySize_ > 0) {
        g_Memory.Free(const_cast<CSequenceJobQueue**>(pQueueArray_), 0);
    }
}

CSequenceJobQueue* CSequenceJobManager::Find(const int jobKey) {
    L2_GUARD_FUNCTION();

    if (jobKey < 0 || nArraySize_ < jobKey) {
        return nullptr;
    }
    return runtime::raw_atomic::LoadPointerRelaxed(&pQueueArray_[jobKey]);
}

int CSequenceJobManager::Insert(CSequenceJobQueue* const queue) {
    L2_GUARD_FUNCTION();

    const std::int32_t count = runtime::raw_atomic::IncrementI32(&nCount_);
    if (nArraySize_ < count) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CSequenceJobManager over array size [%d]",
        //     kSequenceJobSourceWide,
        //     0x10a,
        //     nArraySize_)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CSequenceJobManager over array size [%d]",
        //     kSequenceJobSourceWide,
        //     0x10a,
        //     nArraySize_)
        L2_LOG_ERROR(
            Log, "Cannot register database sequence queue: count %d exceeds capacity %d; returning key 0.", count,
            nArraySize_
        );
        return 0;
    }

    const std::int64_t first = runtime::raw_atomic::AddI64(&nFirst_, 1);
    const std::uint64_t firstBits = static_cast<std::uint64_t>(first);
    const std::uint64_t lastBits = static_cast<std::uint64_t>(runtime::raw_atomic::LoadI64Relaxed(&nLast_));
    const std::uint64_t sizeBits = static_cast<std::uint64_t>(static_cast<std::int64_t>(nArraySize_));
    if (!(firstBits <= lastBits || sizeBits + firstBits <= lastBits + sizeBits)) {
        return 0;
    }

    const std::uint64_t index = static_cast<std::uint64_t>(static_cast<std::int64_t>(nArraySize_ - 1)) & firstBits;
    std::int32_t freeId = runtime::raw_atomic::ExchangeI32(&pFreeIndexArray_[index], 0);
    while (freeId == 0) {
        std::this_thread::yield();
        freeId = runtime::raw_atomic::ExchangeI32(&pFreeIndexArray_[index], 0);
    }
    runtime::raw_atomic::ExchangePointer(&pQueueArray_[freeId], queue);
    return freeId;
}

void CSequenceJobManager::Remove(const int jobKey) {
    L2_GUARD_FUNCTION();

    runtime::raw_atomic::ExchangePointer(&pQueueArray_[jobKey], static_cast<CSequenceJobQueue*>(nullptr));
    const std::int64_t last = runtime::raw_atomic::AddI64(&nLast_, 1);
    const std::uint64_t index =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(nArraySize_ - 1)) & static_cast<std::uint64_t>(last);

    std::int32_t expected = 0;
    for (;;) {
        const std::int32_t observed = runtime::raw_atomic::CompareExchangeI32(&pFreeIndexArray_[index], jobKey, expected);
        const bool exchanged = expected == observed;
        expected = observed;
        if (!exchanged) {
            break;
        }
        std::this_thread::yield();
    }
    runtime::raw_atomic::DecrementI32(&nCount_);
}

void CSequenceJobManager::Receive(const int jobKey, CDBSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();

    CSequenceJobQueue* const queue = Find(jobKey);
    if (queue != nullptr) {
        if (queue == pLockedQueue_) {
            Remove(jobKey);
        } else {
            queue->Receive(socket, packet);
        }
    }
}
