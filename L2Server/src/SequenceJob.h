#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

class CDBSocket;
class CSequenceJobQueue;

// Current CSequenceJobQueue::DoJob and Receive expose this complete field
// carrier. The current image emits no constructor or concrete vtable for
// CSequenceJobObject, so this slice publishes only the typed
// layout consumed by the queue.
class CSequenceJobObject : public MemoryObject {
public:
    std::int32_t nWorldId_;
    std::int32_t nBufLen_;
    char Buffer_[0x5000];
    std::int32_t nTimeoutMs_;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CSequenceJobObject)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CSequenceJobObject)
};

class CSequenceJobQueue : public CIOObject {
public:
    CSequenceJobQueue(const char16_t* name, int queueSize);
    ~CSequenceJobQueue() override;

    // This source surface is present in the exact type structure, but no
    // current caller/body is compiled. Do not manufacture an enqueue path.
    bool Push(CSequenceJobObject* job, int timeoutMilliseconds);

    void TimerExpired(int id) override;

private:
    void DoJob();
    void Receive(CDBSocket* socket, const unsigned char* packet);

    std::u16string strName_;
    std::int32_t nJobKey_;
    std::uint64_t nQueueSize_;
    volatile std::int64_t nFirst_;
    volatile std::int64_t nLast_;
    volatile std::int32_t nCount_;
    volatile std::int32_t nDoingJob_;
    volatile std::int32_t nReciveCurrentCount_;
    CSequenceJobObject** pJobArray_;

    friend class CSequenceJobManager;
};

class CSequenceJobManager : public Singleton<CSequenceJobManager> {
public:
    explicit CSequenceJobManager(int arraySize = 10000);
    ~CSequenceJobManager() override;

    int Insert(CSequenceJobQueue* queue);
    void Locked(int jobKey) noexcept { runtime::raw_atomic::ExchangePointer(&pQueueArray_[jobKey], pLockedQueue_); }
    void Remove(int jobKey);
    CSequenceJobQueue* Find(int jobKey);
    void Receive(int jobKey, CDBSocket* socket, const unsigned char* packet);

private:
    std::int32_t nArraySize_;
    volatile std::int32_t nCount_;
    volatile std::int64_t nFirst_;
    volatile std::int64_t nLast_;
    CSequenceJobQueue* pLockedQueue_;
    volatile std::int32_t* pFreeIndexArray_;
    CSequenceJobQueue* volatile* pQueueArray_;

    friend class CSequenceJobQueue;
};
