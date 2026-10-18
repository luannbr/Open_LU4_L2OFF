#include "Shared/src/Utils.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/Utf16Format.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "TextRuntime.h"
#include "net/Config.h"

#if defined(L2_BUILD_L2SERVER)
#include "PCParam.h"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace {

#if !defined(_WIN32)
std::string HostPathFromWindowsWide(const wchar_t* fileName) {
    if (!fileName) {
        return {};
    }

    std::string path = WideToUtf8(std::wstring_view(fileName));
    // The recovered owners keep their exact Windows path literals. Only
    // the host filesystem transport maps Win32 separators on POSIX hosts.
    std::replace(path.begin(), path.end(), '\\', '/');
    return runtime::ResolveResourcePath(path).string();
}
#endif

bool IsTargetLeapYear(std::uint16_t year) {
    // This is the shipping helper's historical rule, including its deliberate
    // omission of the Gregorian divisible-by-400 exception.
    return (year & 3U) == 0U && year % 100U != 0U;
}

bool CanOpenWideBinary(const wchar_t* fileName) {
    if (!fileName) {
        return false;
    }
#if defined(_WIN32)
    const std::string path = WideToUtf8(std::wstring_view(fileName));
    const std::string selectedPath = runtime::ResolveResourcePath(path).string();
#else
    const std::string path = HostPathFromWindowsWide(fileName);
#endif
    std::FILE* const stream = std::fopen(
#if defined(_WIN32)
        selectedPath.c_str(),
#else
        path.c_str(),
#endif
        "rb"
    );
    if (!stream) {
        return false;
    }
    std::fclose(stream);
    return true;
}

std::string NarrowTargetNumber(std::u16string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char16_t codeUnit : text) {
        if (codeUnit > 0x7f) {
            return {};
        }
        result.push_back(static_cast<char>(codeUnit));
    }
    return result;
}

int ParseTargetInt32(std::u16string_view text) {
    const std::string ascii = NarrowTargetNumber(text);
    if (ascii.empty()) {
        return 0;
    }

    char* end = nullptr;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 10);
    if (end == ascii.c_str()) {
        return 0;
    }
    if (parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (parsed < static_cast<long long>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(parsed);
}

std::wistringstream*& HostSecureStream(std::array<std::byte, 0x108>& image) {
    return *reinterpret_cast<std::wistringstream**>(image.data());
}

int WritePortablePrivateProfile(const wchar_t* section, const wchar_t* key, const wchar_t* value, const wchar_t* fileName) {
    std::wstring selectedPath;
    if (fileName && runtime::HasConfigDirectory() &&
        (std::wcscmp(fileName, L".\\l2npc.ini") == 0 || std::wcscmp(fileName, L".\\l2server.ini") == 0)) {
        selectedPath =
            runtime::ConfigFile(std::filesystem::path(fileName), std::filesystem::path(fileName + 2)).wstring();
        fileName = selectedPath.c_str();
    }
#if defined(_WIN32)
    return ::WritePrivateProfileStringW(section, key, value, fileName);
#else
    // WritePrivateProfileStringW mutates the path supplied by the caller.
    // it does not redirect through an L2NPC-owned configuration singleton.
    // Re-open the selected file with the same first-section/first-key rules.
    if (!fileName || !section || !key || !value) {
        return 0;
    }
    runtime::Config configuration;
    if (!configuration.Load(WideToUtf8(std::wstring_view(fileName)), false)) {
        return 0;
    }
    return configuration.Set(
               WideToUtf8(std::wstring_view(section)), WideToUtf8(std::wstring_view(key)),
               WideToUtf8(std::wstring_view(value))
           )
        ? 1
        : 0;
#endif
}

// Diagnostic only: report OS results without publishing command arguments,
// which may contain credentials. Preserve the caller's native error state.
void ReportAutoStartFailure(const char* setting, const wchar_t* workingFolder, const char* operation, unsigned long error) {
    struct RestoreErrors {
        int savedErrno = errno;
#if defined(_WIN32)
        DWORD savedLastError = ::GetLastError();
#endif
        ~RestoreErrors() {
#if defined(_WIN32)
            ::SetLastError(savedLastError);
#endif
            errno = savedErrno;
        }
    } restoreErrors;
#if defined(_WIN32)
    std::array<wchar_t, 1024> reason{};
    DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(error), 0, reason.data(),
        static_cast<DWORD>(reason.size()), nullptr
    );
    while (length > 0 && (reason[length - 1] == L'\r' || reason[length - 1] == L'\n' || reason[length - 1] == L' ')) {
        reason[--length] = L'\0';
    }
    // Log.Add(LOG_ERROR,
    //     L"[startup] %hs could not create the child process: %hs failed, Windows error %lu (%ls). Working folder: \"%ls\".
    //     Check [%hs] FilePath and WorkingFolder.", setting, operation, error, length ? reason.data() : L"message
    //     unavailable", workingFolder ? workingFolder : L"<unspecified>", setting)
    L2_LOG_ERROR(
        Log,
        u"[startup] %hs could not create the child process: %hs failed, Windows error %lu (%ls). Working folder: \"%ls\". "
        u"Check [%hs] FilePath and WorkingFolder.",
        setting, operation, error, length ? reason.data() : L"message unavailable",
        workingFolder ? workingFolder : L"<unspecified>", setting
    );
#else
    const std::string reason = std::generic_category().message(static_cast<int>(error));
    // Log.Add(LOG_ERROR,
    //     L"[startup] %hs could not create the child process: %hs failed, POSIX error %lu (%hs). Working folder: \"%ls\".
    //     Check [%hs] FilePath and WorkingFolder.", setting, operation, error, reason.c_str(), workingFolder ? workingFolder
    //     : L"<unspecified>", setting)
    L2_LOG_ERROR(
        Log,
        u"[startup] %hs could not create the child process: %hs failed, POSIX error %lu (%hs). Working folder: \"%ls\". "
        u"Check [%hs] FilePath and WorkingFolder.",
        setting, operation, error, reason.c_str(), workingFolder ? workingFolder : L"<unspecified>", setting
    );
#endif
}

#if !defined(_WIN32)
std::vector<std::wstring> SplitWindowsCommandLine(const std::wstring& commandLine) {
    // CreateProcessW receives Config::m_filePath as one mutable command line.
    // Preserve its quote/backslash interpretation at the POSIX transport
    // boundary instead of treating the complete value as one file name.
    std::vector<std::wstring> arguments;
    std::size_t cursor = 0;
    while (cursor < commandLine.size()) {
        while (cursor < commandLine.size() && (commandLine[cursor] == L' ' || commandLine[cursor] == L'\t')) {
            ++cursor;
        }
        if (cursor == commandLine.size()) {
            break;
        }

        std::wstring argument;
        bool quoted = false;
        while (cursor < commandLine.size()) {
            if (!quoted && (commandLine[cursor] == L' ' || commandLine[cursor] == L'\t')) {
                break;
            }

            std::size_t slashCount = 0;
            while (cursor < commandLine.size() && commandLine[cursor] == L'\\') {
                ++slashCount;
                ++cursor;
            }
            if (cursor < commandLine.size() && commandLine[cursor] == L'"') {
                argument.append(slashCount / 2, L'\\');
                if ((slashCount & 1U) != 0U) {
                    argument.push_back(L'"');
                } else {
                    quoted = !quoted;
                }
                ++cursor;
                continue;
            }
            argument.append(slashCount, L'\\');
            if (cursor == commandLine.size()) {
                break;
            }
            argument.push_back(commandLine[cursor++]);
        }
        arguments.push_back(std::move(argument));
    }
    return arguments;
}

