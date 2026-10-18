#include "NCGuardServerLibCompat.h"
#include "Shared/src/Runtime/ConfigDirectory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

constexpr NCGuardServerResult kModuleLoadFailed = 0x3e84;
constexpr NCGuardServerResult kInvalidArgument = 0x3e85;
constexpr NCGuardServerResult kVersionReadFailed = 0x3e89;
#if defined(_WIN32)
constexpr NCGuardServerResult kProcedureLoadFailed = 0x3e88;
#endif
constexpr NCGuardServerResult kFunctionNotLoaded = 0x4653;
constexpr NCGuardServerResult kAlreadyInitialized = 0x4654;
constexpr NCGuardServerResult kServerNotInitialized = 0x4655;
#if defined(_WIN32)
constexpr NCGuardServerResult kConfigurationReadFailed = 0x465c;
constexpr NCGuardServerResult kVersionMismatch = 0x4666;
#endif

#if defined(_WIN32)
constexpr std::size_t kPathCapacity = 0x104;
#endif
constexpr std::size_t kCopyCapacity = 0x102;
constexpr std::size_t kVersionCapacity = 0x103;
constexpr wchar_t kLibraryName[] = L"ncguardserver64.dll";
constexpr wchar_t kConfigurationName[] = L"ncgsvrcfg.ini";

#if defined(_WIN32)
using NativeProcedure = FARPROC;
#else
using NativeProcedure = void*;
#endif

using InitializeFunction = NCGuardServerResult (*)(const wchar_t*, NCGuardSendCallback, NCGuardEventCallback);
using TerminateFunction = NCGuardServerResult (*)();
using StartFunction = NCGuardServerResult (*)();
using StopFunction = NCGuardServerResult (*)(std::uint32_t);
using OnReceivedFunction = NCGuardServerResult (*)(
    std::uint32_t, const std::uint8_t*, std::uint32_t, std::uintptr_t, std::uint32_t
);
using UserInfoFunction = NCGuardServerResult (*)(std::uint64_t);
using PseudoPacketFunction = NCGuardServerResult (*)(
    std::uint64_t, _PSEUDO_PACKET, std::uintptr_t, std::uintptr_t, std::uint32_t
);

// Exact private state consumed by the ncguardserverlib64.lib wrapper at
// current L2Server.exe 0x00ce6d30..0x00ce77e0. The target resolves all
// numbers 1..13, but TerminateNCGuardServer clears only 1..12.
struct NCGuardServerLibraryState final {
    bool initialized = false;
#if defined(_WIN32)
    HMODULE module = nullptr;
#else
    void* module = nullptr;
#endif
    std::array<NativeProcedure, 13> ordinals{};
    std::array<std::uint32_t, 256> crc32Table{};
};

NCGuardServerLibraryState state;

#if defined(_WIN32)

void UnloadModule() noexcept {
    if (state.module != nullptr) {
        ::FreeLibrary(state.module);
    }
    state.module = nullptr;
}

void InitializeCrc32Table() noexcept {
    state.crc32Table.fill(0);
    for (std::uint32_t index = 0; index < state.crc32Table.size(); ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) == 0U ? value >> 1U : (value >> 1U) ^ UINT32_C(0xedb88320);
        }
        state.crc32Table[index] = value;
    }
}

