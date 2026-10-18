#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>

struct MonObj {
    std::u16string strName;
    std::atomic<std::int32_t> iValue{0};
};

class CStateMonMgr {
public:
    CStateMonMgr();
    ~CStateMonMgr();

    CStateMonMgr(const CStateMonMgr&) = delete;
    CStateMonMgr& operator=(const CStateMonMgr&) = delete;

    void SetDirectory(const char16_t* directory);
    const char16_t* GetStatusSummary();
    void MakeReport();
    void Register(MonObj* object);

    static CStateMonMgr* g_pStateMonMgr;

    std::atomic<std::int32_t> threadCount{0};
    std::atomic<std::int32_t> ioThreadCount{0};
    std::atomic<std::int32_t> socketCount{0};
    std::atomic<std::int32_t> acceptCallCount{0};
    std::atomic<std::int32_t> clientPacketPendingCount{0};
    std::atomic<std::int32_t> authPacketPendingCount{0};

private:
    std::filesystem::path m_logDirectory;
    std::u16string m_strSummary;
    std::list<MonObj*> m_mapMonObjs;
    std::mutex m_monMutex;
};