namespace {

// posix_spawn_file_actions_addchdir is the POSIX 2024 name. macOS has it from
// version 26. Older macOS and glibc have only the _np form.
int AddSpawnWorkingDirectory(posix_spawn_file_actions_t* actions, const char* directory) {
#if defined(__APPLE__)
    if (__builtin_available(macOS 26.0, *)) {
        return ::posix_spawn_file_actions_addchdir(actions, directory);
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return ::posix_spawn_file_actions_addchdir_np(actions, directory);
#pragma clang diagnostic pop
#elif defined(__GLIBC__)
    return ::posix_spawn_file_actions_addchdir_np(actions, directory);
#else
    (void)actions;
    (void)directory;
    return ENOTSUP;
#endif
}

} // namespace

void StartPortableProcess(const std::wstring& commandLine, const std::wstring& workingFolder) {
    std::vector<std::wstring> wideArguments = SplitWindowsCommandLine(commandLine);
    if (wideArguments.empty() || wideArguments.front().empty()) {
        // Log.Add(LOG_ERROR,
        //     "[startup] ServerAutoStarter could not create the child process: [ServerAutoStarter] FilePath has no
        //     executable name.")
        L2_LOG_ERROR(
            Log,
            "[startup] ServerAutoStarter could not create the child process: [ServerAutoStarter] FilePath has no executable "
            "name."
        );
        return;
    }

    std::vector<std::string> arguments;
    arguments.reserve(wideArguments.size());
    for (const std::wstring& argument : wideArguments) {
        arguments.push_back(WideToUtf8(argument));
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    // if (::posix_spawn_file_actions_init(&actions) != 0) {
    const int initResult = ::posix_spawn_file_actions_init(&actions);
    if (initResult != 0) {
        ReportAutoStartFailure(
            "ServerAutoStarter", workingFolder.c_str(), "posix_spawn_file_actions_init",
            static_cast<unsigned long>(initResult)
        );
        return;
    }

    int actionResult = 0;
    const std::string directory = WideToUtf8(workingFolder);
    if (!directory.empty()) {
        actionResult = AddSpawnWorkingDirectory(&actions, directory.c_str());
    }

    pid_t processId = -1;
    int spawnResult = actionResult;
    if (spawnResult == 0) {
        spawnResult = ::posix_spawnp(&processId, argv.front(), &actions, nullptr, argv.data(), environ);
    }
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawnResult != 0 || processId <= 0) {
        if (spawnResult != 0) {
            ReportAutoStartFailure(
                "ServerAutoStarter", workingFolder.c_str(),
                actionResult != 0 ? "working-directory action setup" : "posix_spawnp",
                static_cast<unsigned long>(spawnResult)
            );
        } else {
            // Log.Add(LOG_ERROR,
            //     "[startup] ServerAutoStarter received no valid child process ID from spawn (%lld). Startup cannot be
            //     confirmed.", static_cast<long long>(processId))
            L2_LOG_ERROR(
                Log,
                "[startup] ServerAutoStarter received no valid child process ID from spawn (%lld); startup cannot be "
                "confirmed.",
                static_cast<long long>(processId)
            );
        }
        return;
    }

    // A Win32 process handle can be closed without waiting. POSIX children
    // instead need one waiter to avoid a zombie after the launched server
    // eventually exits. This detached waiter is transport-only state and is
    // not added to the recovered ServerAutoStarter object.
    std::thread([processId] {
        int status = 0;
        while (::waitpid(processId, &status, 0) < 0 && errno == EINTR) {
        }
    }).detach();
    // Log.Add(LOG_NORMAL,
    //     L"[startup] ServerAutoStarter created child process %lld. Working folder: \"%ls\". Application readiness is not
    //     confirmed.", static_cast<long long>(processId), workingFolder.c_str())
    L2_LOG_INFO(
        Log,
        u"[startup] ServerAutoStarter created child process %lld. Working folder: \"%ls\". Application readiness is not "
        u"confirmed.",
        static_cast<long long>(processId), workingFolder.c_str()
    );
}

void StartPortableBatch(const std::wstring& filePath, const std::wstring& workingFolder) {
    std::array<std::string, 3> arguments{
        "/bin/sh",
        "-c",
        WideToUtf8(filePath),
    };
    std::array<char*, 4> argv{
        arguments[0].data(),
        arguments[1].data(),
        arguments[2].data(),
        nullptr,
    };

    posix_spawn_file_actions_t actions{};
    // if (::posix_spawn_file_actions_init(&actions) != 0) {
    const int initResult = ::posix_spawn_file_actions_init(&actions);
    if (initResult != 0) {
        ReportAutoStartFailure(
            "BatchAutoStarter", workingFolder.c_str(), "posix_spawn_file_actions_init",
            static_cast<unsigned long>(initResult)
        );
        return;
    }

    int actionResult = 0;
    const std::string directory = WideToUtf8(workingFolder);
    if (!directory.empty()) {
        actionResult = AddSpawnWorkingDirectory(&actions, directory.c_str());
    }

    pid_t processId = -1;
    int spawnResult = actionResult;
    if (spawnResult == 0) {
        spawnResult = ::posix_spawn(&processId, argv.front(), &actions, nullptr, argv.data(), environ);
    }
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawnResult != 0 || processId <= 0) {
        if (spawnResult != 0) {
            ReportAutoStartFailure(
                "BatchAutoStarter", workingFolder.c_str(),
                actionResult != 0 ? "working-directory action setup" : "posix_spawn", static_cast<unsigned long>(spawnResult)
            );
        } else {
            // Log.Add(LOG_ERROR,
            //     "[startup] BatchAutoStarter received no valid child process ID from spawn (%lld). Startup cannot be
            //     confirmed.", static_cast<long long>(processId))
            L2_LOG_ERROR(
                Log,
                "[startup] BatchAutoStarter received no valid child process ID from spawn (%lld); startup cannot be "
                "confirmed.",
                static_cast<long long>(processId)
            );
        }
        return;
    }

    std::thread([processId] {
        int status = 0;
        while (::waitpid(processId, &status, 0) < 0 && errno == EINTR) {
        }
    }).detach();
    // Log.Add(LOG_NORMAL,
    //     L"[startup] BatchAutoStarter created command interpreter process %lld. Working folder: \"%ls\". Command completion
    //     is not confirmed.", static_cast<long long>(processId), workingFolder.c_str())
    L2_LOG_INFO(
        Log,
        u"[startup] BatchAutoStarter created command interpreter process %lld. Working folder: \"%ls\". Command completion "
        u"is not confirmed.",
        static_cast<long long>(processId), workingFolder.c_str()
    );
}
#endif

} // namespace

namespace L2Util {

void ReportByteArrayToHexStr(const wchar_t* label, const char* begin, const char* end) {
    if (!label || !begin || !end) {
        return;
    }

    int rowIndex = 0;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ReportByteArrayToHexStr][%s] Start",
    //     label)
    L2_LOG_ERROR(Log, u"[ReportByteArrayToHexStr][%s] Start", label);
    while (begin < end) {
        char row[101]{};
        char* output = row;
        int byteIndex = 0;
        while (begin < end && byteIndex < 50) {
            char encoded[16]{};
            std::snprintf(encoded, sizeof(encoded), "%02x", static_cast<int>(static_cast<signed char>(*begin)));
            output[0] = encoded[0];
            output[1] = encoded[1];
            output += 2;
            ++begin;
            ++byteIndex;
        }
        *output = '\0';
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] %S",
        //     label,
        //     rowIndex,
        //     row)
        L2_LOG_ERROR(Log, u"[%s][%d] %S", label, rowIndex, row);
        ++rowIndex;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[ReportByteArrayToHexStr][%s] End",
    //     label)
    L2_LOG_ERROR(Log, u"[ReportByteArrayToHexStr][%s] End", label);
}

} // namespace L2Util

bool g_bScriptSecureEnabled = false;

bool g_bPopUpWindowsEnabled = true;

void PopUpWindowV(const wchar_t* format, std::va_list arguments) {
    // Current Cached.exe unnamed Shared/Utils.cpp body 0x005c8fd4.
    // std::array<wchar_t, 1024> message{}
#if defined(_WIN32)
    if (!g_bPopUpWindowsEnabled) {
        runtime::log::WriteConsoleErrorV(format, arguments);
        return;
    }
    std::array<wchar_t, 1024> message{};
    ::_vsnwprintf_s(message.data(), message.size(), message.size() - 1, format, arguments);
    ::MessageBoxW(nullptr, message.data(), L"", 0);
#else
    // const std::wstring hostFormat =
    //     WindowsWideFormatToHost(format)
    // std::vswprintf(
    //     message.data(),
    //     message.size(),
    //     hostFormat.c_str(),
    //     arguments)
    // std::fwprintf(
    //     stderr,
    //     L"%ls\n",
    //     message.data())
    runtime::log::WriteConsoleErrorV(format, arguments);
#endif
}

