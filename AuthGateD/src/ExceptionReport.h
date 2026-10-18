#pragma once

#include <array>
#include <cstdint>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <DbgHelp.h>
#  define AUTHGATED_EXCEPTION_CALL __stdcall
#else
#  define AUTHGATED_EXCEPTION_CALL

// Host carriers for the selected x86 SEH records. They are used only at the
// portable boundary. The Win32 build uses the SDK's exact records above.
struct _EXCEPTION_RECORD {
    std::uint32_t ExceptionCode = 0;
    void* ExceptionAddress = nullptr;
};

struct _CONTEXT {
    std::uintptr_t instructionPointer = 0;
    std::uintptr_t stackPointer = 0;
    std::uintptr_t framePointer = 0;
};

struct _EXCEPTION_POINTERS {
    _EXCEPTION_RECORD* ExceptionRecord = nullptr;
    _CONTEXT* ContextRecord = nullptr;
};

struct _SYMBOL_INFO;
#endif

// This is the Microsoft CodeView BasicType enum used by the target. It is
// normally supplied by the DIA SDK's cvconst.h, not by DbgHelp.h itself, and
// MinGW does not ship that header.
enum BasicType : std::uint32_t {
    btNoType = 0,
    btVoid = 1,
    btChar = 2,
    btWChar = 3,
    btInt = 6,
    btUInt = 7,
    btFloat = 8,
    btBCD = 9,
    btBool = 10,
    btLong = 13,
    btULong = 14,
    btCurrency = 25,
    btDate = 26,
    btVariant = 27,
    btComplex = 28,
    btBit = 29,
    btBSTR = 30,
    btHresult = 31,
};

// AuthGateD's x86 PDB records _tagSTACKFRAME. DbgHelp exposes that exact
// carrier on x86, while its x64 API requires STACKFRAME64. The POSIX branch
// only needs an opaque callback context.
#if defined(_WIN32)
#  if defined(_M_IX86) || defined(__i386__)
using ExceptionReportStackFrame = STACKFRAME;
#  else
using ExceptionReportStackFrame = STACKFRAME64;
#  endif
#else
struct ExceptionReportStackFrame;
#endif

// Original owner: ExceptionReport.h / ExceptionReport.cpp. AuthGateD.pdb
// records this as a one-byte initializer object with entirely static report
// state. The method names and calling conventions below are the PDB contract.
class CExceptionReport {
public:
    using UserFunction = std::int32_t (
        AUTHGATED_EXCEPTION_CALL *)(void* reportFile);
    using ExceptionFilter = std::int32_t (
        AUTHGATED_EXCEPTION_CALL *)(
            _EXCEPTION_POINTERS* exceptionPointers);

    CExceptionReport();
    ~CExceptionReport();

    static void SetProgramName(const char16_t* programName);
    static int _tprintfile(const char16_t* format, ...);
    static std::int32_t AUTHGATED_EXCEPTION_CALL
        UnhandledExceptionFilterTwo(
            _EXCEPTION_POINTERS* exceptionPointers);
    static std::int32_t AUTHGATED_EXCEPTION_CALL
        UnhandledExceptionFilter(
            _EXCEPTION_POINTERS* exceptionPointers);

private:
    static char16_t* GetExceptionString(
        std::uint32_t exceptionCode);
    static int GetLogicalAddress(
        const void* address,
        char16_t* moduleName,
        std::uint32_t moduleNameSize,
        std::uint32_t& section,
        std::uint32_t& offset);
    static char16_t* FormatOutputValue(
        char16_t* output,
        BasicType basicType,
        std::uint64_t size,
        const void* value);
    static BasicType GetBasicType(
        std::uint32_t typeIndex,
        std::uint64_t moduleBase);
    static void WriteBasicInfo(
        _EXCEPTION_RECORD* exceptionRecord);
    static void WriteRegistersInfo(_CONTEXT* context);
    static char16_t* DumpTypeIndex(
        char16_t* output,
        std::uint64_t moduleBase,
        std::uint32_t typeIndex,
        std::uint32_t nesting,
        std::uint32_t address,
        int& expanded);
    static void Dump(
        std::uint64_t address,
        std::uint32_t byteCount,
        int alignAddress);
    static void WriteMemoryDump(_CONTEXT* context);
    static int FormatSymbolValue(
        _SYMBOL_INFO* symbol,
        ExceptionReportStackFrame* frame,
        char16_t* output,
        std::uint32_t outputSize);
    static int AUTHGATED_EXCEPTION_CALL EnumerateSymbolsCallback(
        _SYMBOL_INFO* symbol,
        std::uint32_t symbolSize,
        void* userContext);
    static void WriteStackDetails(_CONTEXT* context, int detail);
    static void WriteExceptionReport(
        _EXCEPTION_POINTERS* exceptionPointers);

    static std::array<char16_t, 260> m_szLogPrefixName;
    static std::array<char16_t, 260> m_szModuleName;
    static UserFunction m_lpUserFunc;
    static void* m_hProcess;
    static void* m_hReportFile;
    static ExceptionFilter m_OldFilter;
    static int m_bHasSymbol;
};


extern CExceptionReport g_CExceptionReport;

#undef AUTHGATED_EXCEPTION_CALL
