#pragma once

#include <cstddef>
#include <cstdint>


// Exact 16-byte state image used by Shared/CYieldLock.
class CYieldLock {
public:
    CYieldLock();

    void Enter(const wchar_t* callerFile, int callerLine);
    void Leave(const wchar_t* callerFile, int callerLine);
    bool IsEntered() const;

private:
    volatile std::int32_t m_nLock = 0;
    std::int32_t m_nThreadID = -1;
    std::int32_t m_nUseCount = 0;
    std::int32_t m_nLog = 0;
};
