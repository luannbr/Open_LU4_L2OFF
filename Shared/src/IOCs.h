#pragma once

#include "CriticalSectionStorage.h"

// Exact global class from Shared/iocs.h. L2NPC.pdb records one protected
// _RTL_CRITICAL_SECTION member named m_CriticalSection at offset zero.
class CIOCriticalSection {
public:
    CIOCriticalSection();
    explicit CIOCriticalSection(unsigned long spinCount);
    ~CIOCriticalSection();

    CIOCriticalSection(const CIOCriticalSection&) = delete;
    CIOCriticalSection& operator=(const CIOCriticalSection&) = delete;

    void Enter(const wchar_t* pszFile, int nLine);
    void Leave(const wchar_t* pszFile, int nLine);
    void WriteLock(const wchar_t* pszFile, int nLine);
    void WriteUnlock();
    void ReadLock(const wchar_t* pszFile, int nLine);
    void ReadUnlock();

protected:
    l2portable_detail::CriticalSectionStorage m_CriticalSection;
};