NCGuardServerResult BuildDirectoryPaths(
    const wchar_t* const dllDirectory,
    std::array<wchar_t, kPathCapacity>& normalizedDllDirectory,
    const wchar_t* const configurationDirectory,
    std::array<wchar_t, kPathCapacity>& normalizedConfigurationDirectory
) {
    const int dllDirectoryLength = ::lstrlenW(dllDirectory);
    const int libraryNameLength = ::lstrlenW(kLibraryName);
    if (dllDirectoryLength == 0 || static_cast<unsigned int>(dllDirectoryLength + libraryNameLength) > kCopyCapacity) {
        return kInvalidArgument;
    }

    ::lstrcpynW(normalizedDllDirectory.data(), dllDirectory, static_cast<int>(kCopyCapacity));
    if (dllDirectory[dllDirectoryLength - 1] != L'\\') {
        ::lstrcatW(normalizedDllDirectory.data(), L"\\");
    }

    const int configurationDirectoryLength = ::lstrlenW(configurationDirectory);
    const int configurationNameLength = ::lstrlenW(kConfigurationName);
    if (configurationDirectoryLength == 0 ||
        static_cast<unsigned int>(configurationDirectoryLength + configurationNameLength) >= kVersionCapacity) {
        return kInvalidArgument;
    }

    ::lstrcpynW(normalizedConfigurationDirectory.data(), configurationDirectory, static_cast<int>(kCopyCapacity));
    if (configurationDirectory[configurationDirectoryLength - 1] != L'\\') {
        ::lstrcatW(normalizedConfigurationDirectory.data(), L"\\");
    }
    return 0;
}

NCGuardServerResult ReadFileVersion(
    const wchar_t* const path, wchar_t* const text, NCGuardServerResult* const packedVersion
) {
    NCGuardServerResult result = kVersionReadFailed;
    if (path == nullptr || text == nullptr) {
        return kInvalidArgument;
    }

    DWORD handle = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) {
        return result;
    }

    HANDLE const processHeap = ::GetProcessHeap();
    void* const versionData = ::HeapAlloc(processHeap, HEAP_ZERO_MEMORY, size);
    if (versionData == nullptr) {
        return result;
    }
    std::memset(versionData, 0, size);

    void* fixedInfoStorage = nullptr;
    UINT fixedInfoSize = 0;
    if (::GetFileVersionInfoW(path, handle, size, versionData) != FALSE &&
        ::VerQueryValueW(versionData, L"\\", &fixedInfoStorage, &fixedInfoSize) != FALSE) {
        const auto* const fixedInfo = static_cast<const VS_FIXEDFILEINFO*>(fixedInfoStorage);
        const std::uint32_t major = HIWORD(fixedInfo->dwFileVersionMS);
        const std::uint32_t minor = LOWORD(fixedInfo->dwFileVersionMS);
        const std::uint32_t build = HIWORD(fixedInfo->dwFileVersionLS);
        const std::uint32_t revision = LOWORD(fixedInfo->dwFileVersionLS);
        std::swprintf(
            text, kVersionCapacity, L"%d.%d.%d.%d", static_cast<int>(major), static_cast<int>(minor),
            static_cast<int>(build), static_cast<int>(revision)
        );
        *packedVersion = major * UINT32_C(100000) + minor * UINT32_C(1000) + build * UINT32_C(10) + revision;
        result = 0;
    }

    // Preserve the embedded wrapper's unusual nonzero HeapFree flags lane.
    ::HeapFree(processHeap, HEAP_ZERO_MEMORY, versionData);
    return result;
}

template <typename Function>
Function Ordinal(const std::size_t ordinal) noexcept {
    return reinterpret_cast<Function>(state.ordinals[ordinal - 1]);
}

#endif

} // namespace

