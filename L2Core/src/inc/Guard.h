#pragma once

#include "L2Core/src/inc/GuardProfile.h"
#include "Runtime.h"
#include "Shared/src/ThreadManager.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The original L2Core StackGuard header defines __WFILE__ as the widened
// compiler source-file macro. Prefer the basename form on modern compilers so
// portable builds retain target-style diagnostics such as L"BattleCampDb.cpp"
// instead of embedding an absolute build-machine path.
#ifndef WIDEN2
#define WIDEN2(value) L##value
#endif
#ifndef WIDEN
#define WIDEN(value) WIDEN2(value)
#endif
#ifndef __WFILE__
#ifdef __FILE_NAME__
#define __WFILE__ WIDEN(__FILE_NAME__)
#else
#define __WFILE__ WIDEN(__FILE__)
#endif
#endif

// The guard stores a logical thread number.
// sGuardIndex is zero when profiling is disabled.
class GuardHelper {
public:
    GuardHelper(const wchar_t* functionName, int threadNumber) : _threadNumber(threadNumber) {
        const int depth = _callStackDepth[_threadNumber]++;
        _pszCallStack[_threadNumber][depth] = functionName;
        _dwCallStackStartTick[_threadNumber][depth] = runtime::GetTickCount();
    }

    ~GuardHelper() { --_callStackDepth[_threadNumber]; }

    static const wchar_t* getCallStack(int threadNumber, int depth);
    static int getCallStackDepth(int threadNumber);
    static const wchar_t* getCaller(int threadNumber);
    static void printCallStack(void* file, int threadNumber);
    static void printCallStackWithElapsedTime(void* file, int threadNumber);

    static constexpr int MAX_CALLSTACK_THREAD = 64;
    static constexpr int MAX_CALLSTACK_DEPTH = 1000;

protected:
    int _threadNumber;

    static int _callStackDepth[MAX_CALLSTACK_THREAD];
    static const wchar_t* _pszCallStack[MAX_CALLSTACK_THREAD][MAX_CALLSTACK_DEPTH];
    static std::uint32_t _dwCallStackStartTick[MAX_CALLSTACK_THREAD][MAX_CALLSTACK_DEPTH];
};


namespace l2core {

template <std::size_t N>
constexpr std::array<wchar_t, N> WideFunctionSignature(const char (&signature)[N]) {
    std::array<wchar_t, N> result{};
    for (std::size_t index = 0; index < N; ++index) {
        result[index] = static_cast<unsigned char>(signature[index]);
    }
    return result;
}

template <std::size_t PrefixSize, std::size_t SignatureSize>
constexpr std::array<wchar_t, PrefixSize + SignatureSize - 1>
WidePrefixedFunctionSignature(
    const wchar_t (&prefix)[PrefixSize],
    const char (&signature)[SignatureSize]) {
    std::array<wchar_t, PrefixSize + SignatureSize - 1> result{};
    for (std::size_t index = 0; index + 1 < PrefixSize; ++index) {
        result[index] = prefix[index];
    }
    for (std::size_t index = 0; index < SignatureSize; ++index) {
        result[PrefixSize - 1 + index] =
            static_cast<unsigned char>(signature[index]);
    }
    return result;
}

} // namespace l2core

#ifndef L2_ENABLE_GUARD_PROFILING
#define L2_ENABLE_GUARD_PROFILING 0
#endif

#if L2_ENABLE_GUARD_PROFILING
#define L2_GUARD_PROFILE(functionName)                                                                                       \
    static const int sGuardIndex = GuardProfileHelper::RegisterGuard((functionName), true);                                  \
    GuardProfileHelper __guard_profile_var__(sGuardIndex, (functionName), true)
#else
#define L2_GUARD_PROFILE(functionName) [[maybe_unused]] static constexpr int sGuardIndex = 0
#endif

#if defined(_MSC_VER)
#define L2_GUARD_WIDEN_IMPL(value) L##value
#define L2_GUARD_WIDEN(value) L2_GUARD_WIDEN_IMPL(value)
#define L2_GUARD_PREFIXED_FUNCTION(                         \
    guardPrefix,                                            \
    functionNameVariable,                                  \
    stackVariable)                                         \
    static const wchar_t functionNameVariable[] =           \
        guardPrefix L2_GUARD_WIDEN(__FUNCSIG__);            \
    GuardHelper stackVariable(                              \
        functionNameVariable,                               \
        CThreadManager::GetThreadID())
#define L2_GUARD_FUNCTION()                                                                                                 \
    static const wchar_t __FUNC_NAME__[] = L2_GUARD_WIDEN(__FUNCSIG__);                                                     \
    L2_GUARD_PROFILE(__FUNC_NAME__);                                                                                         \
    GuardHelper __stack_var__(__FUNC_NAME__, CThreadManager::GetThreadID())
#else
#define L2_GUARD_PREFIXED_FUNCTION(                         \
    guardPrefix,                                            \
    functionNameVariable,                                  \
    stackVariable)                                         \
    static constexpr auto functionNameVariable =            \
        l2core::WidePrefixedFunctionSignature(              \
            guardPrefix,                                    \
            __PRETTY_FUNCTION__);                           \
    GuardHelper stackVariable(                              \
        functionNameVariable.data(),                        \
        CThreadManager::GetThreadID())
#define L2_GUARD_FUNCTION()                                                                                                 \
    static constexpr auto __FUNC_NAME__ = l2core::WideFunctionSignature(__PRETTY_FUNCTION__);                               \
    L2_GUARD_PROFILE(__FUNC_NAME__.data());                                                                                  \
    GuardHelper __stack_var__(__FUNC_NAME__.data(), CThreadManager::GetThreadID())
#endif