void PopUpWindow(const wchar_t* format, ...) {
    std::va_list arguments;
    va_start(arguments, format);
    // PopUpWindowV(
    //     format,
    //     arguments)
    try {
        PopUpWindowV(format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void ChangeFileNameForLocalization(const wchar_t* source, wchar_t* destination, int country) {
    // Current L2NPC.exe 0x005458a4, CacheD.exe 0x005c9fd8 and L2Server.exe
    // 0x00c3eef8. The suffix records are 14 local wchar_t[10] objects. The
    // third argument is ignored and the unchecked process-global country
    // code selects one. CacheD and L2Server additionally probe the localized
    // candidate and fall back to the input name.
    L2_GUARD_FUNCTION();

    const wchar_t suffixes[14][10]{
        L"k", L"e", L"j", L"tw", L"cn", L"th", L"ph", L"id", L"ru", L"eu", L"de", L"fr", L"pl", L"tr"};
    const wchar_t* extension = std::wcsstr(source, L".txt");
    const std::size_t stemLength = static_cast<std::size_t>(extension - source);
    wchar_t stem[1024]{};
    if (stemLength >= std::size(stem)) {
        stem[0] = L'\0';
        // The VS2010 secure CRT in this image reaches
        // _invalid_parameter_noinfo, so every diagnostic argument is null.
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
    } else {
        std::wmemcpy(stem, source, stemLength);
        stem[stemLength] = L'\0';
    }

    const wchar_t* const suffix = suffixes[g_nCountryCode];
    constexpr wchar_t kLocalizedFormat[] = L"%s-%s.txt";
    if (!destination) {
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return;
    }
    const std::wstring hostFormat = WindowsWideFormatToHost(kLocalizedFormat);
    const int formatted = std::swprintf(destination, 1024U, hostFormat.c_str(), stem, suffix);
    if (formatted < 0 || formatted >= 1024) {
        destination[0] = L'\0';
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
    }

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
    const wchar_t* probeName = destination;
    wchar_t secureProbeName[1024]{};
    if (g_bScriptSecureEnabled) {
        const std::size_t localizedLength = std::wcslen(destination);
        constexpr wchar_t encryptedSuffix[] = L".l2enc";
        if (localizedLength + std::size(encryptedSuffix) > std::size(secureProbeName)) {
            secureProbeName[0] = L'\0';
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::wmemcpy(secureProbeName, destination, localizedLength);
            std::wmemcpy(secureProbeName + localizedLength, encryptedSuffix, std::size(encryptedSuffix));
        }
        probeName = secureProbeName;
    }

    if (!CanOpenWideBinary(probeName)) {
        const std::size_t sourceLength = std::wcslen(source);
        if (sourceLength >= 1024U) {
            destination[0] = L'\0';
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::wmemcpy(destination, source, sourceLength + 1U);
        }
    }
#endif
}

void* ReadIntMem(void* memory, int& value) {
    auto* const bytes = static_cast<unsigned char*>(memory);
    const std::uint32_t raw = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    static_assert(sizeof(value) == sizeof(raw));
    std::memcpy(&value, &raw, sizeof(value));
    return bytes + sizeof(value);
}

bool IsInvalidString(wchar_t* value) { return value == nullptr || std::wcslen(value) == 0; }

wchar_t* SafeString(wchar_t* const source, const int count) {
    // Current L2Server.exe 0x00c3cc74, original Shared/Utils.cpp owner.
    thread_local std::array<wchar_t, 10000> buffer{};
    if (source == nullptr || count < 0) {
        buffer[0] = wchar_t{};
        return buffer.data();
    }

    const std::size_t maximum = std::min(static_cast<std::size_t>(count), buffer.size() - 1U);
    std::size_t length = 0;
    while (length < maximum && source[length] != wchar_t{}) {
        buffer[length] = source[length];
        ++length;
    }
    buffer[length] = wchar_t{};
    return buffer.data();
}

char* LoadBinaryAbsolute(const wchar_t* fileName, int& byteCount, bool requireUnicode) {
    // Current L2NPC.exe 0x00545684.
    L2_GUARD_FUNCTION();

    int descriptor = -1;
#if defined(_WIN32)
    ::_wsopen_s(
        &descriptor, fileName ? runtime::ResolveResourcePath(fileName).wstring().c_str() : nullptr, _O_BINARY | _O_RDONLY,
        _SH_DENYWR, 0
    );
#else
    std::string path;
    if (!fileName) {
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
    } else {
        path = HostPathFromWindowsWide(fileName);
        descriptor = ::open(path.c_str(), O_RDONLY);
    }
#endif
    if (descriptor < 0) {
        return nullptr;
    }

    // SHIPPING BUG UTIL-INIT-001: L2NPC.exe 0x00545684 ignores the result of
    // _wstat64i32 and reads status.st_size even when the call fails.
    // Keep the original code below as a disabled example of the source bug.
    /*
    std::int32_t fileSize,
    #if defined(_WIN32)
        struct _stat64i32 status,
        ::_wstat64i32(
            fileName,
            &status),
        fileSize = status.st_size,
    #else
        struct stat status,
        if (::stat(
                path.c_str(),
                &status) == 0 &&
            status.st_size >=
                std::numeric_limits<
                    std::int32_t>::min() &&
            status.st_size <=
                std::numeric_limits<
                    std::int32_t>::max()) {
            fileSize =
                static_cast<std::int32_t>(
                    status.st_size)
        }
    #endif
    */

    std::int32_t fileSize = 0;
#if defined(_WIN32)
    struct _stat64i32 status{};
    if (::_wstat64i32(runtime::ResolveResourcePath(fileName).wstring().c_str(), &status) != 0 || status.st_size < 0) {
        ::_close(descriptor);
        return nullptr;
    }
    fileSize = status.st_size;
#else
    struct stat status{};
    if (::stat(path.c_str(), &status) != 0 || status.st_size < 0 ||
        status.st_size > std::numeric_limits<std::int32_t>::max()) {
        ::close(descriptor);
        return nullptr;
    }
    fileSize = static_cast<std::int32_t>(status.st_size);
#endif

    const std::uint32_t allocationBits = static_cast<std::uint32_t>(fileSize) + 0x50U;
    std::int32_t signedAllocationSize;
    std::memcpy(&signedAllocationSize, &allocationBits, sizeof(signedAllocationSize));
    char* const image =
        static_cast<char*>(std::malloc(static_cast<std::size_t>(static_cast<std::int64_t>(signedAllocationSize))));
    if (!image) {
#if defined(_WIN32)
        ::_close(descriptor);
#else
        ::close(descriptor);
#endif
        return nullptr;
    }

#if defined(_WIN32)
    ::_read(descriptor, image, static_cast<unsigned int>(fileSize));
    ::_close(descriptor);
#else
    ::read(descriptor, image, static_cast<std::uint32_t>(fileSize));
    ::close(descriptor);
#endif
    byteCount = fileSize;
    if (requireUnicode && (static_cast<unsigned char>(image[0]) != 0xffU || static_cast<unsigned char>(image[1]) != 0xfeU)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"file is not unicode. File [%s]",
        //     fileName)
        // Log.Add(LOG_ERROR,
        //     L"[files] Expected a UTF-16LE byte-order mark in \"%ls\". Returning the loaded bytes unchanged.",
        //     fileName ? fileName : L"<unspecified>")
        L2_LOG_ERROR(
            Log, u"[files] Expected a UTF-16LE byte-order mark in \"%ls\"; returning the loaded bytes unchanged.",
            fileName ? fileName : L"<unspecified>"
        );
    }
    return image;
}

char* LoadBinaryAbsoluteSecure(const wchar_t* fileName, int& byteCount, bool secure) {
    // Current L2NPC.exe 0x0054676c, CacheD.exe 0x005ca4e0 and L2Server.exe
    // 0x00c3f344. CacheD and L2Server first pass the input through their
    // product-specific localization/fallback body. L2NPC uses the input
    // directly. The native wifstream is used only as an existence/state
    // probe. CanOpenWideBinary is its host transport.
    L2_GUARD_FUNCTION();

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
    wchar_t localizedName[1024]{};
    ChangeFileNameForLocalization(fileName, localizedName, -1);
    const wchar_t* const selectedFileName = localizedName;
#else
    const wchar_t* const selectedFileName = fileName;
#endif

    if (!g_bScriptSecureEnabled || !secure) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[INSECURE] Read %s",
        //     selectedFileName)
        // Log.Add(LOG_NORMAL,
        //     L"[scripts] Reading unencrypted file \"%ls\".",
        //     selectedFileName ? selectedFileName : L"<unspecified>")
        L2_LOG_INFO(
            Log, u"[scripts] Reading unencrypted file \"%ls\".", selectedFileName ? selectedFileName : L"<unspecified>"
        );
        return LoadBinaryAbsolute(selectedFileName, byteCount, false);
    }

    wchar_t secureName[1024]{};
    if (!selectedFileName) {
        secureName[0] = L'\0';
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
    } else {
        const std::size_t fileNameLength = std::wcslen(selectedFileName);
        if (fileNameLength >= std::size(secureName)) {
            secureName[0] = L'\0';
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::wmemcpy(secureName, selectedFileName, fileNameLength + 1U);
        }
    }

    const std::size_t copiedLength = std::wcslen(secureName);
    constexpr wchar_t encryptedSuffix[] = L".l2enc";
    if (copiedLength + std::size(encryptedSuffix) > std::size(secureName)) {
        secureName[0] = L'\0';
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
    } else {
        std::wmemcpy(secureName + copiedLength, encryptedSuffix, std::size(encryptedSuffix));
    }

    if (!CanOpenWideBinary(secureName)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[SECURE] Read from %s file failed",
        //     secureName)
        // Log.Add(LOG_ERROR,
        //     L"[scripts] Cannot open encrypted file \"%ls\" (errno %d). Loading rejected.",
        //     secureName, errno)
        L2_LOG_ERROR(Log, u"[scripts] Cannot open encrypted file \"%ls\" (errno %d); loading rejected.", secureName, errno);
        return nullptr;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SECURE] Read %s",
    //     selectedFileName)
    // Log.Add(LOG_NORMAL,
    //     L"[scripts] Reading encrypted file \"%ls\".",
    //     secureName)
    L2_LOG_INFO(Log, u"[scripts] Reading encrypted file \"%ls\".", secureName);
    char* const image = LoadBinaryAbsolute(secureName, byteCount, false);
    if (byteCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[SECURE] Read from %s file failed",
        //     secureName)
        // Log.Add(LOG_ERROR,
        //     L"[scripts] Encrypted file load reported %d bytes for \"%ls\". A positive byte count is required. Loading
        //     rejected.", byteCount, secureName)
        L2_LOG_ERROR(
            Log,
            u"[scripts] Encrypted file load reported %d bytes for \"%ls\"; a positive byte count is required. Loading "
            u"rejected.",
            byteCount, secureName
        );
        return nullptr;
    }

    for (int index = 0; index < byteCount - 1; ++index) {
        image[index] =
            static_cast<char>(static_cast<unsigned char>(image[index]) ^ static_cast<unsigned char>(image[index + 1]));
    }
    return image;
}

std::uint16_t* LoadBinaryAbsoluteW(const wchar_t* fileName, int& codeUnitCount) {
    L2_GUARD_FUNCTION();

    int byteCount = 0;
    char* const image = LoadBinaryAbsolute(fileName, byteCount, false);
    codeUnitCount = byteCount / 2;
    return reinterpret_cast<std::uint16_t*>(image);
}

CSecureStream::CSecureStream(const wchar_t* fileName, bool createRawString) {
    // Current L2NPC constructor 0x00546ad0 and CacheD constructor
    // 0x005ca948. PDB donor Shared/Utils.cpp 1589-1594.
    m_validFileName = CreateValidName(fileName);
    m_inTargetImage[0] = CanOpenWideBinary(m_validFileName) ? std::byte{1} : std::byte{0};

    char16_t* const raw = CreateRawString(fileName, createRawString);
    const std::u16string_view rawView =
        raw ? std::u16string_view(raw, static_cast<std::size_t>(m_RawStringLen)) : std::u16string_view{};
    HostSecureStream(m_secureInTargetImage) = new std::wistringstream(Utf16ToWide(rawView));
}

CSecureStream::~CSecureStream() {
    // Current L2NPC destructor 0x0041a41c and CacheD destructor 0x00406ee0
    // close `in`, then release raw/name and destroy m_secureIn. The portable
    // existence probe has no live handle.
    std::free(m_pszRawString);
    std::free(m_validFileName);
    delete HostSecureStream(m_secureInTargetImage);
}

char16_t* CSecureStream::CreateRawString(const wchar_t* fileName, bool create) {
    // Current L2NPC.exe 0x005469fc and CacheD.exe 0x005ca7bc.
    if (!create) {
        m_pszRawString = nullptr;
        m_RawStringLen = 0;
        static char16_t empty = u'\0';
        return &empty;
    }

    int byteCount = 0;
    char* const bytes = LoadBinaryAbsoluteSecure(fileName, byteCount, true);
    const int codeUnitCount = byteCount / 2;
    auto* const raw = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(codeUnitCount) + 1U) * sizeof(char16_t)));
    if (!raw) {
        std::free(bytes);
        throw std::bad_alloc{};
    }
    for (int index = 0; index < codeUnitCount; ++index) {
        const auto low = static_cast<std::uint8_t>(bytes[index * 2]);
        const auto high = static_cast<std::uint8_t>(bytes[index * 2 + 1]);
        raw[index] = static_cast<char16_t>(static_cast<std::uint16_t>(low) | (static_cast<std::uint16_t>(high) << 8U));
    }
    std::free(bytes);
    raw[codeUnitCount] = u'\0';
    m_pszRawString = raw;
    m_RawStringLen = codeUnitCount;
    return raw;
}

