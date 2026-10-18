#pragma once

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <Windows.h>
#else

#include <arpa/inet.h>
#include <netinet/in.h>

// Product-neutral Win32 carriers used by the standalone authentication
// executables. Fixed-width fields keep the selected x86 PDB contracts.
// handles and pointers follow the portable host ABI.
struct _GUID {
    std::uint32_t Data1;
    std::uint16_t Data2;
    std::uint16_t Data3;
    std::uint8_t Data4[8];
};

using GUID = _GUID;
using CLSID = _GUID;

inline constexpr GUID GUID_NULL{};

struct _SERVICE_STATUS {
    std::uint32_t dwServiceType = 0;
    std::uint32_t dwCurrentState = 0;
    std::uint32_t dwControlsAccepted = 0;
    std::uint32_t dwWin32ExitCode = 0;
    std::uint32_t dwServiceSpecificExitCode = 0;
    std::uint32_t dwCheckPoint = 0;
    std::uint32_t dwWaitHint = 0;
};

using SERVICE_STATUS = _SERVICE_STATUS;
using SERVICE_STATUS_HANDLE = void*;

enum _SC_ACTION_TYPE : std::uint32_t {
    SC_ACTION_NONE = 0,
    SC_ACTION_RESTART = 1,
    SC_ACTION_REBOOT = 2,
    SC_ACTION_RUN_COMMAND = 3
};

struct _SC_ACTION {
    _SC_ACTION_TYPE Type = SC_ACTION_NONE;
    std::uint32_t Delay = 0;
};

using SC_ACTION = _SC_ACTION;

struct _SERVICE_FAILURE_ACTIONSA {
    std::uint32_t dwResetPeriod = 0;
    char* lpRebootMsg = nullptr;
    char* lpCommand = nullptr;
    std::uint32_t cActions = 0;
    SC_ACTION* lpsaActions = nullptr;
};

using SERVICE_FAILURE_ACTIONSA = _SERVICE_FAILURE_ACTIONSA;

struct _OVERLAPPED {
    struct OffsetPair {
        std::uint32_t Offset;
        std::uint32_t OffsetHigh;
    };

    union Data {
        OffsetPair offsets;
        void* Pointer;

        constexpr Data() noexcept : Pointer(nullptr) {}
    };

    std::uintptr_t Internal = 0;
    std::uintptr_t InternalHigh = 0;
    Data data;
    void* hEvent = nullptr;
};

union _ULARGE_INTEGER {
    struct Parts {
        std::uint32_t LowPart;
        std::uint32_t HighPart;
    } parts;
    std::uint64_t QuadPart;
};

using ULARGE_INTEGER = _ULARGE_INTEGER;

#endif