extern "C" NCGuardServerResult InitializeNCGuardServerW(
    const wchar_t* const dllDirectory,
    const wchar_t* const configurationDirectory,
    const NCGuardSendCallback sendCallback,
    const NCGuardEventCallback eventCallback
) {
    // Current embedded ncguardserverlib64.lib wrapper 0x00ce6d30.
    if (state.initialized) {
        return kAlreadyInitialized;
    }
    if (dllDirectory == nullptr || configurationDirectory == nullptr) {
        return kInvalidArgument;
    }

#if !defined(_WIN32)
    // The external module is a Windows PE boundary. Portable hosts never
    // execute the shipping DLL. Expose the target wrapper's load-failure
    // result so the enclosing startup path can keep its real policy.
    const std::size_t dllDirectoryLength = std::wcslen(dllDirectory);
    const std::size_t configurationDirectoryLength = std::wcslen(configurationDirectory);
    if (dllDirectoryLength == 0 || dllDirectoryLength + std::size(kLibraryName) - 1 > kCopyCapacity ||
        configurationDirectoryLength == 0 ||
        configurationDirectoryLength + std::size(kConfigurationName) - 1 >= kVersionCapacity) {
        return kInvalidArgument;
    }
    (void)sendCallback;
    (void)eventCallback;
    return kModuleLoadFailed;
#else
    std::array<wchar_t, kPathCapacity> normalizedDllDirectory{};
    std::array<wchar_t, kPathCapacity> normalizedConfigurationDirectory{};
    const std::wstring selectedConfigurationDirectory = runtime::ConfigFile(configurationDirectory, "NCGuard").wstring();
    NCGuardServerResult result = BuildDirectoryPaths(
        dllDirectory, normalizedDllDirectory, selectedConfigurationDirectory.c_str(), normalizedConfigurationDirectory
    );
    if (result != 0) {
        return result;
    }

    std::array<wchar_t, kPathCapacity> libraryPath{};
    ::lstrcpyW(libraryPath.data(), normalizedDllDirectory.data());
    ::lstrcatW(libraryPath.data(), kLibraryName);
    std::array<wchar_t, kPathCapacity> configurationPath{};
    ::lstrcpyW(configurationPath.data(), normalizedConfigurationDirectory.data());
    ::lstrcatW(configurationPath.data(), kConfigurationName);

    std::array<wchar_t, kPathCapacity> fileVersion{};
    NCGuardServerResult packedVersion = 0;
    result = ReadFileVersion(libraryPath.data(), fileVersion.data(), &packedVersion);
    if (result != 0) {
        return kVersionReadFailed;
    }

    std::array<wchar_t, kPathCapacity> configuredVersion{};
    if (::GetPrivateProfileStringW(
            L"update", L"ncgsdllver", L"9999.9.9.1", configuredVersion.data(), static_cast<DWORD>(kPathCapacity),
            configurationPath.data()
        ) == 0) {
        return kConfigurationReadFailed;
    }
    if (::lstrcmpW(fileVersion.data(), configuredVersion.data()) != 0) {
        return kVersionMismatch;
    }

    state.module = ::LoadLibraryW(libraryPath.data());
    if (state.module == nullptr) {
        state.module = ::LoadLibraryW(kLibraryName);
    }
    if (state.module == nullptr) {
        return ::GetLastError() == ERROR_PROC_NOT_FOUND ? kProcedureLoadFailed : kModuleLoadFailed;
    }

    for (std::size_t ordinal = 1; ordinal <= state.ordinals.size(); ++ordinal) {
        state.ordinals[ordinal - 1] = ::GetProcAddress(state.module, reinterpret_cast<LPCSTR>(ordinal));
    }
    for (const NativeProcedure procedure : state.ordinals) {
        if (procedure == nullptr) {
            UnloadModule();
            return kFunctionNotLoaded;
        }
    }

    result = Ordinal<InitializeFunction>(1)(configurationPath.data(), sendCallback, eventCallback);
    if (result == 0) {
        state.initialized = true;
    } else {
        UnloadModule();
    }
    InitializeCrc32Table();
    return result;
#endif
}

extern "C" NCGuardServerResult StartNCGuardServer() {
    // Current embedded wrapper 0x00ce7220.
    if (!state.initialized) {
        return kServerNotInitialized;
    }
#if !defined(_WIN32)
    return kFunctionNotLoaded;
#else
    const StartFunction start = Ordinal<StartFunction>(3);
    return start == nullptr ? kFunctionNotLoaded : start();
#endif
}

extern "C" NCGuardServerResult StopNCGuardServer(const std::uint32_t parameter) {
    // Current embedded wrapper 0x00ce7270.
    if (!state.initialized) {
        return kServerNotInitialized;
    }
#if !defined(_WIN32)
    (void)parameter;
    return kFunctionNotLoaded;
#else
    const StopFunction stop = Ordinal<StopFunction>(4);
    return stop == nullptr ? kFunctionNotLoaded : stop(parameter);
#endif
}