wchar_t* CSecureStream::CreateValidName(const wchar_t* fileName) {
    // Current L2NPC.exe 0x00546088 and CacheD.exe 0x005ca890. CacheD first
    // runs its product-specific localization/fallback body. Both products
    // then use a zeroed wchar_t[1024], wcscpy_s, an optional
    // wcscat_s(".l2enc"), and _wcsdup. A failed initial copy clears the
    // carrier and the suffix append still runs. _wcsdup reports allocation
    // failure as a null result instead of throwing.
    std::array<wchar_t, 0x400> value{};
    const wchar_t* selectedFileName = fileName;
#if defined(L2_BUILD_CACHED)
    std::array<wchar_t, 0x400> localizedName{};
    ChangeFileNameForLocalization(fileName, localizedName.data(), -1);
    selectedFileName = localizedName.data();
#endif
    if (!selectedFileName) {
        value[0] = L'\0';
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
    } else {
        const std::size_t length = std::wcslen(selectedFileName);
        if (length >= value.size()) {
            value[0] = L'\0';
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::wmemcpy(value.data(), selectedFileName, length + 1U);
        }
    }

    if (g_bScriptSecureEnabled) {
        constexpr wchar_t encryptedSuffix[] = L".l2enc";
        const std::size_t length = std::wcslen(value.data());
        if (length + std::size(encryptedSuffix) > value.size()) {
            value[0] = L'\0';
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
            std::wmemcpy(value.data() + length, encryptedSuffix, std::size(encryptedSuffix));
        }
    }

    const std::size_t bytes = (std::wcslen(value.data()) + 1U) * sizeof(wchar_t);
    auto* const duplicate = static_cast<wchar_t*>(std::malloc(bytes));
    if (duplicate) {
        std::memcpy(duplicate, value.data(), bytes);
    }
    return duplicate;
}

int UnicodeToAnsi(char16_t* source, int sourceCount, char* destination, int destinationCount) {
    // The two calls and explicit final NUL reproduce current L2NPC.exe
    // 0x00545590. Its sourceCount/destinationCount parameters are dead.
    L2_GUARD_FUNCTION();

    const int byteCount = portable::WideCharToWindowsAnsi(source, -1, nullptr, 0);
    portable::WideCharToWindowsAnsi(source, -1, destination, byteCount);
    destination[byteCount - 1] = '\0';
    return byteCount;
}

int SetPrivateProfile(const wchar_t* section, const wchar_t* key, wchar_t* value, const wchar_t* fileName) {
    // Current L2NPC.exe 0x00545b6c.
    L2_GUARD_FUNCTION();

    return WritePortablePrivateProfile(section, key, value, fileName);
}

int SetPrivateProfile(const wchar_t* section, const wchar_t* key, int value, const wchar_t* fileName) {
    // Current L2NPC.exe 0x00545c28 uses _itow_s(buffer[256], 10).
    L2_GUARD_FUNCTION();

    // The profile file API keeps native wide text.
    std::array<wchar_t, 256> text{};
    std::swprintf(text.data(), text.size(), L"%d", value);
    return WritePortablePrivateProfile(section, key, text.data(), fileName);
}

std::u16string IntToXStr(int value) {
    // Current L2NPC.exe 0x00546414 and L2Server.exe 0x00c3de80 use
    // _itow_s(..., 50, 10), then construct the xallocator string from that
    // temporary decimal buffer.
    L2_GUARD_FUNCTION();

    std::array<char, 50> narrow{};
    const auto result = std::to_chars(narrow.data(), narrow.data() + narrow.size() - 1, value, 10);
    if (result.ec != std::errc{}) {
        return std::u16string{};
    }

    std::array<char16_t, 50> wide{};
    const std::size_t length = static_cast<std::size_t>(result.ptr - narrow.data());
    for (std::size_t index = 0; index < length; ++index) {
        wide[index] = static_cast<unsigned char>(narrow[index]);
    }
    return std::u16string(std::u16string_view(wide.data(), length));
}

std::u16string ReplaceStr(std::u16string source, const char16_t* search, const char16_t* replacement) {
    // Current Cached.exe 0x005c99e8 and named VS2015 Shared/Utils.cpp lines
    // 854-876. Preserve the source's by-value input and non-overlapping
    // left-to-right scan. Callers never pass an empty search string.
    L2_GUARD_FUNCTION();

    const std::u16string_view input = runtime::StringView(source);
    const std::u16string_view searchText(search);
    const std::u16string_view replacementText(replacement);

    std::u16string result;
    std::size_t offset = 0;
    for (;;) {
        const std::size_t found = input.find(searchText, offset);
        if (found == std::u16string_view::npos) {
            break;
        }
        if (offset < found) {
            result.append(input.substr(offset, found - offset));
        }
        result.append(replacementText);
        offset = found + searchText.size();
    }

    if (offset == 0) {
        result.append(input);
    } else if (offset < input.size()) {
        result.append(input.substr(offset));
    }
    return result;
}

std::u16string JoinStringFromMap(std::map<std::int32_t, std::u16string>& input) {
    // Current L2Server 0x00ad88f8, original owner Shared/Utils.cpp.
    std::u16string result;
    for (const auto& entry : input) {
        result.append(runtime::StringView(entry.second));
    }
    return result;
}

