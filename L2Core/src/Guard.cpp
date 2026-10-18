#include "L2Core/src/inc/Guard.h"

#include "L2Core/src/inc/FileUtils.h"

int GuardHelper::_callStackDepth[MAX_CALLSTACK_THREAD]{};
const wchar_t* GuardHelper::_pszCallStack[MAX_CALLSTACK_THREAD][MAX_CALLSTACK_DEPTH]{};
std::uint32_t GuardHelper::_dwCallStackStartTick[MAX_CALLSTACK_THREAD][MAX_CALLSTACK_DEPTH]{};

const wchar_t* GuardHelper::getCallStack(int threadNumber, int depth) { return _pszCallStack[threadNumber][depth]; }

int GuardHelper::getCallStackDepth(int threadNumber) { return _callStackDepth[threadNumber]; }

const wchar_t* GuardHelper::getCaller(int threadNumber) {
    const int callerDepth = _callStackDepth[threadNumber] - 2;
    return callerDepth < 0 ? nullptr : _pszCallStack[threadNumber][callerDepth];
}

void GuardHelper::printCallStack(void* file, int threadNumber) {
    for (int depth = 0; depth < _callStackDepth[threadNumber]; ++depth) {
        hprintf(file, L"[%d] %ls\r\n", depth, _pszCallStack[threadNumber][depth]);
    }
}

void GuardHelper::printCallStackWithElapsedTime(void* file, int threadNumber) {
    int depth = _callStackDepth[threadNumber];
    for (int index = 0; index < depth - 1; ++index) {
        hprintf(
            file, L"\t%ls [%u ms] -> ", _pszCallStack[threadNumber][index],
            _dwCallStackStartTick[threadNumber][index + 1] - _dwCallStackStartTick[threadNumber][index]
        );
        depth = _callStackDepth[threadNumber];
    }
    if (depth > 0) {
        const int last = depth - 1;
        hprintf(
            file, L"%ls [%u ms]", _pszCallStack[threadNumber][last],
            runtime::GetTickCount() - _dwCallStackStartTick[threadNumber][last]
        );
    }
    hprintf(file, L"\r\n");
}
