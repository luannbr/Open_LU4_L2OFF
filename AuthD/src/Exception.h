#pragma once

#include "Platform/CriticalSection.h"
#include "Platform/WindowsTypes.h"

#include <cstdint>

#if !defined(_WIN32)
struct _CONTEXT;
struct _EXCEPTION_POINTERS;
#endif

// AuthD has its own Exception.obj implementation. These two one-byte
// classes and their static members are recorded from the
// later 64-bit Server/Main/Shared exception runtime used by L2NPC/CacheD.
// Original owner: Src/exception.cpp. L2Authd.pdb types 0x8E59 and 0x8EB9.
class CLogCritical {
public:
    CLogCritical();
    ~CLogCritical();

    static void Initialize();
    static void Deinitialize();

private:
    static authd::platform::CriticalSectionStorage32
        m_CriticalSection;
};


class CExceptionInit {
public:
    // `long` is 32 bits in the selected VS2013/x86 image. Spell the width
    // so LP64 hosts keep the recorded state contract.
    static std::int32_t g_timeStart;

    CExceptionInit();
    ~CExceptionInit();
};


#if defined(_WIN32)
using ExceptionFilterResult = long;
#  define AUTHD_EXCEPTION_STDCALL __stdcall
#else
using ExceptionFilterResult = std::int32_t;
#  define AUTHD_EXCEPTION_STDCALL
#endif

int exception_filter(
    _EXCEPTION_POINTERS* exceptionPointers);
void violation();
void ELOG(
    const char* format,
    ...);
void send_exception(bool terminateProcess);
void exception_init();

int getVideoDesc(
    char* description,
    int descriptionSize);
int GetLogicalAddress(
    const void* address,
    char* moduleName,
    std::uint32_t moduleNameSize,
    std::uint32_t& section,
    std::uint32_t& offset);
void ImageHelpStackWalk(
    void* logFile,
    _CONTEXT* context);
void IntelStackWalk(
    void* logFile,
    _CONTEXT* context);
ExceptionFilterResult AUTHD_EXCEPTION_STDCALL
RecordExceptionInfo(
    _EXCEPTION_POINTERS* exceptionPointers);
void SetUpExceptionFilter();
void ClearExceptionFilter();

// Exact external data symbols in L2Authd.pdb. The selected Exception.obj
// never assigns mailServer, so the legacy hard-coded crash-mail path remains
// dormant unless some future recovered owner proves an assignment.
extern const char* mailServer;
extern std::int32_t Terminating;
extern CExceptionInit theExceptionInit;

#undef AUTHD_EXCEPTION_STDCALL