char16_t GetChar(std::FILE* stream) {
    // Current Cached.exe unnamed Shared/Utils.cpp body 0x005c8648.
    // Observe only the transition into the stream's sticky error state. A
    // caller that retries without clearerr must not flood the logs. Preserve
    // the original fgetc/feof sequence, return values and error flags.
    bool readErrorAlreadySet = std::ferror(stream) != 0;
    const auto reportReadFailure = [&](int result) {
        if (result != EOF || readErrorAlreadySet || std::ferror(stream) == 0) {
            return;
        }
        readErrorAlreadySet = true;
        const runtime::logging::StorageErrorState preserveErrors;
        const auto path = runtime::logging::SnapshotLogFilePath(stream);
        // Log.Add(LOG_ERROR,
        //     "[scripts] Cannot read UTF-16 input \"%s\" (errno %d: %s).",
        //     path[0] ? path.data() : "<path unavailable>", preserveErrors.savedErrno,
        //     preserveErrors.savedErrno ? std::strerror(preserveErrors.savedErrno) : "not reported")
        L2_LOG_ERROR(
            Log, "[scripts] Cannot read UTF-16 input \"%s\" (errno %d: %s).", path[0] ? path.data() : "<path unavailable>",
            preserveErrors.savedErrno, preserveErrors.savedErrno ? std::strerror(preserveErrors.savedErrno) : "not reported"
        );
    };
    const int low = std::fgetc(stream);
    reportReadFailure(low);
    if (std::feof(stream) != 0) {
        return u'\0';
    }
    const int high = std::fgetc(stream);
    reportReadFailure(high);
    if (std::feof(stream) != 0) {
        if (low != EOF && !readErrorAlreadySet) {
            const runtime::logging::StorageErrorState preserveErrors;
            const auto path = runtime::logging::SnapshotLogFilePath(stream);
            // Log.Add(LOG_WARN,
            //     "[scripts] UTF-16 input \"%s\" ends halfway through a code unit. Trailing byte ignored.",
            //     path[0] ? path.data() : "<path unavailable>")
            L2_LOG_WARN(
                Log, "[scripts] UTF-16 input \"%s\" ends halfway through a code unit; trailing byte ignored.",
                path[0] ? path.data() : "<path unavailable>"
            );
        }
        return u'\0';
    }
    return static_cast<char16_t>(
        static_cast<std::uint16_t>(static_cast<unsigned char>(low)) |
        (static_cast<std::uint16_t>(static_cast<unsigned char>(high)) << 8)
    );
}

bool IsDigit(char16_t value) {
    // Current Cached.exe 0x005c86b0 is this complete Shared helper: despite
    // its historical name, a leading minus sign is accepted with 0..9.
    return (value >= u'0' && value <= u'9') || value == u'-';
}

int GetInt(std::FILE* stream) {
    std::u16string number;
    char16_t value = u'\0';
    do {
        value = GetChar(stream);
        if (IsDigit(value)) {
            break;
        }
    } while (std::feof(stream) == 0);

    do {
        number.append(std::u16string_view(&value, 1));
        value = GetChar(stream);
        if (!IsDigit(value)) {
            break;
        }
    } while (std::feof(stream) == 0);

    return ParseTargetInt32(runtime::StringView(number));
}

std::u16string GetString(std::FILE* stream) {
    // Current Cached.exe unnamed Shared/Utils.cpp body 0x005c9934.
    std::u16string result;
    char16_t value = u'\0';
    do {
        value = GetChar(stream);
        if (value == u'[') {
            break;
        }
    } while (std::feof(stream) == 0);

    while ((value = GetChar(stream)) != u']' && std::feof(stream) == 0) {
        result.append(std::u16string_view(&value, 1));
    }
    return result;
}

char16_t* GetLeftString(char16_t* source, char16_t** token) {
    // Current L2NPC.exe 0x00545fb0. Named donor
    // Shared/Utils.cpp lines 974-1003.
    L2_GUARD_FUNCTION();

    *token = nullptr;
    while (*source == u' ') {
        ++source;
    }
    if (*source == u'\0') {
        return source;
    }

    *token = source;
    while (*source != u'\0' && *source != u' ') {
        ++source;
    }
    if (*source != u'\0') {
        *source = u'\0';
        ++source;
    }
    return source;
}

int L2Util::GetTokenizedStrings(char16_t* source, std::vector<std::u16string>& output) {
    // Current L2NPC.exe 0x00546c4c. The source buffer is intentionally
    // split in place by GetLeftString, exactly as in the target.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    for (;;) {
        source = GetLeftString(source, &token);
        if (!token) {
            break;
        }

        // The shipping body constructs a stack xwstring first and inserts
        // that lvalue into xvector, then destroys the local. Direct
        // emplace_back(token) skipped the target copy/allocation boundary.
        const std::u16string value(token);
        output.push_back(value);
    }
    return static_cast<int>(output.size());
}

int GetDayDiff(_SYSTEMTIME* current, _SYSTEMTIME* previous) {
    // Current L2NPC.exe 0x00545d14. Month 13 is accepted:
    // both target tables carry their end-of-year sentinel at that index.
    L2_GUARD_FUNCTION();

    if (!current || !previous || previous->wYear == 0 || current->wYear == 0 || current->wMonth > 13 ||
        previous->wMonth > 13) {
        return 0;
    }

    // The shipping stack tables initialize only indices 1..13. Month zero
    // passes the validation above and so keeps the original
    // indeterminate read instead of being normalized to day offset zero.
    std::array<int, 14> regularYear;
    regularYear[1] = 0;
    regularYear[2] = 31;
    regularYear[3] = 59;
    regularYear[4] = 90;
    regularYear[5] = 120;
    regularYear[6] = 151;
    regularYear[7] = 181;
    regularYear[8] = 212;
    regularYear[9] = 243;
    regularYear[10] = 273;
    regularYear[11] = 304;
    regularYear[12] = 334;
    regularYear[13] = 365;

    std::array<int, 14> leapYear;
    leapYear[1] = 0;
    leapYear[2] = 31;
    leapYear[3] = 60;
    leapYear[4] = 91;
    leapYear[5] = 121;
    leapYear[6] = 152;
    leapYear[7] = 182;
    leapYear[8] = 213;
    leapYear[9] = 244;
    leapYear[10] = 274;
    leapYear[11] = 305;
    leapYear[12] = 335;
    leapYear[13] = 366;

    const auto dayInYear = [&](const _SYSTEMTIME* value) {
        const auto& offsets = IsTargetLeapYear(value->wYear) ? leapYear : regularYear;
        return static_cast<int>(value->wDay) + offsets[static_cast<std::size_t>(value->wMonth)];
    };

    const int currentDay = dayInYear(current);
    const int previousDay = dayInYear(previous);
    const int yearCount = current->wYear >= previous->wYear ? static_cast<int>(current->wYear - previous->wYear) : 0;

    int interveningDays = 0;
    std::uint32_t year = previous->wYear;
    for (int index = 0; index < yearCount; ++index, ++year) {
        interveningDays += IsTargetLeapYear(static_cast<std::uint16_t>(year)) ? 366 : 365;
    }
    return currentDay + interveningDays - previousDay;
}

int L2Util::Time::GetElapsedMin(SharedUtilsLong* value) {
    // Current Cached.exe 0x005c8770.
    return *value / 60;
}

int L2Util::Time::GetElapsedHour(SharedUtilsLong* value) {
    // Current Cached.exe 0x005c8784. The donor source expresses the second
    // quotient through GetElapsedMin. Signed truncation matches time32/3600.
    return GetElapsedMin(value) / 60;
}

int L2Util::Time::GetElapsedHour() {
    // Current Cached.exe 0x005c9090: _time32 followed by signed idiv 3600.
    // The 32-bit carrier keeps the shipping 2038 boundary.
    L2_GUARD_FUNCTION();
    SharedUtilsLong now = runtime::crt::Time32Now();
    return GetElapsedHour(&now);
}

int Random(int maximum) { return static_cast<int>(genrand64_real1() * static_cast<double>(maximum) + 1.0); }