extern "C" NCGuardServerResult GetDllVersionW(
    const wchar_t* const path, wchar_t* const versionText, NCGuardServerResult* const packedVersion
) {
    // Current embedded wrapper 0x00ce74e0. The target validates
    // only path and text. PackedVersion remains an unchecked success output.
#if !defined(_WIN32)
    (void)packedVersion;
    if (path == nullptr || versionText == nullptr) {
        return kInvalidArgument;
    }
    return kVersionReadFailed;
#else
    return ReadFileVersion(path, versionText, packedVersion);
#endif
}

extern "C" NCGuardServerResult MakeReceivePseudoPacket(const std::uint64_t uniqueCharacterId, const _PSEUDO_PACKET purpose) {
    // Current embedded wrapper 0x00ce7700, vendor number 10. Unlike the
    // ordinary lifecycle exports, this wrapper has no
    // initialized-state gate.
#if !defined(_WIN32)
    (void)uniqueCharacterId;
    (void)purpose;
    return kFunctionNotLoaded;
#else
    const PseudoPacketFunction makePacket = Ordinal<PseudoPacketFunction>(10);
    return makePacket == nullptr ? kFunctionNotLoaded : makePacket(uniqueCharacterId, purpose, 0, 0, 0);
#endif
}

extern "C" NCGuardServerResult OnReceivedNCGuardClientMsg(
    const std::uint32_t accountId, const std::uint8_t* const data, const std::uint32_t size
) {
    // Current embedded wrapper 0x00ce73d0. The shipping three-argument
    // source call leaves an additional volatile register lane for number 5.
    // portable code cannot name that indeterminate lane, so it remains zero.
    if (!state.initialized) {
        return kServerNotInitialized;
    }
#if !defined(_WIN32)
    (void)accountId;
    (void)data;
    (void)size;
    return kFunctionNotLoaded;
#else
    const OnReceivedFunction onReceived = Ordinal<OnReceivedFunction>(5);
    return onReceived == nullptr ? kFunctionNotLoaded : onReceived(accountId, data, size, 0, 0);
#endif
}

extern "C" NCGuardServerResult CreateNCGuardUserInfo(const std::uint64_t characterDatabaseId) {
    // Current embedded wrapper 0x00ce7440, vendor number 6.
    if (!state.initialized) {
        return kServerNotInitialized;
    }
#if !defined(_WIN32)
    (void)characterDatabaseId;
    return kFunctionNotLoaded;
#else
    const UserInfoFunction create = Ordinal<UserInfoFunction>(6);
    return create == nullptr ? kFunctionNotLoaded : create(characterDatabaseId);
#endif
}

extern "C" NCGuardServerResult DeleteNCGuardUserInfo(const std::uint64_t characterDatabaseId) {
    // Current embedded wrapper 0x00ce7490, vendor number 7.
    if (!state.initialized) {
        return kServerNotInitialized;
    }
#if !defined(_WIN32)
    (void)characterDatabaseId;
    return kFunctionNotLoaded;
#else
    const UserInfoFunction remove = Ordinal<UserInfoFunction>(7);
    return remove == nullptr ? kFunctionNotLoaded : remove(characterDatabaseId);
#endif
}

extern "C" NCGuardServerResult TerminateNCGuardServer() {
    // Current embedded wrapper 0x00ce72c0.
    if (!state.initialized) {
        return kServerNotInitialized;
    }
#if !defined(_WIN32)
    return kFunctionNotLoaded;
#else
    const TerminateFunction terminate = Ordinal<TerminateFunction>(2);
    if (terminate == nullptr) {
        return kFunctionNotLoaded;
    }

    terminate();
    UnloadModule();
    for (std::size_t index = 0; index < 12; ++index) {
        state.ordinals[index] = nullptr;
    }
    state.initialized = false;
    return 0;
#endif
}