namespace {

bool IsSharedUtilsDigit(SharedUtilsWideChar value) {
    return (value >= static_cast<SharedUtilsWideChar>('0') && value <= static_cast<SharedUtilsWideChar>('9')) ||
        value == static_cast<SharedUtilsWideChar>('-');
}

template <typename Character>
std::int64_t ParseSharedUtilsInt64(const Character* source) {
    // Every current caller first limits the collected token to at most 31
    // UTF-16 code units, and every accepted unit is ASCII digit or '-'.
    std::array<char, 33> narrow{};
    std::size_t length = 0;
    while (length + 1U < narrow.size() && source[length] != static_cast<Character>(0)) {
        narrow[length] = static_cast<char>(source[length]);
        ++length;
    }
    return std::strtoll(narrow.data(), nullptr, 10);
}

template <typename Character>
int ParseSharedUtilsInt32(const Character* source) {
    const std::int64_t parsed = ParseSharedUtilsInt64(source);
    if (parsed > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (parsed < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<int>(parsed);
}

void AppendSharedUtilsCharacter(std::u16string& destination, SharedUtilsWideChar value) {
    const char16_t targetValue = static_cast<char16_t>(value);
    destination.append(std::u16string_view(&targetValue, 1));
}


} // namespace

char* ReplaceChar(char* buffer, std::size_t length, char source, char replacement) {
    // Current CacheD.exe 0x005c86d0.
    for (std::size_t index = 0; index < length; ++index) {
        if (buffer[index] == source) {
            buffer[index] = replacement;
        }
    }
    return buffer;
}

int AnsiToUnicode(const char* source, int sourceCount, SharedUtilsWideChar* destination, int destinationCount) {
    // Current L2Server.exe 0x00c3c978. Both count arguments are dead. The
    // target makes the same two CP_ACP calls and then writes the final NUL.
    L2_GUARD_FUNCTION();

    const int wideCount = portable::WindowsAnsiToWideChar(source, -1, static_cast<SharedUtilsWideChar*>(nullptr), 0);
    portable::WindowsAnsiToWideChar(source, -1, destination, wideCount);
    destination[wideCount - 1] = static_cast<SharedUtilsWideChar>(0);
    return wideCount;
}

void ParseHTMLParam(const SharedUtilsWideChar* source, char* format, ...) {
    // Current L2Server.exe 0x00c3cda0. Delimiters and the byte-sized S/s
    // capacity contract are kept, including unchecked
    // negative capacities and advancing one code unit past a terminal NUL.
    L2_GUARD_FUNCTION();

    std::va_list arguments;
    va_start(arguments, format);
    for (char value = *format; value != '\0'; value = *++format) {
        int tokenLength = 0;
        while (source[tokenLength] != static_cast<SharedUtilsWideChar>(0) &&
            source[tokenLength] != static_cast<SharedUtilsWideChar>('_') &&
            source[tokenLength] != static_cast<SharedUtilsWideChar>('\r') &&
            source[tokenLength] != static_cast<SharedUtilsWideChar>('\n')) {
            ++tokenLength;
        }

        if (value == 'Q') {
            std::array<SharedUtilsWideChar, 32> token{};
            auto* const destination = va_arg(arguments, std::int64_t*);
            if (tokenLength < static_cast<int>(token.size())) {
                std::copy_n(source, tokenLength, token.data());
                bool valid = tokenLength > 0;
                for (int index = 0; valid && index < tokenLength; ++index) {
                    valid = IsSharedUtilsDigit(token[static_cast<std::size_t>(index)]);
                }
                *destination = valid ? ParseSharedUtilsInt64(token.data()) : 0;
            } else {
                *destination = 0;
            }
        } else if (value == 'S' || value == 's') {
            const int byteCapacity = va_arg(arguments, int);
            auto* const destination = va_arg(arguments, SharedUtilsWideChar*);
            const int maximumLength =
                static_cast<int>(static_cast<std::uint64_t>(static_cast<std::int64_t>(byteCapacity)) >> 1U) - 1;
            const int copyLength = maximumLength <= tokenLength ? maximumLength : tokenLength;
            std::memcpy(destination, source, static_cast<std::size_t>(copyLength) * sizeof(SharedUtilsWideChar));
            destination[copyLength] = static_cast<SharedUtilsWideChar>(0);
        } else if (value == 'd') {
            std::array<SharedUtilsWideChar, 32> token{};
            auto* const destination = va_arg(arguments, int*);
            if (tokenLength < static_cast<int>(token.size())) {
                std::copy_n(source, tokenLength, token.data());
                bool valid = tokenLength > 0;
                for (int index = 0; valid && index < tokenLength; ++index) {
                    valid = IsSharedUtilsDigit(token[static_cast<std::size_t>(index)]);
                }
                *destination = valid ? ParseSharedUtilsInt32(token.data()) : 0;
            } else {
                *destination = 0;
            }
        }

        source += tokenLength == 0 ? 1 : tokenLength + 1;
    }
    va_end(arguments);
}

const SharedUtilsWideChar* ParseHTMLParam2(const SharedUtilsWideChar* source, char* format, ...) {
    // Current L2Server.exe 0x00c3d014. This sibling has no Q
    // branch and returns the cursor after processing the complete format.
    L2_GUARD_FUNCTION();

    std::va_list arguments;
    va_start(arguments, format);
    for (char value = *format; value != '\0'; value = *++format) {
        int tokenLength = 0;
        while (source[tokenLength] != static_cast<SharedUtilsWideChar>(0) &&
            source[tokenLength] != static_cast<SharedUtilsWideChar>('_') &&
            source[tokenLength] != static_cast<SharedUtilsWideChar>('\r') &&
            source[tokenLength] != static_cast<SharedUtilsWideChar>('\n')) {
            ++tokenLength;
        }

        if (value == 'S' || value == 's') {
            const int byteCapacity = va_arg(arguments, int);
            auto* const destination = va_arg(arguments, SharedUtilsWideChar*);
            const int maximumLength =
                static_cast<int>(static_cast<std::uint64_t>(static_cast<std::int64_t>(byteCapacity)) >> 1U) - 1;
            const int copyLength = maximumLength <= tokenLength ? maximumLength : tokenLength;
            std::memcpy(destination, source, static_cast<std::size_t>(copyLength) * sizeof(SharedUtilsWideChar));
            destination[copyLength] = static_cast<SharedUtilsWideChar>(0);
        } else if (value == 'd') {
            std::array<SharedUtilsWideChar, 32> token{};
            auto* const destination = va_arg(arguments, int*);
            if (tokenLength < static_cast<int>(token.size())) {
                std::copy_n(source, tokenLength, token.data());
                bool valid = tokenLength > 0;
                for (int index = 0; valid && index < tokenLength; ++index) {
                    valid = IsSharedUtilsDigit(token[static_cast<std::size_t>(index)]);
                }
                *destination = valid ? ParseSharedUtilsInt32(token.data()) : 0;
            } else {
                *destination = 0;
            }
        }

        source += tokenLength == 0 ? 1 : tokenLength + 1;
    }
    va_end(arguments);
    return source;
}

namespace {
template <typename Character>
Character* NextCommandToken(Character* source, Character** token) {
    *token = nullptr;
    while (*source == static_cast<Character>(' ')) {
        ++source;
    }
    Character delimiter = static_cast<Character>(' ');
    const Character first = *source;
    if (first != Character{} && (first != static_cast<Character>('"') || (++source, *source != Character{}))) {
        *token = source;
        if (first == static_cast<Character>('"')) {
            delimiter = first;
        }
        while (*source != Character{} && *source != delimiter) {
            ++source;
        }
        if (*source != Character{}) {
            *source = Character{};
            return source + 1;
        }
    }
    return source;
}
} // namespace

char16_t* GetLeftString2(char16_t* source, char16_t** token) {
    // Same in-place quoted-token and space rules on every host.
    L2_GUARD_FUNCTION();
    return NextCommandToken(source, token);
}

int GetYYYYMMDDFromTime(SharedUtilsLong time) {
    // Current L2Server.exe 0x00c3d668 and current Cached.exe 0x005c8f0c
    // create this Shared/Utils.cpp body.
    L2_GUARD_FUNCTION();

    std::tm localTime;
    std::memset(&localTime, 0xff, sizeof(localTime));
    const std::int32_t time32 = static_cast<std::int32_t>(time);
#if defined(_MSC_VER)
    const __time32_t source = static_cast<__time32_t>(time32);
    ::_localtime32_s(&localTime, &source);
#elif defined(_WIN32)
    const std::time_t source = static_cast<std::time_t>(time32);
    if (time32 >= 0) {
        ::localtime_s(&localTime, &source);
    }
#else
    const std::time_t source = static_cast<std::time_t>(time32);
    if (time32 >= 0) {
        ::localtime_r(&source, &localTime);
    }
#endif
    return (localTime.tm_year * 100 + localTime.tm_mon + 190001) * 100 + localTime.tm_mday;
}

#if defined(L2_BUILD_L2SERVER)
int GetInitialClassType(int classType, RaceType raceType) {
    L2_GUARD_FUNCTION();

    const ClassType typedClass = static_cast<ClassType>(classType);
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_HUMAN) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_MAGE) : static_cast<int>(CT_FIGHTER);
    }
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_ELF) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_ELVEN_MAGE) : static_cast<int>(CT_ELVEN_FIGHTER);
    }
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_DARKELF) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_DARK_MAGE) : static_cast<int>(CT_DARK_FIGHTER);
    }
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_ORC) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_ORC_MAGE) : static_cast<int>(CT_ORC_FIGHTER);
    }
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_DWARF) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_DWARVEN_MAGE)
                                                   : static_cast<int>(CT_DWARVEN_FIGHTER);
    }
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_KAMAEL) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_KAMAEL_F_SOLDIER)
                                                   : static_cast<int>(CT_KAMAEL_M_SOLDIER);
    }
    if (CPCParamDB::GetRaceByClass(typedClass, raceType) == RT_ERTHEIA) {
        return CT_to_FMT[classType] != FMT_FIGHTER ? static_cast<int>(CT_ERTHEIA_WIZARD)
                                                   : static_cast<int>(CT_ERTHEIA_FIGHTER);
    }
    return static_cast<int>(CT_NONE);
}
#endif

int GetIntFromString(SharedUtilsWideChar* source) {
    // Current L2Server.exe 0x00c3df78. The accepted prefix includes '-'
    // anywhere, is capped at ten code units, and only the positive side is
    // clamped. Negative low-bit truncation is therefore observable.
    L2_GUARD_FUNCTION();

    std::u16string number;
    const int sourceLength = static_cast<int>(std::char_traits<SharedUtilsWideChar>::length(source));
    if (sourceLength < 1) {
        return 0;
    }

    std::size_t cursor = 0;
    int accepted = 0;
    while (IsSharedUtilsDigit(source[cursor])) {
        const int previousAccepted = accepted++;
        if (sourceLength < previousAccepted || 10 < static_cast<long long>(cursor + 1U)) {
            break;
        }
        AppendSharedUtilsCharacter(number, source[cursor]);
        ++cursor;
    }

    const std::int64_t parsed = ParseSharedUtilsInt64(number.c_str());
    if (parsed > 2000000000LL) {
        return 2000000000;
    }
    return runtime::Int32FromBits(parsed);
}

std::int64_t GetInt64FromString(SharedUtilsWideChar* source) {
    // Current L2Server.exe 0x00c3e140. This is the same prefix collector as
    // GetIntFromString with an eighteen-code-unit cap and no result clamp.
    L2_GUARD_FUNCTION();

    std::u16string number;
    const int sourceLength = static_cast<int>(std::char_traits<SharedUtilsWideChar>::length(source));
    if (sourceLength < 1) {
        return 0;
    }

    std::size_t cursor = 0;
    int accepted = 0;
    while (IsSharedUtilsDigit(source[cursor])) {
        const int previousAccepted = accepted++;
        if (sourceLength < previousAccepted || 18 < static_cast<long long>(cursor + 1U)) {
            break;
        }
        AppendSharedUtilsCharacter(number, source[cursor]);
        ++cursor;
    }
    return ParseSharedUtilsInt64(number.c_str());
}

std::u16string Trim(SharedUtilsWideChar* source) {
    // Current L2Server.exe 0x00c3e75c and current Cached.exe 0x005c9cb0.
    // Despite its name, this removes every U+0020 instead of only the ends.
    L2_GUARD_FUNCTION();

    std::u16string result;
    const int sourceLength = static_cast<int>(std::char_traits<SharedUtilsWideChar>::length(source));
    for (int index = 0; index < sourceLength; ++index) {
        const SharedUtilsWideChar value = source[static_cast<std::size_t>(index)];
        if (value != static_cast<SharedUtilsWideChar>(' ')) {
            AppendSharedUtilsCharacter(result, value);
        }
    }
    return result;
}

int ToLowerCase(const SharedUtilsWideChar* source, SharedUtilsWideChar* destination) {
    // Current L2Server.exe 0x00c3bf54. The target does not use locale and
    // does not validate either pointer: its accepted ranges are exactly
    // Latin A..Z and Cyrillic U+0410..U+042F.
    int length = 0;
    while (source[length] != SharedUtilsWideChar{}) {
        SharedUtilsWideChar value = source[length];
        if ((value >= static_cast<SharedUtilsWideChar>('A') && value <= static_cast<SharedUtilsWideChar>('Z')) ||
            (value >= static_cast<SharedUtilsWideChar>(0x0410) && value <= static_cast<SharedUtilsWideChar>(0x042f))) {
            value = static_cast<SharedUtilsWideChar>(value + 0x20);
        }
        destination[length] = value;
        ++length;
    }
    destination[length] = SharedUtilsWideChar{};
    return length;
}

std::u16string LowercaseGameName(std::u16string_view source) {
    const auto terminator = source.find(u'\0');
    std::u16string result(source.substr(0, terminator));
    for (char16_t& value : result) {
        if ((value >= u'A' && value <= u'Z') || (value >= u'\u0410' && value <= u'\u042f')) {
            value = static_cast<char16_t>(value + 0x20);
        }
    }
    return result;
}

std::u16string TrimLR(std::u16string source) {
    // Current L2Server.exe 0x00c3e864. Only U+0020 is whitespace here.
    const std::u16string_view value = runtime::StringView(source);
    std::size_t first = 0;
    while (first < value.size() && value[first] == u' ') {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && value[last - 1] == u' ') {
        --last;
    }
    return std::u16string(value.substr(first, last - first));
}

std::u16string TrimDoubleQuotaionMarks(std::u16string source) {
    // Current L2Server.exe 0x00c3eb74. Strip only double quotes at the ends,
    // including repeated quotes. Spaces and quotes inside the value survive.
    const auto value = runtime::StringView(source);
    int first = 0;
    int last = static_cast<int>(value.size()) - 1;
    while (first < static_cast<int>(value.size()) && value[first] == u'"') {
        ++first;
    }
    while (last >= 0 && value[last] == u'"') {
        --last;
    }
    return first <= last ? std::u16string(value.substr(first, last - first + 1)) : std::u16string();
}

#if defined(_WIN32)
bool LoadStrFromReg(wchar_t* output, const wchar_t* valueName, const wchar_t* subKey) {
    // Current Cached.exe 0x005c8890. The named VS2015 Shared/Utils.cpp donor
    // fixes the source owner and lines 39-88. The target tries
    // HKCU before HKLM, accepts REG_BINARY only and gives RegQueryValueExW an
    // exact 0x100-byte buffer extent.
    L2_GUARD_FUNCTION();

    bool loaded = false;
    HKEY key = nullptr;
    DWORD type = 0;
    DWORD byteCount = 0;
    wchar_t value[256];

    const auto loadFrom = [&](HKEY root) {
        if (::RegOpenKeyExW(root, subKey, 0, 0x00020019, &key) != ERROR_SUCCESS) {
            return false;
        }

        byteCount = 0x100;
        const bool result = ::RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(value), &byteCount) ==
                ERROR_SUCCESS &&
            type == REG_BINARY;
        ::RegCloseKey(key);
        return result;
    };

    loaded = loadFrom(HKEY_CURRENT_USER);
    if (!loaded) {
        loaded = loadFrom(HKEY_LOCAL_MACHINE);
    }
    if (loaded) {
        ::lstrcpyW(output, value);
    }
    return loaded;
}

void SaveStrToReg(wchar_t* value, const wchar_t* valueName, const wchar_t* subKey) {
    // Current Cached.exe 0x005c8a4c. Preserve its opposite write priority
    // (HKLM then HKCU), unchecked 256-wide-character staging copy and fixed
    // 0x100-byte REG_BINARY payload.
    L2_GUARD_FUNCTION();

    wchar_t storedValue[256];
    ::lstrcpyW(storedValue, value);

    HKEY key = nullptr;
    DWORD disposition = 0;
    auto createUnder = [&](HKEY root) {
        return ::RegCreateKeyExW(root, subKey, 0, const_cast<wchar_t*>(L""), 0, 0x00020006, nullptr, &key, &disposition);
    };

    LSTATUS status = createUnder(HKEY_LOCAL_MACHINE);
    if (status != ERROR_SUCCESS) {
        status = createUnder(HKEY_CURRENT_USER);
    }
    if (status == ERROR_SUCCESS) {
        ::RegSetValueExW(key, valueName, 0, REG_BINARY, reinterpret_cast<const BYTE*>(storedValue), 0x100);
        ::RegCloseKey(key);
    }
}
#endif

ServerAutoStarter::Config::Config()
    // Current L2Server 0x00c3e2d4 is folded with the Batch config constructor.
    :
    m_enabled(false), m_filePath(u"UnknownFile"), m_workingFolder(u"UnknownFolder") {}

ServerAutoStarter::Config ServerAutoStarter::config;

BatchAutoStarter::Config::Config()
    // Current L2Server 0x00c3e2d4. The second current caller and storage
    // xrefs distinguish this owner from ServerAutoStarter::config.
    :
    m_enabled(false), m_filePath(u"UnknownFile"), m_workingFolder(u"UnknownFolder") {}

BatchAutoStarter::Config BatchAutoStarter::config;

BatchAutoStarter::BatchAutoStarter() {
    // Current L2Server 0x00c3c324. Folded with ServerAutoStarter.
    std::fill(startupInfo.begin(), startupInfo.end(), std::byte{0});
    std::fill(info.begin(), info.end(), std::byte{0});
    const std::uint32_t size = static_cast<std::uint32_t>(startupInfo.size());
    std::memcpy(startupInfo.data(), &size, sizeof(size));
}

BatchAutoStarter::~BatchAutoStarter() {
    // Current L2Server 0x00c3c358 closes both handles unconditionally.
#if defined(_WIN32)
    auto* const processInformation = reinterpret_cast<PROCESS_INFORMATION*>(info.data());
    ::CloseHandle(processInformation->hProcess);
    ::CloseHandle(processInformation->hThread);
#endif
}

void BatchAutoStarter::Do() {
    // Current L2Server 0x00c3daf8. The fixed command prefix deliberately
    // calls cmd.exe and both log records ignore CreateProcessW's result.
    // Logging modernization observes creation results. Launch and handle policy stay unchanged.
    if (!config.m_enabled) {
        return;
    }

    const std::wstring filePath = Utf16ToWide(config.m_filePath);
    const std::wstring workingFolder = Utf16ToWide(config.m_workingFolder);

    // Noise: The result record identifies the configured launcher. Do not expose command arguments that may contain
    // credentials. Log.Add(
    //     LOG_NORMAL,
    //     L"Try to start [%s]",
    //     filePath.c_str())
#if defined(_WIN32)
    std::array<wchar_t, 0x400> command{};
    ::wcscpy_s(command.data(), command.size(), L"c:\\windows\\system32\\cmd.exe /c ");
    ::wcscat_s(command.data(), command.size(), filePath.c_str());
    // ::CreateProcessW(
    //     nullptr,
    //     command.data(),
    //     nullptr,
    //     nullptr,
    //     FALSE,
    //     0,
    //     nullptr,
    //     workingFolder.c_str(),
    //     reinterpret_cast<STARTUPINFOW*>(startupInfo.data()),
    //     reinterpret_cast<PROCESS_INFORMATION*>(info.data()))
    const BOOL created = ::CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, workingFolder.c_str(),
        reinterpret_cast<STARTUPINFOW*>(startupInfo.data()), reinterpret_cast<PROCESS_INFORMATION*>(info.data())
    );
    if (!created) {
        const DWORD error = ::GetLastError();
        ReportAutoStartFailure(
            "BatchAutoStarter", workingFolder.c_str(), "CreateProcessW", static_cast<unsigned long>(error)
        );
    } else {
        // Log.Add(LOG_NORMAL,
        //     L"[startup] BatchAutoStarter created command interpreter process %lu. Working folder: \"%ls\". Command
        //     completion is not confirmed.", static_cast<unsigned long>(
        //         reinterpret_cast<const PROCESS_INFORMATION*>(info.data())->dwProcessId),
        //     workingFolder.c_str())
        L2_LOG_INFO(
            Log,
            u"[startup] BatchAutoStarter created command interpreter process %lu. Working folder: \"%ls\". Command "
            u"completion is not confirmed.",
            static_cast<unsigned long>(reinterpret_cast<const PROCESS_INFORMATION*>(info.data())->dwProcessId),
            workingFolder.c_str()
        );
    }
#else
    StartPortableBatch(filePath, workingFolder);
#endif
    // Log.Add(
    //     LOG_NORMAL,
    //     L"batch:[%s] started in working folder[%s]",
    //     filePath.c_str(),
    //     workingFolder.c_str())
}

ServerAutoStarter::ServerAutoStarter() {
    // Current Cached.exe 0x005c87e4: zero STARTUPINFOW (0x68), then
    // PROCESS_INFORMATION (0x18), and set STARTUPINFO::cb to 0x68.
    std::fill(startupInfo.begin(), startupInfo.end(), std::byte{0});
    std::fill(info.begin(), info.end(), std::byte{0});
    const std::uint32_t size = static_cast<std::uint32_t>(startupInfo.size());
    std::memcpy(startupInfo.data(), &size, sizeof(size));
}

ServerAutoStarter::~ServerAutoStarter() {
    // Current Cached.exe 0x005c8818 closes both returned handles without
    // checking CreateProcessW's result.
#if defined(_WIN32)
    auto* const processInformation = reinterpret_cast<PROCESS_INFORMATION*>(info.data());
    ::CloseHandle(processInformation->hProcess);
    ::CloseHandle(processInformation->hThread);
#endif
}

void ServerAutoStarter::Do() {
    // Current Cached.exe 0x005c92e4. The return value is deliberately ignored
    // and the second message is logged even if process creation fails.
    // Logging modernization observes creation results. Launch and handle policy stay unchanged.
    if (!config.m_enabled) {
        return;
    }

    // const std::wstring filePath =
    //     Utf16ToWide(config.m_filePath)
    // CreateProcessW requires a writable command-line buffer.
    std::wstring filePath = Utf16ToWide(config.m_filePath);
    const std::wstring workingFolder = Utf16ToWide(config.m_workingFolder);

    // Noise: The result record identifies the configured launcher. Do not expose command arguments that may contain
    // credentials. Log.Add(
    //     LOG_NORMAL,
    //     L"Try to start [%s]",
    //     filePath.c_str())
#if defined(_WIN32)
    // ::CreateProcessW(
    //     nullptr,
    //     filePath.data(),
    //     nullptr,
    //     nullptr,
    //     FALSE,
    //     0,
    //     nullptr,
    //     workingFolder.c_str(),
    //     reinterpret_cast<STARTUPINFOW*>(
    //         startupInfo.data()),
    //     reinterpret_cast<PROCESS_INFORMATION*>(
    //         info.data()))
    const BOOL created = ::CreateProcessW(
        nullptr, filePath.data(), nullptr, nullptr, FALSE, 0, nullptr, workingFolder.c_str(),
        reinterpret_cast<STARTUPINFOW*>(startupInfo.data()), reinterpret_cast<PROCESS_INFORMATION*>(info.data())
    );
    if (!created) {
        const DWORD error = ::GetLastError();
        ReportAutoStartFailure(
            "ServerAutoStarter", workingFolder.c_str(), "CreateProcessW", static_cast<unsigned long>(error)
        );
    } else {
        // Log.Add(LOG_NORMAL,
        //     L"[startup] ServerAutoStarter created child process %lu. Working folder: \"%ls\". Application readiness is not
        //     confirmed.", static_cast<unsigned long>(
        //         reinterpret_cast<const PROCESS_INFORMATION*>(info.data())->dwProcessId),
        //     workingFolder.c_str())
        L2_LOG_INFO(
            Log,
            u"[startup] ServerAutoStarter created child process %lu. Working folder: \"%ls\". Application readiness is not "
            u"confirmed.",
            static_cast<unsigned long>(reinterpret_cast<const PROCESS_INFORMATION*>(info.data())->dwProcessId),
            workingFolder.c_str()
        );
    }
#else
    StartPortableProcess(filePath, workingFolder);
#endif
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[%s] started in working folder[%s]",
    //     filePath.c_str(),
    //     workingFolder.c_str())
}

GuardAddTimer::GuardAddTimer(CIOObject* const object, const int time, const int id) :
    m_pObject(object), m_nTime(time), m_nID(id) {}

GuardAddTimer::~GuardAddTimer() { m_pObject->AddTimer(static_cast<std::int64_t>(m_nTime), m_nID); }

JobTransactionLock::JobTransactionLock() : m_nJobType(0), m_nTimeout(0), m_tBeginJobTime(0) {}

JobTransactionLock::JobTransactionLock(int timeout) :
    m_nJobType(0), m_nTimeout(timeout > 0 ? timeout : 0), m_tBeginJobTime(0) {}

bool JobTransactionLock::BeginJob(JobTransactionLong* jobType) {
    const std::int32_t desired = static_cast<std::int32_t>(*jobType);
    const std::int32_t observed = runtime::raw_atomic::CompareExchangeI32(&m_nJobType, desired, 0);
    if (observed == 0) {
        runtime::raw_atomic::StoreI32Relaxed(
            reinterpret_cast<volatile std::int32_t*>(&m_tBeginJobTime), runtime::crt::Time32Now()
        );
        return true;
    }

    const std::int32_t beginTime =
        runtime::raw_atomic::LoadI32Relaxed(reinterpret_cast<const volatile std::int32_t*>(&m_tBeginJobTime));
    if (m_nTimeout > 0 && beginTime > 0) {
        const std::int32_t now = runtime::crt::Time32Now();
        const std::int32_t elapsed =
            runtime::Int32FromBits(static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(beginTime));
        if (elapsed >= m_nTimeout) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"JobTransactionLock timeout : %d",
            //     static_cast<int>(*jobType))
            // Log.Add(LOG_ERROR,
            //     "[transaction] Job %d exceeded its timeout: elapsed %d seconds, limit %d seconds. Replacing the tracked
            //     job with requested job %d, this does not cancel the previous job.", static_cast<int>(observed),
            //     static_cast<int>(elapsed), m_nTimeout, static_cast<int>(*jobType))
            L2_LOG_ERROR(
                Log,
                "[transaction] Job %d exceeded its timeout: elapsed %d seconds, limit %d seconds. Replacing the tracked job "
                "with requested job %d; this does not cancel the previous job.",
                static_cast<int>(observed), static_cast<int>(elapsed), m_nTimeout, static_cast<int>(*jobType)
            );
            const std::int32_t replacement = static_cast<std::int32_t>(*jobType);
            runtime::raw_atomic::StoreI32Relaxed(reinterpret_cast<volatile std::int32_t*>(&m_tBeginJobTime), now);
            runtime::raw_atomic::StoreI32Relaxed(&m_nJobType, replacement);
            return true;
        }
    }

    *jobType = static_cast<JobTransactionLong>(observed);
    return false;
}

bool JobTransactionLock::EndJob() { return runtime::raw_atomic::ExchangeI32(&m_nJobType, 0) != 0; }
